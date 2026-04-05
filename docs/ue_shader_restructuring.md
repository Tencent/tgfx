# tgfx UE RHI 后端 Shader 重构方案：离线 GLSL 预生成 + SPIRV 转 HLSL + VariantID Permutation

## 1. 概述与目标

### 1.1 背景

tgfx 当前的 Shader 系统采用运行时动态拼装：每个 DrawOp 在执行时组合 GeometryProcessor、FragmentProcessor 链和 XferProcessor，由 `ProgramBuilder` 将各 Processor 的 `emitCode()` 输出拼接为完整的 GLSL 代码串，再交给各后端编译（OpenGL 直接编译，Metal 走 GLSL -> SPIRV -> MSL 管线）。

Unreal Engine 的 Shader 系统要求所有 Shader 在 Editor 阶段（Cook 或 DDC）预编译为平台二进制。UE 不支持运行时提交 GLSL/HLSL 源码编译——其 RHI Shader 必须在启动前已经由 Shader Compiler Worker 编译完成。因此 tgfx 的运行时动态拼装模型无法直接用于 UE RHI 后端。

### 1.2 方案选型

**离线 GLSL 预生成 + SPIRV 转 HLSL + VariantID Permutation** 的核心思路：

1. **构建时**：用离线工具枚举 tgfx 所有合法的 Processor 组合，调用现有 `ProgramBuilder` 生成 GLSL 源码，经 `preprocessGLSL() -> shaderc(SPIRV) -> SPIRV-Cross(HLSL)` 管线转为 HLSL，再包装成 UE `.usf/.ush` 文件。每个唯一的 Shader 变体分配一个整数 VariantID。
2. **运行时**：tgfx 计算 `ProgramKey`（与现有逻辑完全相同），通过 `ShaderVariantRegistry` 查找对应的 VariantID，用 UE 的 `TShaderMapRef` + Permutation 获取预编译好的 Shader。

选此方案的理由：
- **复用现有架构**：不修改 Processor 的 `emitCode()` 逻辑，离线工具直接调用现有代码生成 GLSL。
- **变体可控**：tgfx 的 FP 链由 Canvas API 驱动，实际管线模板约 14 种，估算 300-800 个唯一变体，UE Permutation 系统可承载。
- **跨平台一致性**：SPIRV 作为中间表示，可生成 HLSL（D3D11/D3D12）、GLSL（Vulkan）、MSL（Metal），覆盖 UE 所有目标平台。
- **参考先例**：Rive UE 插件采用类似架构（离线 GLSL -> HLSL + Permutation），已在生产环境验证。

### 1.3 目标

- 所有 tgfx 渲染功能在 UE RHI 后端下保持正确性和视觉一致性
- Shader 全部离线预编译，运行时零编译开销
- 与 tgfx 现有代码最小侵入性改动
- 支持 UE 5.4 / 5.5 / 5.6 / 5.7

---

## 2. 架构总览

### 2.1 构建时流程

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Build-time: TgfxShaderVariantGenerator            │
│                                                                     │
│  ┌──────────────┐    ┌──────────────────┐    ┌───────────────────┐  │
│  │ MockContext   │───>│ Enumerate all     │───>│ ProgramBuilder    │  │
│  │ + MockGPU     │    │ valid Processor   │    │ .emitCode()       │  │
│  │ + ShaderCaps  │    │ combinations      │    │ -> GLSL strings   │  │
│  └──────────────┘    └──────────────────┘    └─────────┬─────────┘  │
│                                                        │            │
│                       ┌────────────────────────────────┘            │
│                       ▼                                             │
│  ┌──────────────────────────────────┐   ┌─────────────────────────┐ │
│  │ preprocessGLSL()                 │──>│ shaderc: GLSL -> SPIRV  │ │
│  │ (reused from MetalShaderModule)  │   └───────────┬─────────────┘ │
│  └──────────────────────────────────┘               │               │
│                                                     ▼               │
│  ┌──────────────────────────────────┐   ┌─────────────────────────┐ │
│  │ SPIRV-Cross: SPIRV -> HLSL      │<──│ SPIRV binary            │ │
│  │ + postProcessHLSL()             │   └─────────────────────────┘ │
│  └───────────────┬──────────────────┘                               │
│                  ▼                                                   │
│  ┌──────────────────────────────────┐   ┌─────────────────────────┐ │
│  │ Write .ush generated files       │   │ Write registry JSON     │ │
│  │ (one VS + one PS per variant)    │   │ ProgramKey -> VariantID │ │
│  └──────────────────────────────────┘   └─────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 运行时流程

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Runtime: UE RHI Backend                           │
│                                                                     │
│  DrawOp::execute()                                                  │
│    │                                                                │
│    ├─> ProgramInfo::getProgram()                                    │
│    │     │                                                          │
│    │     ├─> Compute ProgramKey (unchanged)                         │
│    │     │                                                          │
│    │     ├─> ShaderVariantRegistry::findVariantID(programKey)       │
│    │     │     │                                                    │
│    │     │     └─> Returns { drawOpType, variantID }                │
│    │     │                                                          │
│    │     ├─> TShaderMapRef<FTgfxXxxVS>(ShaderMap, variantID)        │
│    │     ├─> TShaderMapRef<FTgfxXxxPS>(ShaderMap, variantID)        │
│    │     │                                                          │
│    │     └─> Build FGraphicsPipelineStateInitializer                │
│    │           + blend state, vertex decl, etc.                     │
│    │                                                                │
│    ├─> UERHIRenderPass::setPipeline(UERHIProgram)                   │
│    ├─> UERHIRenderPass::setUniformBuffer(...)                       │
│    ├─> UERHIRenderPass::setTexture(...)                             │
│    └─> DrawOp::onDraw(renderPass)                                   │
│          └─> UERHIRenderPass::drawIndexed(...)                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 3. 离线 Shader 变体枚举工具

### 3.1 工具架构

离线枚举工具是一个独立的命令行程序（`TgfxShaderVariantGenerator`），链接 tgfx 核心库，运行在开发机上。它不需要真实 GPU——通过 MockContext 模拟所有 GPU 相关接口。

**文件位置**：`src/gpu/ue/tools/TgfxShaderVariantGenerator.cpp`

**输出产物**：
1. `Generated/` 目录下的 `.ush` 文件（HLSL Shader 代码）
2. `TgfxShaderVariantRegistry.generated.h` — ProgramKey 到 VariantID 的编译期映射表
3. `tgfx_shader_variants.json` — 人类可读的变体清单（调试用）

### 3.2 MockContext 设计

MockContext 需要提供 `Context`、`GPU`、`RenderTarget` 和 `ShaderCaps` 的完整模拟，使得 `ProgramBuilder::CreateProgram()` 可以执行 GLSL 生成（但不需要执行实际 GPU 编译）。

```cpp
// src/gpu/ue/tools/MockGPU.h
// Copyright (C) 2026 Tencent. All rights reserved.

#pragma once

#include "tgfx/gpu/GPU.h"

namespace tgfx {

class MockShaderModule : public ShaderModule {
 public:
  std::string code;
  ShaderStage stage;
};

class MockRenderPipeline : public RenderPipeline {
 public:
  RenderPipelineDescriptor descriptor;
};

class MockGPUBuffer : public GPUBuffer {
 public:
  explicit MockGPUBuffer(size_t size) : bufferSize(size), data(size, 0) {
  }

  void* map(size_t offset, size_t length) override {
    return data.data() + offset;
  }

  void unmap() override {
  }

  size_t size() const override {
    return bufferSize;
  }

 private:
  size_t bufferSize;
  std::vector<uint8_t> data;
};

class MockGPU : public GPU {
 public:
  const GPUInfo* info() const override {
    return &gpuInfo;
  }

  const GPUFeatures* features() const override {
    return &gpuFeatures;
  }

  const GPULimits* limits() const override {
    return &gpuLimits;
  }

  CommandQueue* queue() const override {
    return nullptr;
  }

  std::shared_ptr<GPUBuffer> createBuffer(size_t size, uint32_t) override {
    return std::make_shared<MockGPUBuffer>(size);
  }

  bool isFormatRenderable(PixelFormat) const override {
    return true;
  }

  int getSampleCount(int requestedCount, PixelFormat) const override {
    return requestedCount;
  }

  std::shared_ptr<Texture> createTexture(const TextureDescriptor&) override {
    return nullptr;
  }

  std::vector<std::shared_ptr<Texture>> importHardwareTextures(HardwareBufferRef,
                                                                uint32_t) override {
    return {};
  }

  std::shared_ptr<Texture> importBackendTexture(const BackendTexture&, uint32_t,
                                                 bool) override {
    return nullptr;
  }

  std::shared_ptr<Texture> importBackendRenderTarget(const BackendRenderTarget&) override {
    return nullptr;
  }

  std::shared_ptr<Semaphore> importBackendSemaphore(const BackendSemaphore&) override {
    return nullptr;
  }

  BackendSemaphore stealBackendSemaphore(std::shared_ptr<Semaphore>) override {
    return {};
  }

  std::shared_ptr<Sampler> createSampler(const SamplerDescriptor&) override {
    return nullptr;
  }

  std::shared_ptr<ShaderModule> createShaderModule(
      const ShaderModuleDescriptor& descriptor) override {
    auto module = std::make_shared<MockShaderModule>();
    module->code = descriptor.code;
    module->stage = descriptor.stage;
    return module;
  }

  std::shared_ptr<RenderPipeline> createRenderPipeline(
      const RenderPipelineDescriptor& descriptor) override {
    auto pipeline = std::make_shared<MockRenderPipeline>();
    pipeline->descriptor = descriptor;
    return pipeline;
  }

  std::shared_ptr<CommandEncoder> createCommandEncoder() override {
    return nullptr;
  }

 private:
  GPUInfo gpuInfo = {};
  GPUFeatures gpuFeatures = {};
  GPULimits gpuLimits = {};
};

}  // namespace tgfx
```

MockContext 还需要一个 MockRenderTarget：

```cpp
// src/gpu/ue/tools/MockRenderTarget.h
// Copyright (C) 2026 Tencent. All rights reserved.

#pragma once

#include "gpu/resources/RenderTarget.h"

namespace tgfx {

class MockRenderTarget : public RenderTarget {
 public:
  MockRenderTarget(Context* context, int width, int height, PixelFormat format,
                   int sampleCount, ImageOrigin origin)
      : RenderTarget(context, width, height, format, sampleCount, origin) {
  }
};

}  // namespace tgfx
```

ShaderCaps 应配置为与 UE HLSL 后端兼容的参数：

```cpp
// Build a ShaderCaps for offline GLSL generation targeting UE HLSL.
static ShaderCaps MakeUEShaderCaps() {
  ShaderCaps caps;
  // GLSL 330 is a good baseline that the Metal backend also uses.
  // The preprocessGLSL() will upgrade to #version 450 anyway.
  caps.versionDeclString = "#version 330";
  caps.usesPrecisionModifiers = false;
  caps.frameBufferFetchSupport = false;
  caps.maxFragmentSamplers = 16;
  caps.maxUBOSize = 65536;
  caps.uboOffsetAlignment = 256;
  return caps;
}
```

### 3.3 枚举策略

枚举的核心原则：**只枚举实际可达到的 Processor 组合，而非笛卡尔积**。tgfx 的 FP 链由 Canvas API 驱动，形成约 14 种管线模板（见 1.3 节管线模板分析）。每种模板内部，Processor 的参数变体（如 AAType、是否有颜色属性、纹理类型等）产生有限的 ProcessorKey 变体。

**枚举维度**：

| 维度 | 取值范围 | 说明 |
|------|----------|------|
| DrawOp 类型 | 9 种 | 决定 GeometryProcessor 类型 |
| AAType | None, MSAA, Coverage | 影响 GP 的 key |
| GP 参数变体 | 每种 GP 2-8 种 | 如 hasColor, hasUV, hasCoverage 等布尔组合 |
| Color FP 链 | ~20 种模板 | ConstColor, TextureEffect, Gradient 组合等 |
| Coverage FP | 有/无 AARectEffect | 2 种 |
| XferProcessor | Empty, PorterDuff(hasCoverage), PorterDuff(noCoverage) | 3 种 |
| BlendMode | Src, SrcOver (常用) + 其他需 DstTexture 的模式 | 详见下文 |
| OutputSwizzle | RGBA, BGRA | 取决于 PixelFormat |
| CullMode | None, Front, Back | 3 种 |
| PixelFormat | RGBA_8888, BGRA_8888 | RenderTarget 格式 |
| SampleCount | 1, 4 | MSAA |

### 3.4 每种 DrawOp 类型的枚举逻辑

以下是精确的枚举代码。每种 DrawOp 类型对应一组 GP 参数变体和合法 FP 链。

```cpp
// src/gpu/ue/tools/ShaderVariantEnumerator.h
// Copyright (C) 2026 Tencent. All rights reserved.

#pragma once

#include <functional>
#include <vector>
#include "gpu/ProgramInfo.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/GeometryProcessor.h"
#include "gpu/processors/XferProcessor.h"

namespace tgfx {

struct VariantConfig {
  PixelFormat format = PixelFormat::RGBA_8888;
  int sampleCount = 1;
  BlendMode blendMode = BlendMode::SrcOver;
  CullMode cullMode = CullMode::None;
  ImageOrigin origin = ImageOrigin::TopLeft;
};

// Global configuration dimensions to enumerate.
static const PixelFormat RENDER_TARGET_FORMATS[] = {
    PixelFormat::RGBA_8888,
    PixelFormat::BGRA_8888,
};

static const int SAMPLE_COUNTS[] = {1, 4};

static const CullMode CULL_MODES[] = {
    CullMode::None,
    CullMode::Front,
    CullMode::Back,
};

// Only enumerate BlendModes that produce different ProgramKeys.
// BlendMode affects the key via:
//   1. programKey.write(static_cast<uint32_t>(blendMode))
//   2. Whether a PorterDuffXferProcessor is present (for non-coefficient modes)
// Coefficient-based modes: Src, Dst, SrcOver, DstOver, SrcIn, DstIn,
//                          SrcOut, DstOut, SrcATop, DstATop, Xor, Plus,
//                          Screen, Modulate
// Non-coefficient (need DstTexture): Overlay, Darken, Lighten, ColorDodge,
//                                    ColorBurn, HardLight, SoftLight, Difference,
//                                    Exclusion, Multiply, Hue, Saturation,
//                                    Color, Luminosity
static const BlendMode COEFFICIENT_BLEND_MODES[] = {
    BlendMode::Src,
    BlendMode::SrcOver,
};

static const BlendMode DST_TEXTURE_BLEND_MODES[] = {
    BlendMode::Overlay,
    // One representative is sufficient because PorterDuff XP key only depends
    // on BlendMode enum value + hasDstTexture. We enumerate a few to cover
    // all unique XP keys.
    BlendMode::ColorDodge,
    BlendMode::Luminosity,
};

// Callback invoked for each unique ProgramInfo.
using VariantCallback = std::function<void(const ProgramInfo& programInfo,
                                           const BytesKey& programKey,
                                           const std::string& vertexGLSL,
                                           const std::string& fragmentGLSL)>;

class ShaderVariantEnumerator {
 public:
  ShaderVariantEnumerator(Context* mockContext, BlockAllocator* allocator);

  void enumerateAll(const VariantCallback& callback);

 private:
  Context* context;
  BlockAllocator* allocator;

  // Per DrawOp type enumerators.
  void enumerateRectDrawOp(const VariantCallback& callback);
  void enumerateRRectDrawOp(const VariantCallback& callback);
  void enumerateShapeDrawOp(const VariantCallback& callback);
  void enumerateAtlasTextOp(const VariantCallback& callback);
  void enumerateQuads3DDrawOp(const VariantCallback& callback);
  void enumerateMeshDrawOp(const VariantCallback& callback);
  void enumerateHairlineLineOp(const VariantCallback& callback);
  void enumerateHairlineQuadOp(const VariantCallback& callback);
  void enumerateShapeInstancedDrawOp(const VariantCallback& callback);

  // Enumerate all valid color FP chains and invoke callback for each.
  void enumerateColorFPChains(
      const std::function<void(std::vector<PlacementPtr<FragmentProcessor>>& colors)>& callback);

  // Enumerate coverage FP variants (none, AARectEffect).
  void enumerateCoverageFPs(
      const std::function<void(std::vector<PlacementPtr<FragmentProcessor>>& coverages)>& callback);

  // Build ProgramInfo and invoke the VariantCallback if the key is new.
  void tryEmitVariant(PlacementPtr<GeometryProcessor> gp,
                      std::vector<PlacementPtr<FragmentProcessor>>& colors,
                      std::vector<PlacementPtr<FragmentProcessor>>& coverages,
                      PlacementPtr<XferProcessor> xp,
                      const VariantConfig& config,
                      const VariantCallback& callback);

  BytesKeyMap<bool> seenKeys;
};

}  // namespace tgfx
```

**RectDrawOp 枚举示例**（最复杂的 DrawOp，其他类型类似但更简单）：

```cpp
// src/gpu/ue/tools/ShaderVariantEnumerator.cpp (excerpt)
// Copyright (C) 2026 Tencent. All rights reserved.

#include "ShaderVariantEnumerator.h"
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/ConicGradientLayout.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/DiamondGradientLayout.h"
#include "gpu/processors/DualIntervalGradientColorizer.h"
#include "gpu/processors/EmptyXferProcessor.h"
#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"
#include "gpu/processors/LinearGradientLayout.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/RadialGradientLayout.h"
#include "gpu/processors/RoundStrokeRectGeometryProcessor.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TextureGradientColorizer.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/processors/UnrolledBinaryGradientColorizer.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"
#include "MockRenderTarget.h"

namespace tgfx {

ShaderVariantEnumerator::ShaderVariantEnumerator(Context* mockContext, BlockAllocator* allocator)
    : context(mockContext), allocator(allocator) {
}

void ShaderVariantEnumerator::enumerateAll(const VariantCallback& callback) {
  enumerateRectDrawOp(callback);
  enumerateRRectDrawOp(callback);
  enumerateShapeDrawOp(callback);
  enumerateAtlasTextOp(callback);
  enumerateQuads3DDrawOp(callback);
  enumerateMeshDrawOp(callback);
  enumerateHairlineLineOp(callback);
  enumerateHairlineQuadOp(callback);
  enumerateShapeInstancedDrawOp(callback);
}

void ShaderVariantEnumerator::enumerateRectDrawOp(const VariantCallback& callback) {
  // RectDrawOp uses QuadPerEdgeAAGeometryProcessor or RoundStrokeRectGeometryProcessor.
  // QuadPerEdgeAAGeometryProcessor key dimensions:
  //   - AAType: None, Coverage (MSAA handled at pipeline level)
  //   - hasColor: bool (commonColor.has_value() inverts this)
  //   - hasUV: bool (uvMatrix.has_value())
  //   - hasSubset: bool
  //   - hasUVPerspective: bool (only when hasUV && matrix has perspective)

  struct GPVariant {
    AAType aa;
    bool hasCommonColor;
    bool hasUV;
    bool hasSubset;
  };

  GPVariant gpVariants[] = {
      // Solid color fill, no AA
      {AAType::None, true, false, false},
      // Solid color fill, coverage AA
      {AAType::Coverage, true, false, false},
      // Textured, no AA
      {AAType::None, false, true, false},
      // Textured, coverage AA
      {AAType::Coverage, false, true, false},
      // Textured with subset
      {AAType::Coverage, false, true, true},
      // Per-vertex color, no UV (rare but possible)
      {AAType::Coverage, false, false, false},
  };

  for (auto& gpVar : gpVariants) {
    for (auto format : RENDER_TARGET_FORMATS) {
      for (auto sampleCount : SAMPLE_COUNTS) {
        for (auto cullMode : CULL_MODES) {
          auto gp = QuadPerEdgeAAGeometryProcessor::Make(
              allocator, 1024, 1024, gpVar.aa,
              gpVar.hasCommonColor ? std::optional<PMColor>(PMColor::White()) : std::nullopt,
              gpVar.hasUV ? std::optional<Matrix>(Matrix::I()) : std::nullopt,
              gpVar.hasSubset);

          enumerateColorFPChains([&](auto& colors) {
            enumerateCoverageFPs([&](auto& coverages) {
              // For coefficient-based blend modes, no XferProcessor needed.
              for (auto blendMode : COEFFICIENT_BLEND_MODES) {
                VariantConfig config;
                config.format = format;
                config.sampleCount = sampleCount;
                config.blendMode = blendMode;
                config.cullMode = cullMode;
                tryEmitVariant(gp->clone(allocator), colors, coverages,
                               nullptr, config, callback);
              }

              // For dst-texture blend modes, PorterDuffXferProcessor is created.
              for (auto blendMode : DST_TEXTURE_BLEND_MODES) {
                DstTextureInfo dstInfo = {};  // Mock dst texture info
                auto xp = PorterDuffXferProcessor::Make(allocator, blendMode, dstInfo);
                VariantConfig config;
                config.format = format;
                config.sampleCount = sampleCount;
                config.blendMode = blendMode;
                config.cullMode = cullMode;
                tryEmitVariant(gp->clone(allocator), colors, coverages,
                               std::move(xp), config, callback);
              }
            });
          });
        }
      }
    }
  }

  // RoundStrokeRectGeometryProcessor (used for rounded stroke rects)
  for (auto format : RENDER_TARGET_FORMATS) {
    for (auto sampleCount : SAMPLE_COUNTS) {
      auto gp = RoundStrokeRectGeometryProcessor::Make(allocator, 1024, 1024);
      std::vector<PlacementPtr<FragmentProcessor>> colors;
      colors.push_back(ConstColorProcessor::Make(allocator, Color::White()));
      std::vector<PlacementPtr<FragmentProcessor>> coverages;
      for (auto blendMode : COEFFICIENT_BLEND_MODES) {
        VariantConfig config;
        config.format = format;
        config.sampleCount = sampleCount;
        config.blendMode = blendMode;
        tryEmitVariant(gp->clone(allocator), colors, coverages, nullptr, config, callback);
      }
    }
  }
}

void ShaderVariantEnumerator::enumerateColorFPChains(
    const std::function<void(std::vector<PlacementPtr<FragmentProcessor>>& colors)>& callback) {
  // Template 1: ConstColor (solid color)
  {
    std::vector<PlacementPtr<FragmentProcessor>> colors;
    colors.push_back(ConstColorProcessor::Make(allocator, Color::White()));
    callback(colors);
  }

  // Template 2: TextureEffect (standard texture)
  // TextureEffect key depends on: YUV vs non-YUV, RGBAAA, subset presence.
  // We enumerate the common non-YUV case.
  {
    std::vector<PlacementPtr<FragmentProcessor>> colors;
    auto proxy = /* create mock TextureProxy */ nullptr;
    // In practice, we create a MockTextureProxy that returns a valid Texture for key computation.
    // The TextureEffect key only depends on:
    //   classID + isYUV + isRGBAAA + needSubset + samplerState flags
    // We enumerate all combinations of these booleans.
    // For brevity, showing the non-YUV, non-RGBAAA, no-subset case:
    colors.push_back(TextureEffect::Make(allocator, proxy, SamplingArgs{}));
    callback(colors);
  }

  // Template 3: TiledTextureEffect
  {
    std::vector<PlacementPtr<FragmentProcessor>> colors;
    colors.push_back(TiledTextureEffect::Make(allocator, /* proxy */ nullptr,
                                               TileMode::Repeat, TileMode::Repeat,
                                               SamplingArgs{}));
    callback(colors);
  }

  // Template 4: LinearGradient = Compose(LinearGradientLayout, ClampedGradient(Colorizer))
  // Colorizer variants: SingleInterval, DualInterval, UnrolledBinary, TextureGradient
  {
    auto layout = LinearGradientLayout::Make(allocator, Matrix::I());
    // SingleIntervalGradientColorizer
    auto colorizer = SingleIntervalGradientColorizer::Make(allocator, Color::Black(), Color::White());
    auto clamped = ClampedGradientEffect::Make(allocator, std::move(colorizer),
                                                std::move(layout),
                                                /* premul */ false, /* clampMode */ 0);
    std::vector<PlacementPtr<FragmentProcessor>> colors;
    colors.push_back(std::move(clamped));
    callback(colors);
  }
  {
    auto layout = LinearGradientLayout::Make(allocator, Matrix::I());
    auto colorizer = DualIntervalGradientColorizer::Make(allocator,
        Color::Black(), Color::White(), Color::Red(), Color::Blue(), 0.5f);
    auto clamped = ClampedGradientEffect::Make(allocator, std::move(colorizer),
                                                std::move(layout), false, 0);
    std::vector<PlacementPtr<FragmentProcessor>> colors;
    colors.push_back(std::move(clamped));
    callback(colors);
  }
  {
    auto layout = LinearGradientLayout::Make(allocator, Matrix::I());
    auto colorizer = UnrolledBinaryGradientColorizer::Make(allocator, /* ... colors/thresholds */);
    auto clamped = ClampedGradientEffect::Make(allocator, std::move(colorizer),
                                                std::move(layout), false, 0);
    std::vector<PlacementPtr<FragmentProcessor>> colors;
    colors.push_back(std::move(clamped));
    callback(colors);
  }
  {
    auto layout = LinearGradientLayout::Make(allocator, Matrix::I());
    auto colorizer = TextureGradientColorizer::Make(allocator, /* proxy */ nullptr);
    auto clamped = ClampedGradientEffect::Make(allocator, std::move(colorizer),
                                                std::move(layout), false, 0);
    std::vector<PlacementPtr<FragmentProcessor>> colors;
    colors.push_back(std::move(clamped));
    callback(colors);
  }

  // Template 5-7: Radial/Conic/Diamond gradients (same structure, different layout)
  // Enumerate same 4 colorizer variants for each layout type.
  // (code structure identical to Template 4, replacing LinearGradientLayout with
  //  RadialGradientLayout / ConicGradientLayout / DiamondGradientLayout)

  // Template 8: GaussianBlur1D wrapping TextureEffect
  {
    auto inner = TextureEffect::Make(allocator, /* proxy */ nullptr, SamplingArgs{});
    auto blur = GaussianBlur1DFragmentProcessor::Make(allocator, std::move(inner),
                                                       /* direction */ {1, 0},
                                                       /* bounds */ {0, 1024},
                                                       /* radius */ 10);
    std::vector<PlacementPtr<FragmentProcessor>> colors;
    colors.push_back(std::move(blur));
    callback(colors);
  }

  // Template 9: ColorMatrix = Compose(source FP, ColorMatrixFP)
  {
    auto source = TextureEffect::Make(allocator, /* proxy */ nullptr, SamplingArgs{});
    auto matrix = ColorMatrixFragmentProcessor::Make(allocator, /* identity matrix */);
    auto composed = FragmentProcessor::Compose(allocator, std::move(source), std::move(matrix));
    std::vector<PlacementPtr<FragmentProcessor>> colors;
    colors.push_back(std::move(composed));
    callback(colors);
  }

  // Template 10: Xfermode(src, dst) for blend-mode drawing
  {
    auto src = TextureEffect::Make(allocator, /* proxy */ nullptr, SamplingArgs{});
    auto dst = TextureEffect::Make(allocator, /* proxy */ nullptr, SamplingArgs{});
    auto xfermode = XfermodeFragmentProcessor::Make(allocator, std::move(src), std::move(dst),
                                                     BlendMode::SrcOver);
    std::vector<PlacementPtr<FragmentProcessor>> colors;
    colors.push_back(std::move(xfermode));
    callback(colors);
  }

  // Template 11: Luma filter
  {
    auto source = TextureEffect::Make(allocator, /* proxy */ nullptr, SamplingArgs{});
    auto luma = LumaFragmentProcessor::Make(allocator);
    auto composed = FragmentProcessor::Compose(allocator, std::move(source), std::move(luma));
    std::vector<PlacementPtr<FragmentProcessor>> colors;
    colors.push_back(std::move(composed));
    callback(colors);
  }

  // Template 12: DeviceSpaceTextureEffect (used for dst-readback blend modes)
  {
    auto dsTex = DeviceSpaceTextureEffect::Make(allocator, /* proxy */ nullptr, Matrix::I());
    std::vector<PlacementPtr<FragmentProcessor>> colors;
    colors.push_back(std::move(dsTex));
    callback(colors);
  }
}

void ShaderVariantEnumerator::enumerateCoverageFPs(
    const std::function<void(std::vector<PlacementPtr<FragmentProcessor>>& coverages)>& callback) {
  // No coverage FP
  {
    std::vector<PlacementPtr<FragmentProcessor>> coverages;
    callback(coverages);
  }
  // AARectEffect coverage FP
  {
    std::vector<PlacementPtr<FragmentProcessor>> coverages;
    coverages.push_back(AARectEffect::Make(allocator, Rect::MakeWH(1024, 1024)));
    callback(coverages);
  }
}

void ShaderVariantEnumerator::tryEmitVariant(
    PlacementPtr<GeometryProcessor> gp,
    std::vector<PlacementPtr<FragmentProcessor>>& colors,
    std::vector<PlacementPtr<FragmentProcessor>>& coverages,
    PlacementPtr<XferProcessor> xp,
    const VariantConfig& config,
    const VariantCallback& callback) {

  auto renderTarget = std::make_unique<MockRenderTarget>(
      context, 1024, 1024, config.format, config.sampleCount, config.origin);

  std::vector<FragmentProcessor*> fpPtrs;
  fpPtrs.reserve(colors.size() + coverages.size());
  for (auto& c : colors) fpPtrs.push_back(c.get());
  for (auto& c : coverages) fpPtrs.push_back(c.get());

  ProgramInfo programInfo(renderTarget.get(), gp.get(), std::move(fpPtrs),
                          colors.size(), xp.get(), config.blendMode);
  programInfo.setCullMode(config.cullMode);

  // Compute the ProgramKey.
  BytesKey programKey = {};
  gp->computeProcessorKey(context, &programKey);
  for (auto& color : colors) {
    color->computeProcessorKey(context, &programKey);
  }
  for (auto& coverage : coverages) {
    coverage->computeProcessorKey(context, &programKey);
  }
  if (xp) {
    xp->computeProcessorKey(context, &programKey);
  }
  programKey.write(static_cast<uint32_t>(config.blendMode));
  programKey.write(static_cast<uint32_t>(Swizzle::ForWrite(config.format).asKey()));
  programKey.write(static_cast<uint32_t>(config.cullMode));
  programKey.write(static_cast<uint32_t>(config.format));
  programKey.write(static_cast<uint32_t>(config.sampleCount));

  // Deduplicate.
  if (seenKeys.count(programKey) > 0) {
    return;
  }
  seenKeys[programKey] = true;

  // Generate GLSL via ProgramBuilder.
  // ProgramBuilder::CreateProgram will call emitAndInstallProcessors() and finalize().
  // Our MockGPU::createShaderModule() captures the GLSL code.
  auto program = ProgramBuilder::CreateProgram(context, &programInfo);
  if (program == nullptr) {
    return;
  }

  // Extract GLSL from MockShaderModule.
  auto pipeline = std::static_pointer_cast<MockRenderPipeline>(program->getPipeline());
  auto vertexModule = std::static_pointer_cast<MockShaderModule>(
      pipeline->descriptor.vertex.module);
  auto fragmentModule = std::static_pointer_cast<MockShaderModule>(
      pipeline->descriptor.fragment.module);

  callback(programInfo, programKey, vertexModule->code, fragmentModule->code);
}

}  // namespace tgfx
```

### 3.5 ProgramKey 去重

去重使用 `BytesKeyMap<bool> seenKeys`（基于 `BytesKeyHasher`，与 `GlobalCache` 使用相同的哈希函数），确保每个唯一 ProgramKey 只输出一次。由于 ProgramKey 的计算逻辑与运行时完全一致（使用相同的 `computeProcessorKey()` 方法），离线生成的映射与运行时查找的 key 保证匹配。

### 3.6 输出文件格式

**JSON 变体清单** (`tgfx_shader_variants.json`)：

```json
{
  "version": 1,
  "generatorVersion": "1.0.0",
  "variants": [
    {
      "variantID": 0,
      "drawOpType": "RectDrawOp",
      "gpName": "QuadPerEdgeAAGeometryProcessor",
      "fpChain": ["ConstColorProcessor"],
      "xpName": "EmptyXferProcessor",
      "blendMode": "SrcOver",
      "format": "RGBA_8888",
      "sampleCount": 1,
      "cullMode": "None",
      "programKeyHex": "0a1b2c3d...",
      "vertexFile": "Generated/TgfxVariant_0000_VS.ush",
      "fragmentFile": "Generated/TgfxVariant_0000_PS.ush"
    }
  ],
  "totalVariants": 450,
  "drawOpVariantCounts": {
    "RectDrawOp": 180,
    "RRectDrawOp": 60,
    "ShapeDrawOp": 50,
    "AtlasTextOp": 20,
    "Quads3DDrawOp": 30,
    "MeshDrawOp": 40,
    "HairlineLineOp": 20,
    "HairlineQuadOp": 20,
    "ShapeInstancedDrawOp": 30
  }
}
```

**C++ Registry Header** (`TgfxShaderVariantRegistry.generated.h`)：

```cpp
// Auto-generated by TgfxShaderVariantGenerator. Do not edit.
// Copyright (C) 2026 Tencent. All rights reserved.

#pragma once

#include <cstdint>

namespace tgfx {

struct ShaderVariantEntry {
  const uint32_t* programKeyData;
  uint32_t programKeySize;
  uint32_t variantID;
  uint8_t drawOpType;  // DrawOp::Type enum value
};

static const uint32_t ProgramKey_0000[] = {0x0a1b2c3d, 0x4e5f6a7b, /* ... */};
static const uint32_t ProgramKey_0001[] = {0x1a2b3c4d, 0x5e6f7a8b, /* ... */};
// ... one array per variant

static const ShaderVariantEntry SHADER_VARIANT_TABLE[] = {
    {ProgramKey_0000, 12, 0, 0},  // RectDrawOp, variant 0
    {ProgramKey_0001, 14, 1, 0},  // RectDrawOp, variant 1
    // ...
};

static const uint32_t SHADER_VARIANT_COUNT = 450;

// Maximum variant ID per DrawOp type, used to set Permutation range.
static const uint32_t MAX_VARIANT_ID_PER_DRAW_OP[] = {
    180,  // RectDrawOp
    60,   // RRectDrawOp
    50,   // ShapeDrawOp
    20,   // AtlasTextOp
    30,   // Quads3DDrawOp
    40,   // MeshDrawOp
    20,   // HairlineLineOp
    20,   // HairlineQuadOp
    30,   // ShapeInstancedDrawOp
};

}  // namespace tgfx
```

---

## 4. GLSL -> HLSL 转换管线

### 4.1 复用 preprocessGLSL()

离线工具直接复用 `src/gpu/metal/MetalShaderModule.mm` 中的 `preprocessGLSL()` 函数。该函数将 tgfx 生成的 OpenGL 风格 GLSL 转换为 Vulkan 兼容的 GLSL 450：

1. `upgradeGLSLVersion()` — `#version xxx` -> `#version 450`
2. `assignInternalUBOBindings()` — `VertexUniformBlock` -> binding=0, `FragmentUniformBlock` -> binding=1
3. `assignCustomUBOBindings()` — 其他 UBO 按顺序分配 binding
4. `assignSamplerBindings()` — sampler uniform 按顺序分配 binding（从 0 开始）
5. `assignInputLocationQualifiers()` — in 变量添加 location
6. `assignOutputLocationQualifiers()` — out 变量添加 location
7. `removePrecisionDeclarations()` — 移除 precision 声明

为了在离线工具中复用这些函数而不依赖 Metal/ObjC，需要将它们提取到一个纯 C++ 文件中：

**文件**：`src/gpu/ue/tools/GLSLPreprocessor.cpp`

```cpp
// Copyright (C) 2026 Tencent. All rights reserved.

// This file extracts the pure C++ GLSL preprocessing functions from
// MetalShaderModule.mm for use in the offline shader variant generator.
// The functions are identical to those in MetalShaderModule.mm.

#include "GLSLPreprocessor.h"
#include <regex>

namespace tgfx {

using MatchReplacer = std::string (*)(const std::smatch&, int&);

static std::string replaceAllMatches(const std::string& source, const std::regex& pattern,
                                     MatchReplacer replacer, int& counter) {
  std::smatch match;
  std::string::const_iterator searchStart(source.cbegin());
  std::string result;
  size_t lastPos = 0;
  while (std::regex_search(searchStart, source.cend(), match, pattern)) {
    auto matchPos = static_cast<size_t>(match.position(0));
    auto iterOffset = static_cast<size_t>(searchStart - source.cbegin());
    size_t matchStart = matchPos + iterOffset;
    result += source.substr(lastPos, matchStart - lastPos);
    result += replacer(match, counter);
    lastPos = matchStart + static_cast<size_t>(match.length(0));
    searchStart = match.suffix().first;
  }
  result += source.substr(lastPos);
  return result;
}

static std::string upgradeGLSLVersion(const std::string& source) {
  static std::regex versionRegex(R"(#version\s+\d+(\s+es)?)");
  return std::regex_replace(source, versionRegex, "#version 450");
}

static std::string assignInternalUBOBindings(const std::string& source) {
  static std::regex vertexUboRegex(R"(layout\s*\(\s*std140\s*\)\s*uniform\s+VertexUniformBlock)");
  auto result = std::regex_replace(source, vertexUboRegex,
                                   "layout(std140, binding=0) uniform VertexUniformBlock");
  static std::regex fragmentUboRegex(
      R"(layout\s*\(\s*std140\s*\)\s*uniform\s+FragmentUniformBlock)");
  return std::regex_replace(result, fragmentUboRegex,
                            "layout(std140, binding=1) uniform FragmentUniformBlock");
}

static std::string replaceCustomUBO(const std::smatch& match, int& counter) {
  return "layout(std140, binding=" + std::to_string(counter++) + ") uniform " + match[1].str();
}

static std::string assignCustomUBOBindings(const std::string& source) {
  static std::regex uboRegex(R"(layout\s*\(\s*std140\s*\)\s*uniform\s+(\w+))");
  int binding = 0;
  return replaceAllMatches(source, uboRegex, replaceCustomUBO, binding);
}

static std::string replaceSamplerBinding(const std::smatch& match, int& counter) {
  return "layout(binding=" + std::to_string(counter++) + ") uniform " + match[1].str() + " " +
         match[2].str() + ";";
}

static std::string assignSamplerBindings(const std::string& source) {
  static std::regex samplerRegex(R"(uniform\s+(sampler\w+)\s+(\w+);)");
  int binding = 0;
  return replaceAllMatches(source, samplerRegex, replaceSamplerBinding, binding);
}

static std::string replaceInputLocation(const std::smatch& match, int& counter) {
  std::string interpStr = match[1].matched ? match[1].str() : "";
  std::string precisionStr = match[2].matched ? match[2].str() : "";
  return "layout(location=" + std::to_string(counter++) + ") " + interpStr + "in " + precisionStr +
         match[3].str() + " " + match[4].str() + ";";
}

static std::string assignInputLocationQualifiers(const std::string& source) {
  static std::regex inVarRegex(
      R"((flat\s+|noperspective\s+)?in\s+(highp\s+|mediump\s+|lowp\s+)?(\w+)\s+(\w+)\s*;)");
  int location = 0;
  return replaceAllMatches(source, inVarRegex, replaceInputLocation, location);
}

static std::string replaceOutputLocation(const std::smatch& match, int& counter) {
  std::string interpStr = match[1].matched ? match[1].str() : "";
  std::string precisionStr = match[2].matched ? match[2].str() : "";
  return "layout(location=" + std::to_string(counter++) + ") " + interpStr + "out " + precisionStr +
         match[3].str() + " " + match[4].str() + ";";
}

static std::string assignOutputLocationQualifiers(const std::string& source) {
  static std::regex outVarRegex(
      R"((flat\s+|noperspective\s+)?out\s+(highp\s+|mediump\s+|lowp\s+)?(\w+)\s+(\w+)\s*;)");
  int location = 0;
  return replaceAllMatches(source, outVarRegex, replaceOutputLocation, location);
}

static std::string removePrecisionDeclarations(const std::string& source) {
  static std::regex precisionDeclRegex(R"(precision\s+(highp|mediump|lowp)\s+\w+\s*;)");
  return std::regex_replace(source, precisionDeclRegex, "");
}

std::string preprocessGLSL(const std::string& glslCode) {
  auto result = upgradeGLSLVersion(glslCode);
  result = assignInternalUBOBindings(result);
  result = assignCustomUBOBindings(result);
  result = assignSamplerBindings(result);
  result = assignInputLocationQualifiers(result);
  result = assignOutputLocationQualifiers(result);
  result = removePrecisionDeclarations(result);
  return result;
}

}  // namespace tgfx
```

### 4.2 SPIRV-Cross HLSL 配置

```cpp
// src/gpu/ue/tools/SPIRVToHLSL.cpp
// Copyright (C) 2026 Tencent. All rights reserved.

#include "SPIRVToHLSL.h"
#include <shaderc/shaderc.hpp>
#include <spirv_hlsl.hpp>
#include <spirv_parser.hpp>
#include "GLSLPreprocessor.h"

namespace tgfx {

static std::vector<uint32_t> compileGLSLToSPIRV(const std::string& glslCode, ShaderStage stage) {
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);

  shaderc_shader_kind shaderKind =
      (stage == ShaderStage::Vertex) ? shaderc_vertex_shader : shaderc_fragment_shader;

  shaderc::SpvCompilationResult spvResult =
      compiler.CompileGlslToSpv(glslCode, shaderKind, "shader", "main", options);

  if (spvResult.GetCompilationStatus() != shaderc_compilation_status_success) {
    fprintf(stderr, "GLSL to SPIR-V compilation error: %s\n",
            spvResult.GetErrorMessage().c_str());
    fprintf(stderr, "GLSL:\n%s\n", glslCode.c_str());
    return {};
  }

  return {spvResult.cbegin(), spvResult.cend()};
}

static std::string convertSPIRVToHLSL(const std::vector<uint32_t>& spirvBinary,
                                       ShaderStage stage) {
  spirv_cross::Parser spvParser(spirvBinary.data(), spirvBinary.size());
  spvParser.parse();

  spirv_cross::CompilerHLSL hlslCompiler(std::move(spvParser.get_parsed_ir()));

  spirv_cross::CompilerHLSL::Options hlslOptions;
  // Shader Model 5.0 for broadest UE compatibility (D3D11 + D3D12).
  hlslOptions.shader_model = 50;
  // Use row-major matrices to match tgfx's column-major GLSL with transpose.
  // Actually tgfx uses column-major in GLSL; SPIRV-Cross handles the transpose
  // when converting to HLSL row-major automatically.
  hlslOptions.point_size_compat = false;
  hlslOptions.point_coord_compat = false;
  hlslCompiler.set_hlsl_options(hlslOptions);

  auto commonOptions = hlslCompiler.get_common_options();
  // Flip Y in vertex shader to match UE's coordinate system.
  // UE handles this via its own projection matrix, so we do NOT flip here.
  commonOptions.vertex.flip_vert_y = false;
  hlslCompiler.set_common_options(commonOptions);

  // Map UBO bindings to HLSL cbuffer register slots.
  auto uboResources = hlslCompiler.get_shader_resources().uniform_buffers;
  for (auto& ubo : uboResources) {
    uint32_t spvBinding = hlslCompiler.get_decoration(ubo.id, spv::DecorationBinding);
    hlslCompiler.set_decoration(ubo.id, spv::DecorationBinding, spvBinding);
  }

  // Map sampled images to HLSL texture/sampler register slots.
  auto sampledImages = hlslCompiler.get_shader_resources().sampled_images;
  for (auto& image : sampledImages) {
    uint32_t spvBinding = hlslCompiler.get_decoration(image.id, spv::DecorationBinding);
    hlslCompiler.set_decoration(image.id, spv::DecorationBinding, spvBinding);
  }

  std::string hlslCode = hlslCompiler.compile();
  if (hlslCode.empty()) {
    fprintf(stderr, "SPIR-V to HLSL conversion failed\n");
    return "";
  }

  return hlslCode;
}

GLSLToHLSLResult convertGLSLToHLSL(const std::string& rawGLSL, ShaderStage stage) {
  GLSLToHLSLResult result = {};

  // Step 1: Preprocess GLSL (add bindings, locations, upgrade version).
  std::string vulkanGLSL = preprocessGLSL(rawGLSL);

  // Step 2: Compile to SPIRV.
  auto spirvBinary = compileGLSLToSPIRV(vulkanGLSL, stage);
  if (spirvBinary.empty()) {
    result.error = "SPIRV compilation failed";
    return result;
  }

  // Step 3: Convert SPIRV to HLSL.
  result.hlsl = convertSPIRVToHLSL(spirvBinary, stage);
  if (result.hlsl.empty()) {
    result.error = "HLSL conversion failed";
    return result;
  }

  // Step 4: Post-process for UE compatibility.
  result.hlsl = postProcessHLSL(result.hlsl, stage);
  result.success = true;
  return result;
}

}  // namespace tgfx
```

### 4.3 后处理：UE .ush 格式适配

SPIRV-Cross 输出的 HLSL 需要以下后处理才能嵌入 UE 的 `.ush` 文件：

```cpp
// src/gpu/ue/tools/SPIRVToHLSL.cpp (continued)

std::string postProcessHLSL(const std::string& hlsl, ShaderStage stage) {
  std::string result = hlsl;

  // 1. Remove SPIRV-Cross generated cbuffer declarations.
  //    UE manages cbuffers through its own parameter system.
  //    We replace cbuffer blocks with #include to TgfxUniformBridge.ush.
  //    Actually, we keep the cbuffer declarations as they are compatible with
  //    UE's manual uniform buffer binding via FRHICommandList.

  // 2. Replace SPIRV-Cross entry point names.
  //    SPIRV-Cross generates "vert_main" and "frag_main" by default.
  //    UE expects specific entry points defined in .usf files.
  if (stage == ShaderStage::Vertex) {
    // Replace the main vertex function signature.
    // SPIRV-Cross generates: void vert_main()
    // We need to wrap it properly for UE.
    // No change needed: the .usf entry point will call this function.
  }

  // 3. Add UE-specific semantic qualifiers.
  //    SPIRV-Cross already generates proper HLSL semantics
  //    (TEXCOORD0, SV_Position, SV_Target0, etc.).

  // 4. Remove any #version or #extension directives that SPIRV-Cross may emit.
  std::regex versionDirective(R"(#version\s+\d+.*)");
  result = std::regex_replace(result, versionDirective, "");

  // 5. Replace combined texture-sampler declarations with separate texture/sampler
  //    for UE RHI compatibility. SPIRV-Cross with SM 5.0 already does this.

  // 6. Wrap in #ifndef guard based on variant ID.
  // This will be done by the file writer, not here.

  return result;
}
```

### 4.4 已知 SPIRV-Cross HLSL 输出问题和 Workaround

| 问题 | 描述 | Workaround |
|------|------|------------|
| `gl_FragCoord` 翻转 | SPIRV-Cross 可能不正确处理 Y 轴翻转 | 在后处理中检测 `gl_FragCoord` 使用，添加 UE 的 `View.ViewSizeAndInvSize.y - gl_FragCoord.y` 翻转。tgfx 目前不在 Fragment Shader 中使用 `gl_FragCoord`，因此暂不需要处理。 |
| `mat3x3` padding | std140 中 mat3 每列 pad 到 vec4，SPIRV-Cross HLSL 输出可能使用 `float3x3` 但 cbuffer 布局不含 padding | tgfx 的 `UniformData::setData<Matrix>` 已经 pad 到 3xfloat4。SPIRV-Cross 在 HLSL 中生成 `column_major float3x4`（注意是 3x4，不是 3x3），匹配 std140 布局。 |
| Sampler/Texture 分离 | SM 5.0 要求分离 Texture 和 SamplerState | SPIRV-Cross 在 SM 5.0 模式下已自动分离。输出形如 `Texture2D<float4> _Texture : register(t0); SamplerState _Sampler : register(s0);` |
| `discard` vs `clip` | GLSL `discard` -> HLSL `discard`（SM 5.0 支持） | 无需处理 |
| `subpassInput` | tgfx 的 framebuffer fetch 在 Metal 后端使用 `subpassInput` | UE 后端不使用 framebuffer fetch，ShaderCaps 设置 `frameBufferFetchSupport = false` |

---

## 5. UE Shader 文件组织

### 5.1 目录结构

```
TgfxPlugin/
  Shaders/
    Private/
      Tgfx/
        TgfxCommon.ush                          // Common utilities and type mappings
        TgfxUniformBridge.ush                    // Uniform buffer macros
        TgfxRectDrawOp.usf                       // Entry point for RectDrawOp VS/PS
        TgfxRRectDrawOp.usf                      // Entry point for RRectDrawOp VS/PS
        TgfxShapeDrawOp.usf                      // Entry point for ShapeDrawOp VS/PS
        TgfxAtlasTextOp.usf                      // Entry point for AtlasTextOp VS/PS
        TgfxQuads3DDrawOp.usf                    // Entry point for Quads3DDrawOp VS/PS
        TgfxMeshDrawOp.usf                       // Entry point for MeshDrawOp VS/PS
        TgfxHairlineLineOp.usf                   // Entry point for HairlineLineOp VS/PS
        TgfxHairlineQuadOp.usf                   // Entry point for HairlineQuadOp VS/PS
        TgfxShapeInstancedDrawOp.usf             // Entry point for ShapeInstancedDrawOp VS/PS
        Generated/
          TgfxVariant_0000_VS.ush                // Generated vertex shader HLSL
          TgfxVariant_0000_PS.ush                // Generated pixel shader HLSL
          TgfxVariant_0001_VS.ush
          TgfxVariant_0001_PS.ush
          ...
```

### 5.2 .usf 入口文件模板

每种 DrawOp 类型有一个 `.usf` 文件作为 UE Shader 编译入口。所有变体通过 `TGFX_VARIANT_ID` Permutation 维度选择不同的 `#include`。

```hlsl
// TgfxRectDrawOp.usf
// Copyright (C) 2026 Tencent. All rights reserved.
// Auto-generated entry point for RectDrawOp shaders.

#include "TgfxCommon.ush"

// Include the generated variant based on TGFX_VARIANT_ID permutation.
// The preprocessor converts TGFX_VARIANT_ID to the correct file path.

// We use a lookup table approach: each variant ID maps to a #include.
// UE's shader compiler evaluates only the active permutation branch.

#if TGFX_VARIANT_ID == 0
#include "Generated/TgfxVariant_0000_VS.ush"
#include "Generated/TgfxVariant_0000_PS.ush"
#elif TGFX_VARIANT_ID == 1
#include "Generated/TgfxVariant_0001_VS.ush"
#include "Generated/TgfxVariant_0001_PS.ush"
// ... (auto-generated for all variants belonging to this DrawOp type)
#else
// Fallback: output solid magenta for debugging.
void MainVS(
    in float4 InPosition : ATTRIBUTE0,
    out float4 OutPosition : SV_Position)
{
    OutPosition = float4(InPosition.xy, 0.0, 1.0);
}

void MainPS(out float4 OutColor : SV_Target0)
{
    OutColor = float4(1.0, 0.0, 1.0, 1.0);
}
#endif
```

注意：上面的 `#if/#elif` 链在变体很多时会很长。实际实现中，离线工具会自动生成这个文件。也可以采用宏拼接方案：

```hlsl
// Alternative: macro-based include (avoids long if-elif chains).
// TgfxRectDrawOp.usf

#include "TgfxCommon.ush"

#define TGFX_VARIANT_FILE_VS(id) TGFX_STRINGIFY(Generated/TgfxRectDrawOp_ ## id ## _VS.ush)
#define TGFX_VARIANT_FILE_PS(id) TGFX_STRINGIFY(Generated/TgfxRectDrawOp_ ## id ## _PS.ush)

// UE shader compiler does NOT support token pasting in #include paths.
// Therefore, we must use the #if/#elif approach.
// The offline tool generates the complete .usf file with all branches.
```

由于 UE 的 Shader Preprocessor 不支持在 `#include` 路径中做宏拼接，我们必须使用 `#if/#elif` 链。离线工具自动生成完整的 `.usf` 文件，无需手写。

### 5.3 .ush Generated 文件模板

每个生成的 `.ush` 文件包含一个变体的完整 VS 或 PS 代码：

```hlsl
// Generated/TgfxVariant_0000_VS.ush
// Auto-generated by TgfxShaderVariantGenerator. Do not edit.
// DrawOp: RectDrawOp, VariantID: 0
// GP: QuadPerEdgeAAGeometryProcessor, FP: [ConstColorProcessor], XP: EmptyXferProcessor

// cbuffer declarations (from SPIRV-Cross)
cbuffer VertexUniformBlock : register(b0)
{
    float4 _RTAdjust;
    // ... other uniforms for this variant
};

// Vertex inputs (semantics match UE vertex declaration)
struct VSInput
{
    float3 Position : ATTRIBUTE0;
    float4 Coverage : ATTRIBUTE1;
    // ... varies per GP
};

struct VSOutput
{
    float4 Position : SV_Position;
    float4 Varying0 : TEXCOORD0;
    // ... varies per variant
};

VSOutput MainVS(VSInput input)
{
    VSOutput output = (VSOutput)0;
    // ... SPIRV-Cross generated HLSL code ...
    return output;
}
```

```hlsl
// Generated/TgfxVariant_0000_PS.ush
// Auto-generated by TgfxShaderVariantGenerator. Do not edit.
// DrawOp: RectDrawOp, VariantID: 0

cbuffer FragmentUniformBlock : register(b1)
{
    float4 _ConstColor_P1;
    // ... other uniforms
};

// Textures and samplers (if any)
// Texture2D _TextureSampler_0 : register(t0);
// SamplerState _TextureSampler_0_sampler : register(s0);

struct PSInput
{
    float4 Position : SV_Position;
    float4 Varying0 : TEXCOORD0;
};

float4 MainPS(PSInput input) : SV_Target0
{
    // ... SPIRV-Cross generated HLSL code ...
    return outputColor;
}
```

### 5.4 TgfxCommon.ush

```hlsl
// TgfxCommon.ush
// Copyright (C) 2026 Tencent. All rights reserved.
// Common utilities for tgfx UE shaders.

#pragma once

#include "/Engine/Public/Platform.ush"
#include "/Engine/Private/Common.ush"

// Helper macros for variant include path generation.
#define TGFX_STRINGIFY_IMPL(x) #x
#define TGFX_STRINGIFY(x) TGFX_STRINGIFY_IMPL(x)

// Coordinate system conversion.
// tgfx uses top-left origin with Y-down.
// UE clip space: [-1,1] with platform-dependent Y direction.
// The RTAdjust uniform handles this conversion in the vertex shader.

// Texture coordinate helpers.
// tgfx textures use top-left origin (0,0) to bottom-right (1,1).
// This matches UE's default UV convention.
```

### 5.5 TgfxUniformBridge.ush

```hlsl
// TgfxUniformBridge.ush
// Copyright (C) 2026 Tencent. All rights reserved.
// Macros for bridging tgfx uniform buffer layout to UE RHI.

#pragma once

// tgfx uses two UBOs with std140 layout:
//   VertexUniformBlock (binding=0) - visible to VS only
//   FragmentUniformBlock (binding=1) - visible to PS only
//   Textures start at binding=2
//
// In UE RHI, we bind these as raw cbuffers:
//   b0 = VertexUniformBlock
//   b1 = FragmentUniformBlock
//   t0..tN = Textures
//   s0..sN = Samplers
//
// SPIRV-Cross HLSL output already uses these register assignments.
// No additional bridging is needed for the shader code itself.
// The C++ side (UERHIRenderPass) ensures correct binding.

// std140 layout rules (for reference):
// - scalars: 4-byte aligned, 4-byte size
// - vec2: 8-byte aligned, 8-byte size
// - vec3: 16-byte aligned, 12-byte size (padded to 16)
// - vec4: 16-byte aligned, 16-byte size
// - mat3: 3 columns of vec4 (each 16-byte aligned), 48-byte total
// - mat4: 4 columns of vec4, 64-byte total
// - arrays: each element rounded up to vec4 alignment
//
// HLSL cbuffers with the same layout are compatible when using
// packoffset or when the compiler uses the same packing rules.
// SPIRV-Cross ensures the HLSL cbuffer layout matches std140.
```

---

## 6. UE Global Shader 集成

### 6.1 FShader 子类声明

每种 DrawOp 类型有一对 VS/PS Global Shader 类。

```cpp
// Source/TgfxPlugin/Private/Shaders/TgfxShaders.h
// Copyright (C) 2026 Tencent. All rights reserved.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameterMacros.h"
#include "ShaderParameterStruct.h"

// Permutation dimension: TGFX_VARIANT_ID
class FTgfxVariantIDDimension : SHADER_PERMUTATION_RANGE_INT("TGFX_VARIANT_ID", 0, 1024);

// ===== RectDrawOp Shaders =====

class FTgfxRectDrawOpVS : public FGlobalShader {
 public:
  DECLARE_GLOBAL_SHADER(FTgfxRectDrawOpVS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxRectDrawOpVS, FGlobalShader);

  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;

  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
                                           FShaderCompilerEnvironment& OutEnvironment);

  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
  END_SHADER_PARAMETER_STRUCT()
};

class FTgfxRectDrawOpPS : public FGlobalShader {
 public:
  DECLARE_GLOBAL_SHADER(FTgfxRectDrawOpPS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxRectDrawOpPS, FGlobalShader);

  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;

  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
                                           FShaderCompilerEnvironment& OutEnvironment);

  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
  END_SHADER_PARAMETER_STRUCT()
};

// ===== RRectDrawOp Shaders =====
class FTgfxRRectDrawOpVS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxRRectDrawOpVS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxRRectDrawOpVS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};

class FTgfxRRectDrawOpPS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxRRectDrawOpPS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxRRectDrawOpPS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};

// (Repeat for ShapeDrawOp, AtlasTextOp, Quads3DDrawOp, MeshDrawOp,
//  HairlineLineOp, HairlineQuadOp, ShapeInstancedDrawOp)
// Each pair follows the identical pattern above.

// ===== ShapeDrawOp =====
class FTgfxShapeDrawOpVS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxShapeDrawOpVS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxShapeDrawOpVS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};
class FTgfxShapeDrawOpPS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxShapeDrawOpPS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxShapeDrawOpPS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};

// ===== AtlasTextOp =====
class FTgfxAtlasTextOpVS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxAtlasTextOpVS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxAtlasTextOpVS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};
class FTgfxAtlasTextOpPS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxAtlasTextOpPS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxAtlasTextOpPS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};

// ===== Quads3DDrawOp =====
class FTgfxQuads3DDrawOpVS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxQuads3DDrawOpVS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxQuads3DDrawOpVS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};
class FTgfxQuads3DDrawOpPS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxQuads3DDrawOpPS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxQuads3DDrawOpPS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};

// ===== MeshDrawOp =====
class FTgfxMeshDrawOpVS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxMeshDrawOpVS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxMeshDrawOpVS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};
class FTgfxMeshDrawOpPS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxMeshDrawOpPS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxMeshDrawOpPS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};

// ===== HairlineLineOp =====
class FTgfxHairlineLineOpVS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxHairlineLineOpVS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxHairlineLineOpVS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};
class FTgfxHairlineLineOpPS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxHairlineLineOpPS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxHairlineLineOpPS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};

// ===== HairlineQuadOp =====
class FTgfxHairlineQuadOpVS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxHairlineQuadOpVS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxHairlineQuadOpVS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};
class FTgfxHairlineQuadOpPS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxHairlineQuadOpPS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxHairlineQuadOpPS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};

// ===== ShapeInstancedDrawOp =====
class FTgfxShapeInstancedDrawOpVS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxShapeInstancedDrawOpVS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxShapeInstancedDrawOpVS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};
class FTgfxShapeInstancedDrawOpPS : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTgfxShapeInstancedDrawOpPS);
  SHADER_USE_PARAMETER_STRUCT(FTgfxShapeInstancedDrawOpPS, FGlobalShader);
  using FPermutationDomain = TShaderPermutationDomain<FTgfxVariantIDDimension>;
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, ) END_SHADER_PARAMETER_STRUCT()
};
```

### 6.2 Permutation 维度设计

使用单一 `TGFX_VARIANT_ID` 整数维度，范围 `[0, MAX_VARIANT_ID_PER_DRAW_OP[type])`:

```cpp
// SHADER_PERMUTATION_RANGE_INT 要求编译时已知最大值。
// 我们使用一个保守上限 1024，通过 ShouldCompilePermutation 裁剪无效的。
class FTgfxVariantIDDimension : SHADER_PERMUTATION_RANGE_INT("TGFX_VARIANT_ID", 0, 1024);
```

### 6.3 ShouldCompilePermutation() 实现

```cpp
// Source/TgfxPlugin/Private/Shaders/TgfxShaders.cpp
// Copyright (C) 2026 Tencent. All rights reserved.

#include "TgfxShaders.h"
#include "TgfxShaderVariantRegistry.generated.h"

// Helper: check if a variant ID is valid for a given DrawOp type.
static bool IsValidVariantID(uint8_t drawOpType, int32_t variantID) {
  if (drawOpType >= sizeof(MAX_VARIANT_ID_PER_DRAW_OP) / sizeof(MAX_VARIANT_ID_PER_DRAW_OP[0])) {
    return false;
  }
  return variantID >= 0 &&
         static_cast<uint32_t>(variantID) < MAX_VARIANT_ID_PER_DRAW_OP[drawOpType];
}

bool FTgfxRectDrawOpVS::ShouldCompilePermutation(
    const FGlobalShaderPermutationParameters& Parameters) {
  FPermutationDomain PermutationVector(Parameters.PermutationId);
  int32_t VariantID = PermutationVector.Get<FTgfxVariantIDDimension>();
  return IsValidVariantID(0 /* DrawOp::Type::RectDrawOp */, VariantID);
}

bool FTgfxRectDrawOpPS::ShouldCompilePermutation(
    const FGlobalShaderPermutationParameters& Parameters) {
  FPermutationDomain PermutationVector(Parameters.PermutationId);
  int32_t VariantID = PermutationVector.Get<FTgfxVariantIDDimension>();
  return IsValidVariantID(0 /* DrawOp::Type::RectDrawOp */, VariantID);
}

// ... Same pattern for all other DrawOp types, with the correct type index.

// IMPLEMENT_GLOBAL_SHADER registrations
IMPLEMENT_GLOBAL_SHADER(FTgfxRectDrawOpVS, "/Plugin/TgfxPlugin/Private/Tgfx/TgfxRectDrawOp.usf",
                        "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FTgfxRectDrawOpPS, "/Plugin/TgfxPlugin/Private/Tgfx/TgfxRectDrawOp.usf",
                        "MainPS", SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FTgfxRRectDrawOpVS, "/Plugin/TgfxPlugin/Private/Tgfx/TgfxRRectDrawOp.usf",
                        "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FTgfxRRectDrawOpPS, "/Plugin/TgfxPlugin/Private/Tgfx/TgfxRRectDrawOp.usf",
                        "MainPS", SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FTgfxShapeDrawOpVS, "/Plugin/TgfxPlugin/Private/Tgfx/TgfxShapeDrawOp.usf",
                        "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FTgfxShapeDrawOpPS, "/Plugin/TgfxPlugin/Private/Tgfx/TgfxShapeDrawOp.usf",
                        "MainPS", SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FTgfxAtlasTextOpVS, "/Plugin/TgfxPlugin/Private/Tgfx/TgfxAtlasTextOp.usf",
                        "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FTgfxAtlasTextOpPS, "/Plugin/TgfxPlugin/Private/Tgfx/TgfxAtlasTextOp.usf",
                        "MainPS", SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FTgfxQuads3DDrawOpVS,
                        "/Plugin/TgfxPlugin/Private/Tgfx/TgfxQuads3DDrawOp.usf",
                        "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FTgfxQuads3DDrawOpPS,
                        "/Plugin/TgfxPlugin/Private/Tgfx/TgfxQuads3DDrawOp.usf",
                        "MainPS", SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FTgfxMeshDrawOpVS, "/Plugin/TgfxPlugin/Private/Tgfx/TgfxMeshDrawOp.usf",
                        "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FTgfxMeshDrawOpPS, "/Plugin/TgfxPlugin/Private/Tgfx/TgfxMeshDrawOp.usf",
                        "MainPS", SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FTgfxHairlineLineOpVS,
                        "/Plugin/TgfxPlugin/Private/Tgfx/TgfxHairlineLineOp.usf",
                        "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FTgfxHairlineLineOpPS,
                        "/Plugin/TgfxPlugin/Private/Tgfx/TgfxHairlineLineOp.usf",
                        "MainPS", SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FTgfxHairlineQuadOpVS,
                        "/Plugin/TgfxPlugin/Private/Tgfx/TgfxHairlineQuadOp.usf",
                        "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FTgfxHairlineQuadOpPS,
                        "/Plugin/TgfxPlugin/Private/Tgfx/TgfxHairlineQuadOp.usf",
                        "MainPS", SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FTgfxShapeInstancedDrawOpVS,
                        "/Plugin/TgfxPlugin/Private/Tgfx/TgfxShapeInstancedDrawOp.usf",
                        "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FTgfxShapeInstancedDrawOpPS,
                        "/Plugin/TgfxPlugin/Private/Tgfx/TgfxShapeInstancedDrawOp.usf",
                        "MainPS", SF_Pixel);
```

### 6.4 Shader 目录映射

在插件模块的 `StartupModule()` 中注册 Shader 源码目录：

```cpp
// Source/TgfxPlugin/Private/TgfxPluginModule.cpp
// Copyright (C) 2026 Tencent. All rights reserved.

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

void FTgfxPluginModule::StartupModule() {
  FString PluginShaderDir = FPaths::Combine(
      IPluginManager::Get().FindPlugin("TgfxPlugin")->GetBaseDir(),
      TEXT("Shaders"));
  AddShaderSourceDirectoryMapping(TEXT("/Plugin/TgfxPlugin"), PluginShaderDir);
}
```

---

## 7. ShaderVariantRegistry

### 7.1 数据结构设计

`ShaderVariantRegistry` 是运行时的核心查找结构，将 tgfx 的 `BytesKey`（ProgramKey）映射到 `{drawOpType, variantID}` 对。

```cpp
// src/gpu/ue/ShaderVariantRegistry.h
// Copyright (C) 2026 Tencent. All rights reserved.

#pragma once

#include <mutex>
#include "tgfx/core/BytesKey.h"
#include "src/gpu/ops/DrawOp.h"

namespace tgfx {

struct ShaderVariantInfo {
  DrawOp::Type drawOpType;
  uint32_t variantID;
};

class ShaderVariantRegistry {
 public:
  static ShaderVariantRegistry* Instance();

  // Look up a variant by ProgramKey. Returns true if found.
  bool findVariant(const BytesKey& programKey, ShaderVariantInfo* outInfo) const;

  // Returns the total number of registered variants.
  uint32_t variantCount() const;

 private:
  ShaderVariantRegistry();

  void loadFromGeneratedTable();

  BytesKeyMap<ShaderVariantInfo> registry;
  mutable std::mutex mutex;
};

}  // namespace tgfx
```

### 7.2 构建时生成

离线工具生成 `TgfxShaderVariantRegistry.generated.h`，其中包含静态数组 `SHADER_VARIANT_TABLE[]`。`ShaderVariantRegistry` 在构造时遍历此表，将每个条目的 ProgramKey 和 VariantInfo 插入哈希表。

```cpp
// src/gpu/ue/ShaderVariantRegistry.cpp
// Copyright (C) 2026 Tencent. All rights reserved.

#include "ShaderVariantRegistry.h"
#include "TgfxShaderVariantRegistry.generated.h"

namespace tgfx {

ShaderVariantRegistry* ShaderVariantRegistry::Instance() {
  static ShaderVariantRegistry instance;
  return &instance;
}

ShaderVariantRegistry::ShaderVariantRegistry() {
  loadFromGeneratedTable();
}

void ShaderVariantRegistry::loadFromGeneratedTable() {
  registry.reserve(SHADER_VARIANT_COUNT);
  for (uint32_t i = 0; i < SHADER_VARIANT_COUNT; ++i) {
    const auto& entry = SHADER_VARIANT_TABLE[i];
    BytesKey key;
    key.reserve(entry.programKeySize);
    for (uint32_t j = 0; j < entry.programKeySize; ++j) {
      key.write(entry.programKeyData[j]);
    }
    ShaderVariantInfo info;
    info.drawOpType = static_cast<DrawOp::Type>(entry.drawOpType);
    info.variantID = entry.variantID;
    registry[key] = info;
  }
}

bool ShaderVariantRegistry::findVariant(const BytesKey& programKey,
                                         ShaderVariantInfo* outInfo) const {
  // Registry is immutable after construction; read-only access is thread-safe
  // without locking on most platforms. We use a lock for extra safety.
  std::lock_guard<std::mutex> lock(mutex);
  auto it = registry.find(programKey);
  if (it == registry.end()) {
    return false;
  }
  *outInfo = it->second;
  return true;
}

uint32_t ShaderVariantRegistry::variantCount() const {
  return SHADER_VARIANT_COUNT;
}

}  // namespace tgfx
```

### 7.3 运行时查找

运行时查找发生在 `ProgramInfo::getProgram()` 的 UE 快速路径中（详见第 8 章）。查找是 O(1) 哈希表操作。

### 7.4 线程安全

- Registry 在模块加载时一次性构建，之后只读。
- `BytesKeyMap` 底层是 `std::unordered_map`，只读并发访问在大多数 STL 实现中是安全的。
- 为保险起见仍加了 `std::mutex`。如果性能分析表明锁成为瓶颈，可以改为：
  1. 使用 `std::shared_mutex` + `shared_lock` 读锁
  2. 或彻底去掉锁（因为 map 只读，C++ 标准保证并发 const 访问安全）

---

## 8. tgfx 核心修改

### 8.1 ProgramInfo::getProgram() 的 UE 快速路径

在 UE 后端下，`ProgramInfo::getProgram()` 跳过 GLSL 生成和编译，直接通过 Registry 查找预编译 Shader：

```cpp
// src/gpu/ProgramInfo.cpp 中新增的 UE 快速路径

#ifdef TGFX_USE_UE_RHI

std::shared_ptr<Program> ProgramInfo::getProgram() const {
  auto context = renderTarget->getContext();
  BytesKey programKey = {};
  geometryProcessor->computeProcessorKey(context, &programKey);
  for (const auto& processor : fragmentProcessors) {
    processor->computeProcessorKey(context, &programKey);
  }
  if (xferProcessor != nullptr) {
    xferProcessor->computeProcessorKey(context, &programKey);
  }
  programKey.write(static_cast<uint32_t>(blendMode));
  programKey.write(static_cast<uint32_t>(getOutputSwizzle().asKey()));
  programKey.write(static_cast<uint32_t>(cullMode));
  programKey.write(static_cast<uint32_t>(renderTarget->format()));
  programKey.write(static_cast<uint32_t>(renderTarget->sampleCount()));

  // Fast path: check GlobalCache first (for reuse within same frame).
  auto program = context->globalCache()->findProgram(programKey);
  if (program != nullptr) {
    return program;
  }

  // Look up in ShaderVariantRegistry.
  ShaderVariantInfo variantInfo;
  if (!ShaderVariantRegistry::Instance()->findVariant(programKey, &variantInfo)) {
    LOGE("ProgramInfo::getProgram() UE RHI: ProgramKey not found in registry! "
         "This indicates a missing shader variant.");
    return nullptr;
  }

  // Create UERHIProgram wrapping the UE pre-compiled shader.
  auto ueProgram = UERHIProgram::Create(context, this, variantInfo);
  if (ueProgram == nullptr) {
    LOGE("ProgramInfo::getProgram() Failed to create UE RHI program!");
    return nullptr;
  }

  context->globalCache()->addProgram(programKey, ueProgram);
  return ueProgram;
}

#endif  // TGFX_USE_UE_RHI
```

ProgramKey 的计算逻辑与原有代码完全相同，确保与离线生成的 key 匹配。

### 8.2 UERHIGPU 实现

```cpp
// src/gpu/ue/UERHIGPU.h
// Copyright (C) 2026 Tencent. All rights reserved.

#pragma once

#include "tgfx/gpu/GPU.h"

// Forward declarations for UE types
class FRHICommandListImmediate;

namespace tgfx {

class UERHIGPU : public GPU {
 public:
  explicit UERHIGPU(FRHICommandListImmediate& rhiCommandList);

  const GPUInfo* info() const override;
  const GPUFeatures* features() const override;
  const GPULimits* limits() const override;
  CommandQueue* queue() const override;

  std::shared_ptr<GPUBuffer> createBuffer(size_t size, uint32_t usage) override;
  bool isFormatRenderable(PixelFormat pixelFormat) const override;
  int getSampleCount(int requestedCount, PixelFormat pixelFormat) const override;
  std::shared_ptr<Texture> createTexture(const TextureDescriptor& descriptor) override;

  std::vector<std::shared_ptr<Texture>> importHardwareTextures(
      HardwareBufferRef hardwareBuffer, uint32_t usage) override;
  std::shared_ptr<Texture> importBackendTexture(const BackendTexture& backendTexture,
                                                 uint32_t usage, bool adopted) override;
  std::shared_ptr<Texture> importBackendRenderTarget(
      const BackendRenderTarget& backendRenderTarget) override;
  std::shared_ptr<Semaphore> importBackendSemaphore(
      const BackendSemaphore& semaphore) override;
  BackendSemaphore stealBackendSemaphore(std::shared_ptr<Semaphore> semaphore) override;

  std::shared_ptr<Sampler> createSampler(const SamplerDescriptor& descriptor) override;

  // In UE RHI mode, createShaderModule is NOT used (shaders are pre-compiled).
  // This returns a stub that holds the variant info for pipeline creation.
  std::shared_ptr<ShaderModule> createShaderModule(
      const ShaderModuleDescriptor& descriptor) override;

  std::shared_ptr<RenderPipeline> createRenderPipeline(
      const RenderPipelineDescriptor& descriptor) override;

  std::shared_ptr<CommandEncoder> createCommandEncoder() override;

  FRHICommandListImmediate& getRHICommandList() const {
    return rhiCommandList;
  }

 private:
  FRHICommandListImmediate& rhiCommandList;
  GPUInfo gpuInfo;
  GPUFeatures gpuFeatures;
  GPULimits gpuLimits;
};

}  // namespace tgfx
```

### 8.3 UERHIRenderPass 实现

```cpp
// src/gpu/ue/UERHIRenderPass.h
// Copyright (C) 2026 Tencent. All rights reserved.

#pragma once

#include "tgfx/gpu/RenderPass.h"

class FRHICommandListImmediate;
class FRHITexture;

namespace tgfx {

class UERHIRenderPass : public RenderPass {
 public:
  UERHIRenderPass(RenderPassDescriptor descriptor, FRHICommandListImmediate& rhiCommandList);

  GPU* gpu() const override;

  void setViewport(int x, int y, int width, int height) override;
  void setScissorRect(int x, int y, int width, int height) override;
  void setPipeline(std::shared_ptr<RenderPipeline> pipeline) override;
  void setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer,
                        size_t offset, size_t size) override;
  void setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                  std::shared_ptr<Sampler> sampler) override;
  void setVertexBuffer(unsigned slot, std::shared_ptr<GPUBuffer> buffer,
                       size_t offset) override;
  void setIndexBuffer(std::shared_ptr<GPUBuffer> buffer, IndexFormat format) override;
  void setStencilReference(uint32_t reference) override;
  void draw(PrimitiveType primitiveType, uint32_t vertexCount, uint32_t instanceCount,
            uint32_t firstVertex, uint32_t firstInstance) override;
  void drawIndexed(PrimitiveType primitiveType, uint32_t indexCount, uint32_t instanceCount,
                   uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) override;

 protected:
  void onEnd() override;

 private:
  FRHICommandListImmediate& rhiCommandList;

  // Cached state to minimize redundant RHI calls.
  std::shared_ptr<RenderPipeline> currentPipeline;
  FRHIBuffer* currentVertexBuffers[2] = {nullptr, nullptr};
  size_t currentVertexOffsets[2] = {0, 0};
  FRHIBuffer* currentIndexBuffer = nullptr;
  IndexFormat currentIndexFormat = IndexFormat::UInt16;

  // Pending uniform buffer bindings.
  struct UniformBinding {
    std::shared_ptr<GPUBuffer> buffer;
    size_t offset = 0;
    size_t size = 0;
  };
  UniformBinding pendingUniforms[2] = {};  // [0]=Vertex, [1]=Fragment

  // Pending texture bindings.
  struct TextureBinding {
    std::shared_ptr<Texture> texture;
    std::shared_ptr<Sampler> sampler;
  };
  static constexpr int MAX_TEXTURE_BINDINGS = 16;
  TextureBinding pendingTextures[MAX_TEXTURE_BINDINGS] = {};
  int maxTextureBinding = -1;

  void applyPipelineState();
  void applyUniforms();
  void applyTextures();
};

}  // namespace tgfx
```

### 8.4 Uniform Buffer 适配（std140 -> UE RHI UBO）

tgfx 的 `UniformData` 使用 std140 布局写入一块连续内存。在 UE RHI 后端中，这块内存直接上传为 `FRHIUniformBuffer` 或通过 `FRHICommandList::SetShaderUniformBuffer()` 绑定。

关键点：**SPIRV-Cross 生成的 HLSL cbuffer 布局与 std140 完全匹配**（SPIRV-Cross 内部保持 SPIRV 的 std140 偏移），因此不需要任何布局转换。

```cpp
// UERHIRenderPass::applyUniforms() implementation
void UERHIRenderPass::applyUniforms() {
  for (int i = 0; i < 2; ++i) {
    auto& binding = pendingUniforms[i];
    if (!binding.buffer) continue;

    auto srcBuffer = static_cast<UERHIGPUBuffer*>(binding.buffer.get());
    const uint8_t* srcData = srcBuffer->getMappedData() + binding.offset;

    // Upload uniform data directly via SetShaderParameter.
    if (i == 0) {
      rhiCommandList.SetShaderParameter(
          rhiCommandList.GetBoundVertexShader(),
          0,             // BaseIndex (cbuffer register)
          0,             // BufferOffset
          binding.size,  // NumBytes
          srcData);
    } else {
      rhiCommandList.SetShaderParameter(
          rhiCommandList.GetBoundPixelShader(),
          1,             // BaseIndex (cbuffer register)
          0,             // BufferOffset
          binding.size,  // NumBytes
          srcData);
    }
  }
}
```

**重要**：UE 的 `SetShaderUniformBuffer` 绑定整个 buffer。如果 tgfx 使用 GlobalCache 的环形 buffer（多个 draw call 共享同一 buffer 的不同 offset），需要改用 `SetShaderParameter` 按字节上传 uniform 数据，或者为 UE 后端在每次 draw call 时创建独立的小 uniform buffer。推荐方案是使用 `SetShaderParameter` 按字节上传，它避免了 UBO 创建开销，数据直接写入 RHI 命令流。

### 8.5 Texture/Sampler 绑定

```cpp
void UERHIRenderPass::setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                                  std::shared_ptr<Sampler> sampler) {
  // tgfx texture bindings start at TEXTURE_BINDING_POINT_START (2).
  // In HLSL, textures are at register(t0), register(t1), etc.
  // The mapping is: tgfx binding N -> HLSL register(t(N-2)), register(s(N-2)).
  int hlslSlot = static_cast<int>(binding) - TEXTURE_BINDING_POINT_START;
  if (hlslSlot >= 0 && hlslSlot < MAX_TEXTURE_BINDINGS) {
    pendingTextures[hlslSlot].texture = std::move(texture);
    pendingTextures[hlslSlot].sampler = std::move(sampler);
    if (hlslSlot > maxTextureBinding) {
      maxTextureBinding = hlslSlot;
    }
  }
}

void UERHIRenderPass::applyTextures() {
  for (int i = 0; i <= maxTextureBinding; ++i) {
    auto& tb = pendingTextures[i];
    if (!tb.texture) continue;

    auto ueTexture = static_cast<UERHITexture*>(tb.texture.get());
    auto ueSampler = static_cast<UERHISampler*>(tb.sampler.get());

    // Bind to pixel shader (fragment textures).
    rhiCommandList.SetShaderTexture(
        rhiCommandList.GetBoundPixelShader(),
        i,  // texture slot
        ueTexture->getRHITexture());
    rhiCommandList.SetShaderSampler(
        rhiCommandList.GetBoundPixelShader(),
        i,  // sampler slot
        ueSampler->getRHISamplerState());
  }
}
```

### 8.6 Vertex Buffer / Index Buffer 管理

```cpp
void UERHIRenderPass::setVertexBuffer(unsigned slot, std::shared_ptr<GPUBuffer> buffer,
                                       size_t offset) {
  if (slot < 2) {
    auto ueBuffer = buffer ? static_cast<UERHIGPUBuffer*>(buffer.get()) : nullptr;
    currentVertexBuffers[slot] = ueBuffer ? ueBuffer->getRHIBuffer() : nullptr;
    currentVertexOffsets[slot] = offset;
  }
}

void UERHIRenderPass::setIndexBuffer(std::shared_ptr<GPUBuffer> buffer, IndexFormat format) {
  auto ueBuffer = buffer ? static_cast<UERHIGPUBuffer*>(buffer.get()) : nullptr;
  currentIndexBuffer = ueBuffer ? ueBuffer->getRHIBuffer() : nullptr;
  currentIndexFormat = format;
}

void UERHIRenderPass::draw(PrimitiveType primitiveType, uint32_t vertexCount,
                            uint32_t instanceCount, uint32_t firstVertex,
                            uint32_t firstInstance) {
  applyPipelineState();
  applyUniforms();
  applyTextures();

  // Bind vertex buffers.
  for (int i = 0; i < 2; ++i) {
    if (currentVertexBuffers[i]) {
      rhiCommandList.SetStreamSource(i, currentVertexBuffers[i],
                                     static_cast<uint32_t>(currentVertexOffsets[i]));
    }
  }

  uint32_t primitiveCount = 0;
  EPrimitiveType ueType;
  if (primitiveType == PrimitiveType::Triangles) {
    ueType = PT_TriangleList;
    primitiveCount = vertexCount / 3;
  } else {
    ueType = PT_TriangleStrip;
    primitiveCount = vertexCount - 2;
  }

  if (instanceCount > 1) {
    rhiCommandList.DrawPrimitive(firstVertex, primitiveCount, instanceCount);
  } else {
    rhiCommandList.DrawPrimitive(firstVertex, primitiveCount, 1);
  }
}

void UERHIRenderPass::drawIndexed(PrimitiveType primitiveType, uint32_t indexCount,
                                   uint32_t instanceCount, uint32_t firstIndex,
                                   int32_t baseVertex, uint32_t firstInstance) {
  applyPipelineState();
  applyUniforms();
  applyTextures();

  for (int i = 0; i < 2; ++i) {
    if (currentVertexBuffers[i]) {
      rhiCommandList.SetStreamSource(i, currentVertexBuffers[i],
                                     static_cast<uint32_t>(currentVertexOffsets[i]));
    }
  }

  uint32_t primitiveCount = 0;
  if (primitiveType == PrimitiveType::Triangles) {
    primitiveCount = indexCount / 3;
  } else {
    primitiveCount = indexCount - 2;
  }

  rhiCommandList.DrawIndexedPrimitive(
      currentIndexBuffer,
      baseVertex,
      0,  // MinVertexIndex
      0,  // MaxVertexIndex (0 = unused for modern RHI)
      firstIndex,
      primitiveCount,
      instanceCount);
}
```

---

## 9. Uniform 与纹理参数绑定

### 9.1 UniformData 的 std140 布局规则

tgfx 的 `UniformData`（`src/gpu/UniformData.h`）使用 std140 布局规则分配内存：

| UniformFormat | GLSL 类型 | std140 大小 | std140 对齐 |
|---------------|-----------|-------------|-------------|
| Float | float | 4 | 4 |
| Float2 | vec2 | 8 | 8 |
| Float3 | vec3 | 12 | 16 |
| Float4 | vec4 | 16 | 16 |
| Float2x2 | mat2 | 32 | 16 |
| Float3x3 | mat3 | 48 (3x vec4) | 16 |
| Float4x4 | mat4 | 64 | 16 |
| Int | int | 4 | 4 |
| Int2 | ivec2 | 8 | 8 |
| Int3 | ivec3 | 12 | 16 |
| Int4 | ivec4 | 16 | 16 |

特别注意 `Matrix`（3x3）的写入方式（`src/gpu/UniformData.h:62-73`）：
- 从 `Matrix::get9()` 获取 9 个 float
- 重排为 3 列 x 4 float（列优先，每列第 4 个元素 pad 为 0）
- 总共写入 12 个 float = 48 字节

SPIRV-Cross 在 HLSL 中生成 `column_major float3x4`（而非 `float3x3`），其 cbuffer 布局与上述 48 字节完全匹配。

### 9.2 UE RHI Uniform Buffer 创建和绑定

在 UE RHI 中，uniform 数据的传递方式有两种：

**方式 A：直接字节上传（推荐）**
```cpp
// 将 std140 布局的字节直接设置到 cbuffer。
// UE 5.4+ 的 FRHICommandList 支持 SetShaderParameter 按字节写入 cbuffer 指定偏移。
rhiCommandList.SetShaderParameter(
    VertexShaderRHI,
    /* BufferIndex */ 0,   // register(b0) for VertexUniformBlock
    /* BaseIndex */ 0,     // offset within cbuffer
    /* NumBytes */ vertexUniformData->size(),
    /* NewValue */ vertexUniformDataPtr);
```

**方式 B：FRHIUniformBuffer**
```cpp
// 创建一个包含原始字节的 uniform buffer。
FRHIResourceCreateInfo CreateInfo(TEXT("TgfxVertexUBO"));
FBufferRHIRef UBO = RHICreateUniformBuffer(
    vertexUniformDataPtr,
    /* Layout */ VertexUniformLayout,
    UniformBuffer_SingleFrame);
rhiCommandList.SetShaderUniformBuffer(VertexShaderRHI, 0, UBO);
```

方式 A 更简单（无需声明 `FShaderParameterStruct` layout），因为 tgfx 的 uniform 布局是动态变化的（每个变体不同）。

### 9.3 纹理采样器绑定方式

tgfx 的纹理绑定起始于 `TEXTURE_BINDING_POINT_START = 2`。在 HLSL 中，SPIRV-Cross 将 sampler binding 映射为从 0 开始的连续 `register(t/s)` 槽位。因此需要做偏移转换：

```
tgfx binding 2  ->  HLSL register(t0), register(s0)
tgfx binding 3  ->  HLSL register(t1), register(s1)
tgfx binding N  ->  HLSL register(t(N-2)), register(s(N-2))
```

### 9.4 Binding Point 映射总表

| tgfx Binding | 资源类型 | HLSL Register | UE RHI API |
|--------------|----------|---------------|------------|
| 0 | VertexUniformBlock | `b0` | `SetShaderParameter(VS, 0, ...)` |
| 1 | FragmentUniformBlock | `b1` | `SetShaderParameter(PS, 1, ...)` |
| 2 | Texture 0 | `t0` + `s0` | `SetShaderTexture(PS, 0, ...)` + `SetShaderSampler(PS, 0, ...)` |
| 3 | Texture 1 | `t1` + `s1` | `SetShaderTexture(PS, 1, ...)` + `SetShaderSampler(PS, 1, ...)` |
| N | Texture N-2 | `t(N-2)` + `s(N-2)` | `SetShaderTexture(PS, N-2, ...)` + `SetShaderSampler(PS, N-2, ...)` |

---

## 10. PSO 创建与管理

### 10.1 FGraphicsPipelineStateInitializer 构建

每次 `setPipeline()` 时构建 PSO：

```cpp
// src/gpu/ue/UERHIRenderPass.cpp

void UERHIRenderPass::applyPipelineState() {
  auto ueProgram = static_cast<UERHIProgram*>(currentPipeline.get());
  if (!ueProgram) return;

  FGraphicsPipelineStateInitializer PSOInit;

  // Shaders
  PSOInit.BoundShaderState.VertexShaderRHI = ueProgram->getVertexShader();
  PSOInit.BoundShaderState.PixelShaderRHI = ueProgram->getPixelShader();
  PSOInit.BoundShaderState.VertexDeclarationRHI = ueProgram->getVertexDeclaration();

  // Primitive type
  PSOInit.PrimitiveType = PT_TriangleList;  // tgfx primarily uses triangle list

  // Blend state
  auto& blend = ueProgram->getBlendState();
  PSOInit.BlendState = blend;

  // Rasterizer state
  PSOInit.RasterizerState = ueProgram->getRasterizerState();

  // Depth/Stencil state
  PSOInit.DepthStencilState = ueProgram->getDepthStencilState();

  // Render target format
  PSOInit.NumSamples = ueProgram->getSampleCount();

  SetGraphicsPipelineState(rhiCommandList, PSOInit, 0);
}
```

### 10.2 Blend State 映射

tgfx `BlendFactor` -> UE `EBlendFactor`:

```cpp
static EBlendFactor ToUEBlendFactor(BlendFactor factor) {
  switch (factor) {
    case BlendFactor::Zero:             return BF_Zero;
    case BlendFactor::One:              return BF_One;
    case BlendFactor::Src:              return BF_SourceColor;
    case BlendFactor::OneMinusSrc:      return BF_InverseSourceColor;
    case BlendFactor::Dst:              return BF_DestColor;
    case BlendFactor::OneMinusDst:      return BF_InverseDestColor;
    case BlendFactor::SrcAlpha:         return BF_SourceAlpha;
    case BlendFactor::OneMinusSrcAlpha: return BF_InverseSourceAlpha;
    case BlendFactor::DstAlpha:         return BF_DestAlpha;
    case BlendFactor::OneMinusDstAlpha: return BF_InverseDestAlpha;
    default:                            return BF_One;
  }
}

static EBlendOperation ToUEBlendOp(BlendOperation op) {
  switch (op) {
    case BlendOperation::Add:             return BO_Add;
    case BlendOperation::Subtract:        return BO_Subtract;
    case BlendOperation::ReverseSubtract: return BO_ReverseSubtract;
    default:                              return BO_Add;
  }
}

static FBlendStateRHIRef CreateBlendState(const PipelineColorAttachment& attachment) {
  if (!attachment.blendEnable) {
    return TStaticBlendState<>::GetRHI();
  }
  // For dynamic blend states, use FBlendStateInitializerRHI.
  FBlendStateInitializerRHI BlendInit;
  BlendInit.bUseIndependentRenderTargetBlendStates = false;
  BlendInit.RenderTargets[0].ColorBlendOp = ToUEBlendOp(attachment.colorBlendOp);
  BlendInit.RenderTargets[0].ColorSrcBlend = ToUEBlendFactor(attachment.srcColorBlendFactor);
  BlendInit.RenderTargets[0].ColorDestBlend = ToUEBlendFactor(attachment.dstColorBlendFactor);
  BlendInit.RenderTargets[0].AlphaBlendOp = ToUEBlendOp(attachment.alphaBlendOp);
  BlendInit.RenderTargets[0].AlphaSrcBlend = ToUEBlendFactor(attachment.srcAlphaBlendFactor);
  BlendInit.RenderTargets[0].AlphaDestBlend = ToUEBlendFactor(attachment.dstAlphaBlendFactor);
  BlendInit.RenderTargets[0].ColorWriteMask =
      static_cast<EColorWriteMask>(attachment.colorWriteMask);
  return RHICreateBlendState(BlendInit);
}
```

### 10.3 Rasterizer State 映射

```cpp
static FRasterizerStateRHIRef CreateRasterizerState(CullMode cullMode) {
  FRasterizerStateInitializerRHI RastInit;
  RastInit.FillMode = FM_Solid;
  switch (cullMode) {
    case CullMode::None:
      RastInit.CullMode = CM_None;
      break;
    case CullMode::Front:
      RastInit.CullMode = CM_CW;   // tgfx uses CW as front face
      break;
    case CullMode::Back:
      RastInit.CullMode = CM_CCW;
      break;
  }
  RastInit.bAllowMSAA = true;
  RastInit.bEnableLineAA = false;
  return RHICreateRasterizerState(RastInit);
}
```

### 10.4 Depth/Stencil State

tgfx 的大多数 DrawOp 不使用深度/模板测试。默认配置：

```cpp
static FDepthStencilStateRHIRef CreateDefaultDepthStencilState() {
  // No depth test, no stencil test.
  return TStaticDepthStencilState<false, CF_Always>::GetRHI();
}
```

如果 tgfx 将来使用 stencil（如路径裁剪），需要扩展：

```cpp
static FDepthStencilStateRHIRef CreateDepthStencilState(
    const DepthStencilDescriptor& descriptor) {
  FDepthStencilStateInitializerRHI DSInit;
  DSInit.bEnableDepthWrite = descriptor.depthWriteEnabled;
  DSInit.DepthTest = ToUECompareFunc(descriptor.depthCompare);
  DSInit.FrontFaceStencilTest = ToUECompareFunc(descriptor.stencilFront.compare);
  DSInit.FrontFaceStencilFailStencilOp = ToUEStencilOp(descriptor.stencilFront.failOp);
  DSInit.FrontFaceDepthFailStencilOp = ToUEStencilOp(descriptor.stencilFront.depthFailOp);
  DSInit.FrontFacePassStencilOp = ToUEStencilOp(descriptor.stencilFront.passOp);
  DSInit.BackFaceStencilTest = ToUECompareFunc(descriptor.stencilBack.compare);
  DSInit.BackFaceStencilFailStencilOp = ToUEStencilOp(descriptor.stencilBack.failOp);
  DSInit.BackFaceDepthFailStencilOp = ToUEStencilOp(descriptor.stencilBack.depthFailOp);
  DSInit.BackFacePassStencilOp = ToUEStencilOp(descriptor.stencilBack.passOp);
  DSInit.StencilReadMask = static_cast<uint8>(descriptor.stencilReadMask);
  DSInit.StencilWriteMask = static_cast<uint8>(descriptor.stencilWriteMask);
  return RHICreateDepthStencilState(DSInit);
}
```

### 10.5 Vertex Declaration 构建

tgfx 的 `Attribute` 需要映射到 UE 的 `FVertexElement`：

```cpp
static EVertexElementType ToUEVertexFormat(VertexFormat format) {
  switch (format) {
    case VertexFormat::Float:            return VET_Float1;
    case VertexFormat::Float2:           return VET_Float2;
    case VertexFormat::Float3:           return VET_Float3;
    case VertexFormat::Float4:           return VET_Float4;
    case VertexFormat::Half2:            return VET_Half2;
    case VertexFormat::Half4:            return VET_Half4;
    case VertexFormat::UByte4Normalized: return VET_UByte4N;
    case VertexFormat::Int:              return VET_UInt;
    default:                             return VET_Float4;
  }
}

static FVertexDeclarationRHIRef CreateVertexDeclaration(
    const std::vector<Attribute>& vertexAttributes,
    const std::vector<Attribute>& instanceAttributes) {
  FVertexDeclarationElementList Elements;

  // Vertex attributes (stream 0)
  uint32_t offset = 0;
  for (size_t i = 0; i < vertexAttributes.size(); ++i) {
    const auto& attr = vertexAttributes[i];
    FVertexElement elem;
    elem.StreamIndex = 0;
    elem.Offset = offset;
    elem.Type = ToUEVertexFormat(attr.format());
    elem.AttributeIndex = static_cast<uint8>(i);
    elem.bUseInstanceIndex = false;
    Elements.Add(elem);
    offset += static_cast<uint32_t>(attr.size());
  }

  // Instance attributes (stream 1)
  offset = 0;
  for (size_t i = 0; i < instanceAttributes.size(); ++i) {
    const auto& attr = instanceAttributes[i];
    FVertexElement elem;
    elem.StreamIndex = 1;
    elem.Offset = offset;
    elem.Type = ToUEVertexFormat(attr.format());
    elem.AttributeIndex = static_cast<uint8>(vertexAttributes.size() + i);
    elem.bUseInstanceIndex = true;
    elem.Stride = 0;  // UE calculates from layout
    Elements.Add(elem);
    offset += static_cast<uint32_t>(attr.size());
  }

  return RHICreateVertexDeclaration(Elements);
}
```

---

## 11. 边界情况处理

### 11.1 未枚举到的 ProgramKey（运行时 fallback 策略）

如果运行时遇到 `ShaderVariantRegistry` 中找不到的 ProgramKey：

1. **Debug 模式**：记录详细错误日志（包括 ProgramKey hex、GP/FP/XP 名称），绘制洋红色填充方便定位。
2. **Release 模式**：静默跳过该 DrawOp（不渲染），避免崩溃。
3. **根本解决**：这表明离线枚举工具有遗漏。需要：
   - 在 Debug 模式下收集未命中的 ProgramKey 并导出
   - 分析是哪种 Processor 组合未覆盖
   - 更新枚举工具的覆盖范围
   - 重新运行工具生成更新的 Shader 文件

```cpp
// Runtime fallback in ProgramInfo::getProgram() UE path
if (!ShaderVariantRegistry::Instance()->findVariant(programKey, &variantInfo)) {
#if DEBUG
  std::string keyHex = "";
  for (size_t i = 0; i < programKey.size(); ++i) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%08x", programKey.data()[i]);
    keyHex += buf;
  }
  LOGE("UE RHI: Missing shader variant! ProgramKey=%s GP=%s numFP=%zu",
       keyHex.c_str(), geometryProcessor->name().c_str(),
       fragmentProcessors.size());
  // Write to crash report file for later analysis.
  appendToMissingVariantsLog(programKey, geometryProcessor, fragmentProcessors, xferProcessor);
#endif
  return nullptr;  // DrawOp::execute() will skip rendering
}
```

### 11.2 Processor 新增后的更新流程

当 tgfx 新增 Processor 子类或修改现有 Processor 的 `computeProcessorKey()`/`emitCode()` 时：

1. 在枚举工具中为新 Processor 添加枚举分支
2. 重新运行 `TgfxShaderVariantGenerator`
3. 重新生成 `.ush` 文件和 Registry
4. 重新编译 UE Shader（UE Editor 或 Cook）

**CI 集成**：建议在 CI 中添加一步：运行枚举工具并检查 `tgfx_shader_variants.json` 是否有变化。如果有变化但未更新 Generated 文件，构建失败。

### 11.3 UE 版本兼容性

| UE 版本 | 注意事项 |
|---------|----------|
| 5.4 | `SetShaderParameter` API 存在但某些重载有限。需检查 cbuffer 大小限制。 |
| 5.5 | `FRHICommandList` 接口稳定。RHI 2.0 开始引入，Global Shader 接口不变。 |
| 5.6 | Shader Model 6.0 可选。我们用 SM 5.0，不受影响。 |
| 5.7 | 需验证 `SHADER_PERMUTATION_RANGE_INT` 最大值是否有新限制。 |

**兼容策略**：
- 用 `#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4` 条件编译处理 API 差异
- SM 5.0 作为目标，所有 UE 5.x 平台均支持
- 不使用 UE 5.6+ 的新 RHI 2.0 专有 API

### 11.4 平台差异

| 平台 | RHI | 注意事项 |
|------|-----|----------|
| Windows | D3D11 | SM 5.0，cbuffer 每个 slot 最大 4096 个 float4。tgfx 的 UBO < 64KB，无风险。 |
| Windows | D3D12 | 与 D3D11 HLSL 兼容。Root Signature 由 UE 管理。 |
| Windows/Linux | Vulkan | UE 的 Vulkan RHI 接受 HLSL 并通过 DXC 编译为 SPIRV。SM 5.0 HLSL 兼容。 |
| macOS/iOS | Metal | UE 的 Metal RHI 接受 HLSL 并通过 DXC->SPIRV->MSL 管线。SM 5.0 兼容。 |
| Android | Vulkan | 同 Windows Vulkan。 |
| Android | OpenGL ES | UE 5.4+ 仍支持但已逐步弃用。如需支持，需要额外的 GLSL ES 输出路径。 |

### 11.5 Shader 编译错误诊断

离线工具在生成 HLSL 后应执行验证编译（使用 `dxc` 或 `fxc`）：

```cpp
// In TgfxShaderVariantGenerator, after HLSL generation:
static bool validateHLSL(const std::string& hlsl, ShaderStage stage,
                          const std::string& variantName) {
  // Write to temp file.
  std::string tempPath = "/tmp/tgfx_validate_" + variantName + ".hlsl";
  writeFile(tempPath, hlsl);

  // Invoke dxc to validate.
  std::string profile = (stage == ShaderStage::Vertex) ? "vs_5_0" : "ps_5_0";
  std::string entryPoint = (stage == ShaderStage::Vertex) ? "MainVS" : "MainPS";
  std::string cmd = "dxc -T " + profile + " -E " + entryPoint +
                    " -Fo /dev/null " + tempPath + " 2>&1";
  int exitCode = system(cmd.c_str());
  return exitCode == 0;
}
```

### 11.6 Hot Reload 支持

UE 的 Shader Hot Reload（Ctrl+Shift+.）在 Editor 中重新编译所有 Shader。由于 tgfx Shader 是 Global Shader，它们会被自动重新编译。Hot Reload 后：

1. `TShaderMapRef` 自动获取新编译的 Shader
2. tgfx 的 `GlobalCache` 中缓存的 `UERHIProgram` 持有的 Shader 引用变为无效
3. 需要在 Hot Reload 回调中清空 `GlobalCache` 的 program cache

```cpp
// Register hot reload callback in plugin StartupModule()
FCoreDelegates::OnShadersPreCompile.AddLambda([]() {
  // Clear tgfx program cache so it re-fetches from updated ShaderMap.
  if (auto* context = GetTgfxContext()) {
    context->globalCache()->clearProgramCache();
  }
});
```

---

## 12. 性能优化

### 12.1 Registry 查找优化（O(1) 哈希）

`ShaderVariantRegistry` 使用 `BytesKeyMap`（`std::unordered_map<BytesKey, ..., BytesKeyHasher>`），查找复杂度 O(1)。`BytesKeyHasher` 对 key 的 `uint32_t` 数组做位混合哈希，与 `GlobalCache` 使用相同的哈希函数。

进一步优化：
- 预先分配哈希表容量（`reserve(SHADER_VARIANT_COUNT)`）
- 如果 ProgramKey 长度固定或可预测，可以使用更简单的哈希（如直接取前 2-3 个 uint32 做 XOR）

### 12.2 PSO 缓存策略

UE 内部已经有 PSO 缓存（`PipelineStateCache`）。每个唯一的 `FGraphicsPipelineStateInitializer` 只创建一次 PSO。tgfx 端不需要额外缓存。

但可以优化 PSO Initializer 的构建：
- 为每个 `{variantID, blendState, cullMode, sampleCount}` 组合缓存 `FGraphicsPipelineStateInitializer`
- 避免重复创建 `FBlendStateRHIRef` 和 `FRasterizerStateRHIRef`

```cpp
// PSO initializer cache in UERHIProgram
class UERHIProgram : public RenderPipeline {
  // Cache PSO init per unique state combination.
  // Since most state comes from the variant itself, this is often a 1:1 mapping.
  FGraphicsPipelineStateInitializer cachedPSOInit;
  bool psoInitCached = false;

  const FGraphicsPipelineStateInitializer& getPSOInit() {
    if (!psoInitCached) {
      buildPSOInit();
      psoInitCached = true;
    }
    return cachedPSOInit;
  }
};
```

### 12.3 Uniform Buffer 环形复用

tgfx 的 `GlobalCache::findOrCreateUniformBuffer()` 使用环形 buffer 池（`UniformBufferPacket`）复用 GPU buffer，减少创建/销毁开销。在 UE 后端中：

- 如果使用 `SetShaderParameter` 按字节上传，不需要 GPU buffer 管理（UE 内部处理）
- 如果使用 `FRHIUniformBuffer`，可以利用 `UniformBuffer_SingleFrame` 策略让 UE 自动回收

推荐使用 `SetShaderParameter` 方式，它避免了 UBO 创建开销，数据直接写入 RHI 命令流。

### 12.4 Draw Call 批处理

tgfx 已经在上层（`OpsCompositor`）做了 Draw Call 合并：相邻的同类 DrawOp 尽可能合并为一个 draw call。UE 后端保持这一行为不变。

额外优化：
- 排序 DrawOp 以最小化 PSO 切换（相同 variant 的 DrawOp 连续执行）
- 使用 `RHIBeginRenderQuery` / `RHIEndRenderQuery` 收集 GPU 统计

### 12.5 避免不必要的状态切换

```cpp
void UERHIRenderPass::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
  if (pipeline == currentPipeline) {
    return;  // Same pipeline, skip state setup.
  }
  currentPipeline = std::move(pipeline);
}
```

类似地，texture 和 vertex buffer 绑定都做脏标记检查。

---

## 13. 测试策略

### 13.1 离线工具单元测试

```
test/src/UEShaderVariantTest.cpp
```

测试内容：
1. **枚举完整性**：运行枚举器，验证输出的变体数 >= 预期下限（如 300）
2. **GLSL 生成**：每个变体的 VS/PS GLSL 非空
3. **SPIRV 编译**：每个变体的 GLSL 可成功编译为 SPIRV
4. **HLSL 转换**：每个变体的 SPIRV 可成功转为 HLSL
5. **ProgramKey 唯一性**：无重复 key
6. **Registry 一致性**：生成的 Registry 可正确查找所有已枚举的 key

```cpp
// test/src/UEShaderVariantTest.cpp
// Copyright (C) 2026 Tencent. All rights reserved.

#include <gtest/gtest.h>
#include "gpu/ue/tools/ShaderVariantEnumerator.h"
#include "gpu/ue/tools/SPIRVToHLSL.h"

namespace tgfx {

TEST(UEShaderVariantTest, EnumerateAllVariants) {
  BlockAllocator allocator;
  auto mockContext = createMockContext();
  ShaderVariantEnumerator enumerator(mockContext.get(), &allocator);

  int totalVariants = 0;
  BytesKeyMap<bool> seenKeys;

  enumerator.enumerateAll([&](const ProgramInfo& info, const BytesKey& key,
                              const std::string& vertexGLSL, const std::string& fragmentGLSL) {
    EXPECT_FALSE(vertexGLSL.empty());
    EXPECT_FALSE(fragmentGLSL.empty());
    EXPECT_EQ(seenKeys.count(key), 0u);
    seenKeys[key] = true;
    totalVariants++;
  });

  EXPECT_GE(totalVariants, 300);
  EXPECT_LE(totalVariants, 2000);
}

TEST(UEShaderVariantTest, GLSLToHLSLConversion) {
  BlockAllocator allocator;
  auto mockContext = createMockContext();
  ShaderVariantEnumerator enumerator(mockContext.get(), &allocator);

  int conversionErrors = 0;

  enumerator.enumerateAll([&](const ProgramInfo& info, const BytesKey& key,
                              const std::string& vertexGLSL, const std::string& fragmentGLSL) {
    auto vsResult = convertGLSLToHLSL(vertexGLSL, ShaderStage::Vertex);
    auto psResult = convertGLSLToHLSL(fragmentGLSL, ShaderStage::Fragment);

    if (!vsResult.success) {
      fprintf(stderr, "VS conversion failed: %s\n", vsResult.error.c_str());
      conversionErrors++;
    }
    if (!psResult.success) {
      fprintf(stderr, "PS conversion failed: %s\n", psResult.error.c_str());
      conversionErrors++;
    }
  });

  EXPECT_EQ(conversionErrors, 0);
}

TEST(UEShaderVariantTest, RegistryLookupConsistency) {
  BlockAllocator allocator;
  auto mockContext = createMockContext();
  ShaderVariantEnumerator enumerator(mockContext.get(), &allocator);

  std::vector<BytesKey> allKeys;
  enumerator.enumerateAll([&](const ProgramInfo& info, const BytesKey& key,
                              const std::string& vertexGLSL, const std::string& fragmentGLSL) {
    allKeys.push_back(key);
  });

  // Build registry from generated data.
  auto* registry = ShaderVariantRegistry::Instance();

  for (const auto& key : allKeys) {
    ShaderVariantInfo info;
    EXPECT_TRUE(registry->findVariant(key, &info));
  }
}

}  // namespace tgfx
```

### 13.2 Shader 编译正确性验证

在 CI 中使用 `dxc`（DirectX Shader Compiler）验证所有生成的 HLSL：

```bash
#!/bin/bash
# validate_hlsl_shaders.sh

ERRORS=0
for f in Generated/*_VS.ush; do
  if ! dxc -T vs_5_0 -E MainVS "$f" -Fo /dev/null 2>/dev/null; then
    echo "FAILED: $f"
    ERRORS=$((ERRORS + 1))
  fi
done
for f in Generated/*_PS.ush; do
  if ! dxc -T ps_5_0 -E MainPS "$f" -Fo /dev/null 2>/dev/null; then
    echo "FAILED: $f"
    ERRORS=$((ERRORS + 1))
  fi
done
echo "Total errors: $ERRORS"
exit $ERRORS
```

### 13.3 渲染结果对比测试

在 UE Editor 内运行 tgfx 渲染测试场景，截图与 GL/Metal 后端的基准图对比：

1. 准备一组覆盖所有 DrawOp 类型的测试 Canvas 操作
2. 分别用 GL/Metal 后端和 UE RHI 后端渲染到同尺寸纹理
3. 像素级对比（允许 1-2 LSB 差异，因为不同后端的精度可能略有不同）

### 13.4 性能基准测试

1. **ProgramKey 查找延迟**：测量 `ShaderVariantRegistry::findVariant()` 的 p50/p99 耗时
2. **Draw Call 吞吐**：在 UE 中渲染 1000 个不同 DrawOp 的总帧时间
3. **PSO 缓存命中率**：UE 的 PSO cache stats（`stat RHI`）
4. **内存占用**：Registry 哈希表内存 + 所有预编译 Shader 的 DDC 缓存大小

---

## 14. 实施步骤与依赖关系

### 14.1 四个阶段

#### 阶段 1：离线 Shader 生成工具

**任务**：
1. 实现 MockGPU / MockRenderTarget / MockContext (~300 行)
2. 将 `preprocessGLSL()` 提取为纯 C++ 文件 (~150 行)
3. 实现 `SPIRVToHLSL` 转换管线 (~200 行)
4. 实现 `ShaderVariantEnumerator`，覆盖所有 9 种 DrawOp 类型 (~800 行)
5. 实现变体输出写入器（.ush 文件 + .usf 入口 + Registry 头文件 + JSON 清单）(~400 行)
6. 实现 `TgfxShaderVariantGenerator` main 函数 (~100 行)

**总预估代码量**：~2000 行
**依赖**：shaderc, spirv-cross (已作为 tgfx Metal 后端依赖存在)

#### 阶段 2：UE Global Shader 集成

**任务**：
1. 创建 9 对 VS/PS FGlobalShader 子类 (~400 行)
2. 实现 `ShouldCompilePermutation()` + `IMPLEMENT_GLOBAL_SHADER` 注册 (~200 行)
3. 编写 TgfxCommon.ush + TgfxUniformBridge.ush (~100 行)
4. 插件模块注册 Shader 目录映射 (~30 行)
5. 验证 UE Editor 中 Shader 编译通过

**总预估代码量**：~730 行（C++）+ 自动生成的 .usf/.ush 文件
**依赖**：阶段 1 的输出产物

#### 阶段 3：UE RHI 后端核心实现

**任务**：
1. 实现 `ShaderVariantRegistry` (~150 行)
2. 实现 `UERHIGPU` (~400 行)
3. 实现 `UERHIRenderPass` (~600 行)
4. 实现 `UERHIProgram`（包装 TShaderMapRef + PSO 状态）(~300 行)
5. 实现 `UERHIGPUBuffer` / `UERHITexture` / `UERHISampler` (~300 行)
6. 修改 `ProgramInfo::getProgram()` 添加 UE 快速路径 (~50 行)
7. Blend State / Rasterizer State / DepthStencil State / VertexDeclaration 映射 (~300 行)

**总预估代码量**：~2100 行
**依赖**：阶段 1 + 阶段 2

#### 阶段 4：测试与优化

**任务**：
1. 编写离线工具单元测试 (~300 行)
2. 编写 HLSL 编译验证脚本 (~50 行)
3. 实现渲染对比测试框架 (~400 行)
4. 性能基准测试 (~200 行)
5. Hot Reload 支持 (~50 行)
6. 未命中变体的诊断日志 (~100 行)

**总预估代码量**：~1100 行
**依赖**：阶段 3

### 14.2 模块间依赖关系图

```
阶段 1: 离线 Shader 生成工具
    |
    |-- MockContext (不依赖 UE)
    |-- GLSLPreprocessor (提取自 MetalShaderModule.mm)
    |-- SPIRVToHLSL (依赖 shaderc + spirv-cross)
    |-- ShaderVariantEnumerator (依赖 tgfx 核心 Processor 类)
    +-- 输出: .ush/.usf 文件 + Registry .h + .json
         |
         v
阶段 2: UE Global Shader 集成
    |
    |-- FGlobalShader 子类 (依赖 UE Shader Framework)
    |-- Permutation 维度 (依赖阶段 1 的 MAX_VARIANT_ID 常量)
    +-- .usf 入口文件 (依赖阶段 1 的 Generated .ush 文件)
         |
         v
阶段 3: UE RHI 后端核心实现
    |
    |-- ShaderVariantRegistry (依赖阶段 1 的 Registry .h)
    |-- UERHIGPU / UERHIRenderPass (依赖 UE RHI API)
    |-- UERHIProgram (依赖阶段 2 的 FGlobalShader 类)
    +-- ProgramInfo UE 快速路径 (依赖 ShaderVariantRegistry)
         |
         v
阶段 4: 测试与优化
    |
    |-- 离线工具测试 (依赖阶段 1)
    |-- HLSL 验证 (依赖阶段 1 输出 + dxc)
    |-- 渲染对比测试 (依赖阶段 3)
    +-- 性能测试 (依赖阶段 3)
```

### 14.3 每个模块的预估代码量

| 模块 | 新增文件 | 预估行数 |
|------|----------|----------|
| MockGPU / MockRenderTarget | `src/gpu/ue/tools/MockGPU.h`, `MockRenderTarget.h` | 300 |
| GLSLPreprocessor | `src/gpu/ue/tools/GLSLPreprocessor.h/.cpp` | 200 |
| SPIRVToHLSL | `src/gpu/ue/tools/SPIRVToHLSL.h/.cpp` | 250 |
| ShaderVariantEnumerator | `src/gpu/ue/tools/ShaderVariantEnumerator.h/.cpp` | 800 |
| VariantFileWriter | `src/gpu/ue/tools/VariantFileWriter.h/.cpp` | 400 |
| Generator main | `src/gpu/ue/tools/TgfxShaderVariantGenerator.cpp` | 100 |
| FGlobalShader 声明/实现 | `TgfxShaders.h/.cpp` | 600 |
| .ush/.usf 模板 | `TgfxCommon.ush`, `TgfxUniformBridge.ush` | 100 |
| ShaderVariantRegistry | `src/gpu/ue/ShaderVariantRegistry.h/.cpp` | 150 |
| UERHIGPU | `src/gpu/ue/UERHIGPU.h/.cpp` | 400 |
| UERHIRenderPass | `src/gpu/ue/UERHIRenderPass.h/.cpp` | 600 |
| UERHIProgram | `src/gpu/ue/UERHIProgram.h/.cpp` | 300 |
| UE RHI Resources | `src/gpu/ue/UERHIGPUBuffer.h`, `UERHITexture.h`, `UERHISampler.h` | 300 |
| State Mapping | `src/gpu/ue/UERHIStateMapping.h/.cpp` | 300 |
| ProgramInfo UE 路径修改 | `src/gpu/ProgramInfo.cpp` (修改) | 50 |
| 单元测试 | `test/src/UEShaderVariantTest.cpp` | 500 |
| 渲染对比测试 | `test/src/UERHIRenderTest.cpp` | 400 |
| **总计** | | **~5750** |