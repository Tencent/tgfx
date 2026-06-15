# TGFX 构建期 Shader 翻译方案

## 一、问题定义

### 1.1 当前运行时翻译链路

TGFX 对 Vulkan、Metal、WebGPU 三个后端采用统一的"GLSL 先行"架构：所有 Processor 的 `emitCode()` 生成 GLSL 450 源码，然后在运行时逐级翻译为各后端的目标着色器语言，最后交给 GPU 驱动创建着色器对象。完整链路如下：

```
Canvas API → DrawOp → Processor 组合
  → ProgramBuilder::emitAndInstallProcessors() → GLSL 450 源码 (VS + FS)
  → PreprocessGLSL()     — 添加 binding/location 限定符          [< 1ms]
  → CompileGLSLToSPIRV() — shaderc 编译 GLSL → SPIR-V binary   [~15-30ms]
  → 后端特定翻译:
      Vulkan:  无需额外翻译，直接使用 SPIR-V
      Metal:   spirv-cross 将 SPIR-V → MSL source               [~5-10ms]
      WebGPU:  tint 将 SPIR-V → WGSL source                     [~5-10ms]
  → GPU 驱动创建着色器对象:
      Vulkan:  vkCreateShaderModule(SPIR-V)                      [~1ms]
      Metal:   [MTLDevice newLibraryWithSource:MSL]               [~3-5ms]
      WebGPU:  wgpuDeviceCreateShaderModule(WGSL)                 [~3-5ms]
```

单个 shader（VS 或 FS）的翻译耗时约 20-40ms，其中 shaderc 编译占 60% 以上。一个 Program 包含 VS + FS 两个 shader，首次创建耗时约 40-80ms。

### 1.2 性能影响

首次绘制时，应用通常触发 5-15 个不同的 Program 变体（纯色、图片、文本、渐变、模糊等），导致首帧总延迟 200-600ms。这段时间完全被 CPU 端的着色器语言翻译占据，GPU 处于空闲等待状态。

### 1.3 目标

**在构建期完成 Processor → GLSL → SPIR-V → MSL/WGSL 的全链路语言翻译**，产出各后端可直接消费的着色器源码或字节码。运行时只需将翻译产物传给 GPU API 创建着色器对象（1-5ms），完全跳过 shaderc/spirv-cross/tint 的重量级翻译。

### 1.4 边界：本方案不涉及的内容

- **GPU 驱动编译**：`vkCreateShaderModule`、`newLibraryWithSource`、`wgpuDeviceCreateShaderModule` 等调用是 GPU 驱动将中间表示编译为硬件微码的过程，必须在有真实设备的运行时执行，不在本方案范围内。
- **Pipeline 预创建**：`createRenderPipeline` 依赖运行时的 viewport、blend state 等参数，本方案不改变其流程。
- **VkPipelineCache / MTLBinaryArchive**：这些是缓存 GPU 驱动编译结果的机制，属于独立的优化方向，与本方案解耦。

---

## 二、可行性分析

### 2.1 Shader 变体的确定性

TGFX 的 shader 由 Processor 的 `emitCode()` 动态拼接，表面上看是"运行时决定"的。但分析代码后发现以下关键事实：

**Processor 种类有限且编译时完全已知。** 代码库中共有 37 个 Processor（通过 `DEFINE_PROCESSOR_CLASS_ID` 宏声明），分为三类：

| 类别 | 数量 | 代表性 Processor |
|------|:----:|-----------------|
| GeometryProcessor | 12 | QuadPerEdgeAA, AtlasText, Ellipse, ShapeInstanced |
| FragmentProcessor | 23 | TextureEffect, ConstColor, GaussianBlur1D, 渐变系列 |
| XferProcessor | 2 | EmptyXP, PorterDuffXP |

**每个 Processor 的 `emitCode()` 是确定性函数。** 给定相同的 key 参数（即 `onComputeProcessorKey` 写入 `BytesKey` 的那些字段），`emitCode()` 一定产出相同的 GLSL 代码。例如 `QuadPerEdgeAAGeometryProcessor` 的 key 由 5 个布尔标志组成（`aa`、`hasColor`、`hasUV`、`hasSubset`、`hasSubsetMatrix`），最多 32 种变体。

**Processor 的组合方式由 DrawOp 和 OpsCompositor 决定。** 8 种 DrawOp（`RectDrawOp`、`RRectDrawOp`、`ShapeDrawOp`、`AtlasTextOp`、`ShapeInstancedDrawOp`、`MeshDrawOp`、`HairlineLineOp`、`HairlineQuadOp`）各自固定使用特定的 GP 类型。FP 链路由 `OpsCompositor::addDrawOp()` 根据 `Brush` 的 shader / colorFilter / maskFilter 以及 clip 状态组装，路径在源码中可枚举。

**结论**：虽然理论上 Processor 组合空间无界，但 Canvas API 的使用模式将其约束为有限集。预估高频变体 100-200 种，可以在构建期穷举。

### 2.2 GLSL 生成阶段的运行时依赖分析

构建期翻译的前提是：GLSL 生成阶段（`ProgramBuilder::emitAndInstallProcessors()`）的所有依赖都能在无 GPU 设备的环境下满足。

逐一追踪 `emitCode()` 路径中对 `Context` / `GPU` / `Texture` 等运行时对象的实际使用：

| 依赖对象 | 使用方式 | 使用位置 | 是否需要真实 GPU？ |
|---------|---------|---------|:-:|
| `ShaderCaps*` | 读取 `versionDeclString`、`usesPrecisionModifiers`、`frameBufferFetch*`、`requiresUniformControlFlow`、`maxFragmentSamplers` 等字段 | ShaderBuilder、GLSLFragmentShaderBuilder、GLSLProgramBuilder、多个 Processor 的 EmitArgs | ❌ 纯数据结构，可按后端预设 |
| `Context::backend()` | 仅 `ProgramInfo.cpp:100` 的 WebGPU Y-flip 特殊处理 | GetRTAdjustArray() | ❌ 可从 ShaderCaps 或 Backend 枚举获取 |
| `Texture*`（通过 `addSampler`）| 读取 `texture->type()`（决定 sampler 的 UniformFormat）和 `texture->format()`（决定采样 Swizzle）| UniformHandler::addSampler() | ❌ 只需 TextureType + PixelFormat 两个枚举值 |
| `RenderTarget*` | 读取 `format()`、`sampleCount()`、`origin()`、`width()`/`height()` | ProgramInfo 的 key 计算和 RTAdjust | ❌ 只需 format 和 sampleCount 参与 key；width/height/origin 只影响 uniform 值，不影响 GLSL 代码 |
| `Context::gpu()` | `GLSLProgramBuilder::finalize()` 调用 `gpu->createShaderModule()` 和 `gpu->createRenderPipeline()` | finalize() | ❌ 构建期只需 GLSL 文本，不调用 finalize() |
| `TextureProxy*` | 通过 `getTextureView()->getTexture()` 获取 type/format；`isAlphaOnly()` | AtlasTextGeometryProcessor、TextureEffect 等 | ❌ 可通过 TextureSamplerDesc mock |

**结论**：GLSL 生成阶段的所有依赖均为纯数据查询。通过预设 `ShaderCaps` 和轻量级 `TextureSamplerDesc`，可以在无 GPU 设备的环境下完整执行 GLSL 生成。

### 2.3 翻译工具链的可用性

TGFX 已集成完整的跨语言翻译工具链，均为纯 CPU 库：

| 工具 | 用途 | 源码位置 |
|------|------|---------|
| `shaderc` | GLSL → SPIR-V | `ShaderCompiler.cpp`、`WebGPUShaderModule.cpp` |
| `spirv-cross` | SPIR-V → MSL | `MetalShaderModule.mm` |
| `tint` | SPIR-V → WGSL | `WebGPUShaderModule.cpp` |

这些工具在构建期可直接复用，无需额外引入依赖。

### 2.4 业界参照

| 引擎 | 做法 | 构建期产物 | 运行时开销 |
|------|------|-----------|:---------:|
| UE5 | Cook 阶段 Shader Compiler Worker 遍历排列组合 | `.ushaderbytecode` | 零翻译 |
| Unity | Build 阶段编译 `multi_compile` 变体 | `.shaderbundle` | 零翻译 |
| bgfx | `shaderc` CLI 将 `.sc` 文件编译为各平台二进制 | `.bin` | 零翻译 |
| Skia/Flutter | 运行时动态编译 + `.sksl.json` 预热 | 无 | 仍有首帧卡顿 |

TGFX 应对标 UE5/Unity/bgfx 的构建期翻译模式。与它们的区别仅在于变体枚举方式——TGFX 枚举的是 Processor 组合，而非 `#define` 排列组合或手写 shader 文件。

---

## 三、技术障碍

### 3.1 ClassID 运行时分配导致跨 binary 不一致

#### 问题

当前 `DEFINE_PROCESSOR_CLASS_ID` 宏使用运行时全局递增计数器分配 ClassID：

```cpp
// src/gpu/processors/Processor.h:27-31
#define DEFINE_PROCESSOR_CLASS_ID               \
  static uint32_t ClassID() {                   \
    static uint32_t ClassID = UniqueID::Next(); \
    return ClassID;                             \
  }
```

`UniqueID::Next()` 的返回值取决于各 Processor 的 `ClassID()` 首次调用顺序。构建工具和 App 是两个不同的 binary，static 初始化顺序不同，导致同一个 Processor 在两边获得不同的 ClassID 数值。由于 ClassID 是 `BytesKey` 的组成部分（`computeProcessorKey` 的首个 `write` 调用），两边的 key 将不匹配——构建期存入 Bundle 的 key 在运行时查不到。

#### 影响

这是构建期翻译的**硬性前置条件**。不解决此问题，构建期和运行时无法共享同一个 key 空间。

#### 解法

将 ClassID 改为编译期确定性 hash，使其值仅取决于 Processor 的类名字符串，与 binary 无关：

```cpp
namespace detail {
constexpr uint32_t FNV1a(const char* str, uint32_t hash = 2166136261u) {
  return *str ? FNV1a(str + 1, (hash ^ static_cast<uint32_t>(*str)) * 16777619u) : hash;
}
}  // namespace detail

#define DEFINE_PROCESSOR_CLASS_ID                                          \
  static constexpr uint32_t ClassID() {                                   \
    return ::tgfx::detail::FNV1a(__FILE_NAME__ ":" TGFX_STRINGIFY(__LINE__)); \
  }
```

**备选方案（更稳定）**：使用类名字符串 hash，不依赖 `__FILE_NAME__` 和 `__LINE__`：

```cpp
#define DEFINE_PROCESSOR_CLASS_ID_NAMED(name)                              \
  static constexpr uint32_t ClassID() {                                   \
    return ::tgfx::detail::FNV1a(#name);                                  \
  }
```

37 个 Processor 各不相同的类名保证 hash 无冲突，可通过编译期 `static_assert` 验证。

#### 影响范围

- `src/gpu/processors/Processor.h`：宏定义修改
- 37 个 Processor 的 `.h` 文件：适配新宏
- `Processor` 构造函数：`_classID` 改为 `constexpr`

### 3.2 ProgramBuilder 对 Context 的耦合

#### 问题

`ProgramBuilder` 持有 `Context* context` 成员。GLSL 生成过程中，`emitCode()` 路径通过 `context->shaderCaps()` 访问后端能力信息（约 25 处引用，分布在 9 个文件中）。构建期没有真实的 `Context` 对象。

各引用点的完整清单：

| 文件 | 行号 | 用途 |
|------|------|------|
| `GLSLProgramBuilder.cpp` | 128, 152, 229 | `getShaderVarDeclarations`、`getUniformBlockDeclaration`、`checkSamplerCounts` 中读取 `usesPrecisionModifiers`、`maxFragmentSamplers` |
| `ProgramBuilder.cpp` | 71 | `emitAndInstallGeoProc` 中将 `shaderCaps` 传入 `EmitArgs` |
| `ShaderBuilder.cpp` | 48 | `versionDeclString` |
| `GLSLVertexShaderBuilder.cpp` | 26 | `usesPrecisionModifiers` |
| `GLSLFragmentShaderBuilder.cpp` | 29, 36-49 | `usesPrecisionModifiers`、`frameBufferFetch*` |
| `GLSLTiledTextureEffect.cpp` | 4 处 | `requiresUniformControlFlow` |
| `GLSLClampedGradientEffect.cpp` | 1 处 | `requiresUniformControlFlow` |
| `ProgramInfo.cpp` | 100 | `context->backend()` 判断 WebGPU Y-flip |

#### 解法

将 GLSL 生成阶段的依赖从 `Context*` 收窄为 `const ShaderCaps*`：

1. `ProgramBuilder` 新增 `const ShaderCaps* shaderCaps_` 成员，构造时从 `context->shaderCaps()` 赋值。
2. 所有 `context->shaderCaps()` / `getContext()->shaderCaps()` 改为使用 `shaderCaps_` 访问器。
3. 对于 `context->backend()`（仅 1 处），将 `Backend` 信息存入 `ShaderCaps` 或通过独立参数传入。
4. 构建期工具不传 `Context*`，直接传预设好的 `ShaderCaps` 对象。

`ShaderCaps` 已有的字段完整覆盖了 `emitCode()` 的所有需求：

```cpp
// src/gpu/ShaderCaps.h — 现有字段
std::string versionDeclString;
bool usesPrecisionModifiers;
bool frameBufferFetchSupport;
bool frameBufferFetchNeedsCustomOutput;
std::string frameBufferFetchColorName;
std::string frameBufferFetchExtensionString;
bool frameBufferFetchUsesSubpassInput;
int maxFragmentSamplers;
int maxUBOSize;
int uboOffsetAlignment;
bool requiresUniformControlFlow;
```

为构建期使用，新增一个不依赖 `GPU*` 的工厂方法：

```cpp
static ShaderCaps MakeForBackend(Backend backend) {
  ShaderCaps caps;
  // 根据后端类型设置各字段的标准值
  // Metal: frameBufferFetchSupport=true, frameBufferFetchUsesSubpassInput=true
  // WebGPU: requiresUniformControlFlow=true, frameBufferFetchUsesSubpassInput=true
  // Vulkan: 默认配置
  return caps;
}
```

#### 影响范围

- `ProgramBuilder.h/.cpp`：新增 `shaderCaps_` 成员和访问器
- `ShaderCaps.h/.cpp`：新增无参构造 + 工厂方法
- 9 个引用 `context->shaderCaps()` 的文件：改为使用 `shaderCaps_`

### 3.3 addSampler 对 Texture 对象的依赖

#### 问题

`UniformHandler::addSampler` 接受 `std::shared_ptr<Texture>` 参数，从中读取两个属性：

```cpp
// src/gpu/UniformHandler.cpp:40-61
SamplerHandle UniformHandler::addSampler(std::shared_ptr<Texture> texture,
                                         const std::string& name) {
  UniformFormat format;
  switch (texture->type()) {           // → TextureType 枚举
    case TextureType::External:
      format = UniformFormat::TextureExternalSampler;
      break;
    case TextureType::Rectangle:
      format = UniformFormat::Texture2DRectSampler;
      break;
    default:
      format = UniformFormat::Texture2DSampler;
      break;
  }
  auto swizzle = Swizzle::ForRead(texture->format());  // → PixelFormat 枚举
  // ...
}
```

此外，`ProgramBuilder::emitAndInstallFragProc` 中通过 `subFP->textureAt(i)` 获取 `shared_ptr<Texture>` 再传入 `emitSampler`：

```cpp
// src/gpu/ProgramBuilder.cpp:117-122
while (const auto subFP = fpIter.next()) {
  for (size_t i = 0; i < subFP->numTextureSamplers(); ++i) {
    auto texture = subFP->textureAt(i);
    texSamplers.emplace_back(emitSampler(texture, name));
  }
}
```

构建期没有真实的 `Texture` 对象。

#### 解法

引入轻量级描述结构，替代 `shared_ptr<Texture>` 作为 `addSampler` 的参数：

```cpp
struct TextureSamplerDesc {
  TextureType type = TextureType::TwoD;
  PixelFormat format = PixelFormat::RGBA_8888;
};
```

`addSampler` 签名改为：

```cpp
SamplerHandle addSampler(const TextureSamplerDesc& desc, const std::string& name);
```

运行时从真实 Texture 构造 `TextureSamplerDesc`：

```cpp
TextureSamplerDesc desc{texture->type(), texture->format()};
```

构建期从变体描述直接构造。

相应地，`Processor` 的 `textureAt()` 在构建期返回的不再是真实 Texture，而是 `TextureSamplerDesc`。需要调整 `ProgramBuilder::emitSampler` 和 `emitAndInstallXferProc` 中的 `dstTextureView->getTexture()` 调用。

#### 影响范围

- `UniformHandler.h/.cpp`：`addSampler` 参数类型变更
- `ProgramBuilder.cpp`：`emitSampler` 适配
- 3 个直接调用 `addSampler` 的 GLSL Processor 实现

### 3.4 computeProcessorKey 中的纹理 key

#### 问题

`FragmentProcessor::computeProcessorKey` 调用 `TextureView::ComputeTextureKey` 将纹理信息写入 key：

```cpp
// src/gpu/processors/FragmentProcessor.cpp:85-95
void FragmentProcessor::computeProcessorKey(Context* context, BytesKey* bytesKey) const {
  bytesKey->write(classID());
  onComputeProcessorKey(bytesKey);
  auto textureSamplerCount = onCountTextureSamplers();
  for (size_t i = 0; i < textureSamplerCount; ++i) {
    TextureView::ComputeTextureKey(textureAt(i), bytesKey);
  }
  for (const auto& childProcessor : childProcessors) {
    childProcessor->computeProcessorKey(context, bytesKey);
  }
}
```

构建期没有真实 `Texture` 和 `TextureView` 对象。需要确保构建期计算出的 key 与运行时一致。

#### 解法

`TextureView::ComputeTextureKey` 实际上只写入 `TextureType` 和 `PixelFormat` 信息。构建期可以直接用相同的 key 计算逻辑，传入 `TextureSamplerDesc` 而非真实 Texture。

需要提供一个不依赖 `shared_ptr<Texture>` 的 `ComputeTextureKey` 重载或独立函数，接受 `TextureType` 和 `PixelFormat` 即可。

### 3.5 Program key 中的 RenderTarget 信息

#### 问题

`ProgramInfo::getProgram()` 计算 program key 时写入了 RenderTarget 的属性：

```cpp
// src/gpu/ProgramInfo.cpp:124-151
BytesKey programKey = {};
geometryProcessor->computeProcessorKey(context, &programKey);
for (...) processor->computeProcessorKey(context, &programKey);
if (xferProcessor) xferProcessor->computeProcessorKey(context, &programKey);
programKey.write(static_cast<uint32_t>(blendMode));
programKey.write(static_cast<uint32_t>(getOutputSwizzle().asKey()));  // ← 依赖 RT format
programKey.write(static_cast<uint32_t>(cullMode));
programKey.write(static_cast<uint32_t>(renderTarget->format()));     // ← 依赖 RT format
programKey.write(static_cast<uint32_t>(renderTarget->sampleCount())); // ← 依赖 RT sampleCount
```

其中 `getOutputSwizzle()` 通过 `Swizzle::ForWrite(renderTarget->format())` 计算，仅依赖 `PixelFormat` 枚举。`sampleCount` 只有 1 和 4 两种常见取值。

#### 解法

构建期枚举变体时，将 `PixelFormat`（决定 output swizzle 和 format key）和 `sampleCount` 作为变体描述的一部分。这些不需要真实 RenderTarget 对象，只需枚举值。

高频组合非常有限：

| PixelFormat | Output Swizzle | 常见场景 |
|-------------|---------------|---------|
| RGBA_8888 | RGBA | 大多数场景 |
| BGRA_8888 | RGBA | macOS/iOS 默认 swap chain |
| ALPHA_8 | AAAA | 覆盖率 mask |

sampleCount 通常为 1（无 MSAA），少数场景为 4。

---

## 四、整体架构

### 4.1 构建期翻译流程

```
┌──────────────────────────────────────────────────────────────────────────┐
│                       构建期 (CI / 开发机)                                │
│                                                                          │
│  1. ShaderVariantEnumerator                                              │
│     枚举所有高频 Processor 组合 + pipeline state → VariantDesc[]          │
│                                                                          │
│  2. 对每个 VariantDesc:                                                   │
│     a. 构造 Processor 对象 (用预设 ShaderCaps + TextureSamplerDesc)       │
│     b. computeProgramKey() → BytesKey                                    │
│     c. ProgramBuilder::emitAndInstallProcessors() → VS/FS GLSL 源码      │
│     d. 执行翻译链 (按目标后端):                                            │
│        Vulkan:  PreprocessGLSL → shaderc → SPIR-V binary                │
│        Metal:   PreprocessGLSL → shaderc → SPIR-V → spirv-cross → MSL   │
│        WebGPU:  preprocessGLSL → shaderc → SPIR-V → tint → WGSL         │
│     e. 将 {BytesKey, VS产物, FS产物} 写入 Bundle                          │
│                                                                          │
│  3. 输出 ShaderBundle 文件 (每个后端一个)                                   │
│     shaders_vulkan.bundle   — 包含 SPIR-V binary                        │
│     shaders_metal.bundle    — 包含 MSL source text                       │
│     shaders_webgpu.bundle   — 包含 WGSL source text                      │
└──────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ 随应用安装包分发
                                    ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                       运行时 (用户设备)                                    │
│                                                                          │
│  ProgramInfo::getProgram()                                               │
│    1. computeProgramKey() → BytesKey                                     │
│    2. GlobalCache::findProgram(key)                                      │
│       命中 → 返回 Program (0ms)                                           │
│    3. [新增] ShaderBundle::find(key)                                     │
│       命中 → 从预翻译产物创建 ShaderModule                                 │
│              Vulkan:  vkCreateShaderModule(SPIR-V)          ~1ms         │
│              Metal:   [MTLDevice newLibraryWithSource:MSL]   ~3-5ms      │
│              WebGPU:  wgpuDeviceCreateShaderModule(WGSL)     ~3-5ms      │
│            → 继续创建 RenderPipeline → 返回 Program                       │
│    4. Bundle 未命中 → 完整动态翻译 (~40ms, 兜底路径)                        │
└──────────────────────────────────────────────────────────────────────────┘
```

### 4.2 运行时改造点

改造集中在 `GLSLProgramBuilder::finalize()`。当前流程：

```cpp
// src/gpu/glsl/GLSLProgramBuilder.cpp:167-226 — 现有 finalize()
std::shared_ptr<Program> GLSLProgramBuilder::finalize() {
  fragmentShaderBuilder()->declareCustomOutputColor();
  finalizeShaders();
  auto gpu = context->gpu();
  // ① 从 GLSL 创建 ShaderModule（内部执行完整翻译链）
  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = vertexShaderBuilder()->shaderString();
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = gpu->createShaderModule(vertexModule);
  // ...同理创建 fragmentShader...
  // ② 创建 RenderPipeline
  auto pipeline = gpu->createRenderPipeline(descriptor);
  return std::make_shared<Program>(...);
}
```

改造后，在步骤 ① 前插入 Bundle 查找逻辑。若命中，直接从预翻译产物创建 ShaderModule，跳过 `createShaderModule(GLSL)` 内部的翻译链：

```
finalize()
  → emitCode → finalizeShaders → 生成 GLSL 文本
  → 查找 ShaderBundle
    → 命中: 从 Bundle 中的 SPIR-V/MSL/WGSL 创建 ShaderModule (跳过翻译)
    → 未命中: 走现有 createShaderModule(GLSL) 路径 (兜底)
  → 创建 RenderPipeline (不变)
```

**注意**：即使 Bundle 命中，仍需执行 `emitAndInstallProcessors()` 以获取 uniform 布局信息（`UniformData`、`VertexBufferLayout` 等），因为这些信息不仅影响 shader 代码，还用于组装 `RenderPipelineDescriptor`。跳过的仅是 ShaderModule 创建中的翻译步骤。

### 4.3 与现有缓存的关系

```
┌─────────────────────────────────────────────────────────────┐
│  ProgramInfo::getProgram()                                   │
│                                                              │
│  第一级: GlobalCache (内存 LRU, 已有, ≤256 个 Program)        │
│    命中 → 返回完整 Program 对象 (包含 Pipeline + UniformData)  │
│    开销: 0ms                                                  │
│                                                              │
│  第二级: ShaderBundle (只读, 随包分发, 本方案新增)              │
│    命中 → 从预翻译产物创建 ShaderModule + 组装 Pipeline         │
│    开销: ~5ms (GPU 对象创建)                                   │
│                                                              │
│  第三级: 动态翻译 (现有路径, 兜底)                              │
│    处理 Bundle 未覆盖的变体                                    │
│    开销: ~40-80ms (完整翻译链)                                 │
└─────────────────────────────────────────────────────────────┘
```

ShaderBundle 是第一级（内存缓存）和第三级（动态翻译）之间的新层。它不存储完整 Program 对象（那需要 Pipeline，而 Pipeline 依赖运行时参数），只存储翻译后的着色器源码/字节码。

---

## 五、变体枚举

### 5.1 枚举策略

从 DrawOp 子类反推 GP 类型和参数，结合 OpsCompositor 的 FP 链路组装逻辑，枚举高频变体。

#### 5.1.1 GeometryProcessor 变体

| GP 类型 | Key 参数 | 高频变体数 |
|---------|---------|:---------:|
| QuadPerEdgeAAGeometryProcessor | aa(2) × hasColor(2) × hasUV(2) × hasSubset(2) × hasSubsetMatrix(取决于hasSubset) | ~12 |
| AtlasTextGeometryProcessor | aa(2) × hasColor(2) × isAlphaOnly(2) | ~4 |
| DefaultGeometryProcessor | aa(2) | 2 |
| EllipseGeometryProcessor | stroke(2) × hasColor(2) | ~4 |
| ComplexEllipseGeometryProcessor | stroke(2) × hasColor(2) | ~4 |
| NonAARRectGeometryProcessor | hasColor(2) × stroke(2) | ~4 |
| ComplexNonAARRectGeometryProcessor | hasColor(2) × stroke(2) | ~4 |
| RoundStrokeRectGeometryProcessor | aa(2) × hasColor(2) × hasUV(2) | ~4 |
| ShapeInstancedGeometryProcessor | hasColors(2) × aa(2) | ~4 |
| MeshGeometryProcessor | hasTexCoords(2) × hasColors(2) × hasCoverage(2) | ~4 |
| HairlineLineGeometryProcessor | aa(2) | 2 |
| HairlineQuadGeometryProcessor | aa(2) | 2 |
| **GP 小计** | | **~50** |

#### 5.1.2 FragmentProcessor 链路变体

| FP 链路模式 | Key 参数 | 高频变体数 |
|------------|---------|:---------:|
| ConstColor（3 种 InputMode） | Ignore / ModulateRGBA / ModulateA | 3 |
| TextureEffect（非 YUV）| alphaOnly(2) × hasSubset(2) × perspective(2) | ~4 |
| TextureEffect（YUV）| I420/NV12 × colorRange(2) | ~4 |
| TiledTextureEffect | alphaOnly(2) × shaderModeX/Y(3×3) × perspective(2) | ~6 |
| LinearGradientLayout + Colorizer + ClampedGradient | intervals(2/4/8) | 3 |
| RadialGradientLayout + Colorizer + ClampedGradient | intervals(2/4/8) | 3 |
| ConicGradientLayout + Colorizer + ClampedGradient | intervals(2/4/8) | 3 |
| GaussianBlur1DFragmentProcessor | maxSigma 档位(~4) | 4 |
| ColorMatrixFragmentProcessor | 无 key 参数 | 1 |
| AARectEffect | 无 key 参数 | 1 |
| XfermodeFragmentProcessor | mode(DstIn/SrcIn/SrcOut) × child 类型(3) | ~5 |
| ComposeFragmentProcessor | 组合其 child 的 key | ~3 |
| DeviceSpaceTextureEffect | 无 key 参数 | 1 |
| **FP 小计** | | **~41** |

#### 5.1.3 XferProcessor 变体

| XP 类型 | Key 参数 | 变体数 |
|---------|---------|:------:|
| EmptyXferProcessor | 无 | 1 |
| PorterDuffXferProcessor | blendMode × 是否有 dstTexture | ~6 |
| **XP 小计** | | **~7** |

#### 5.1.4 Pipeline State 变体

| 参数 | 高频取值 | 说明 |
|------|---------|------|
| BlendMode | SrcOver (绝大多数) | 其他 mode 通过 PorterDuffXP 或硬件混合实现 |
| Output Swizzle | RGBA (RGBA_8888), RGBA (BGRA_8888) | 由 RT format 决定 |
| CullMode | None (2D 绑定不裁剪) | — |
| PixelFormat | RGBA_8888, BGRA_8888 | — |
| SampleCount | 1 | MSAA 少用 |

#### 5.1.5 组合估算

变体总数不是各部分的笛卡尔积——实际有效组合由 DrawOp 的代码路径约束。例如 `AtlasTextOp` 只使用 `AtlasTextGeometryProcessor` + `ConstColor(ModulateA)` 的 FP 链路，不会出现 `AtlasText + TextureEffect` 的组合。

根据 `OpsCompositor::addDrawOp` 的代码路径分析，高频组合约 **100-200 种**。这是一个可管理的数量级，每个变体的 GLSL 文本约 1-3KB，翻译后产物（SPIR-V/MSL/WGSL）约 2-5KB，Bundle 总大小约 200KB-1MB。

### 5.2 枚举的验证与补漏

构建期枚举无法保证 100% 覆盖所有运行时实际出现的变体（业务方可能使用少见的 Processor 参数组合）。通过以下机制持续补漏：

1. **运行时插桩统计**：在 `ProgramInfo::getProgram()` 中记录每个 `BytesKey` 是 Bundle 命中还是未命中。内测阶段收集 miss 的 key 特征。
2. **反序列化 miss key**：将 miss 的 BytesKey 反解为 Processor 组合描述，补充到枚举清单。
3. **CI 自动更新**：变体清单变更后 CI 自动重新生成 Bundle。

---

## 六、Bundle 格式设计

### 6.1 文件结构

```
Offset  Size     Description
────────────────────────────────────────────────
0x00    8 bytes  Magic: "TGFXSHDR"
0x08    4 bytes  Bundle version (uint32)
0x0C    4 bytes  Backend enum (uint32): Vulkan=0, Metal=1, WebGPU=2
0x10    4 bytes  Entry count N (uint32)
0x14    4 bytes  Reserved (alignment padding)
────────────────────────────────────────────────
0x18    N × 32   Entry table
                 Each entry (32 bytes):
                   BytesKey hash   (16 bytes, 128-bit)
                   VS data offset  (4 bytes, uint32)
                   VS data size    (4 bytes, uint32)
                   FS data offset  (4 bytes, uint32)
                   FS data size    (4 bytes, uint32)
────────────────────────────────────────────────
...              Data section
                 VS/FS shader data (SPIR-V binary / MSL text / WGSL text)
                 按 entry 顺序排列
────────────────────────────────────────────────
```

### 6.2 Key 设计

Bundle 的查找 key 应与 `ProgramInfo::getProgram()` 中计算的 `BytesKey` 完全一致。由于 `BytesKey` 是变长字节序列，在 Bundle entry table 中存储其 128-bit hash（如 CityHash128 或直接截断/填充到固定长度）。

运行时查找流程：
1. 计算 `BytesKey` 的 128-bit hash
2. 在 entry table 中二分查找（entry 按 hash 排序存储）
3. 命中后，通过 offset/size 读取 VS 和 FS 的翻译产物

### 6.3 版本管理

`Bundle version` 字段用于版本校验。任何影响 GLSL 生成的代码变更（Processor `emitCode()` 修改、`ShaderBuilder` 逻辑变更、`PreprocessGLSL` 规则变更）都应递增此版本号。运行时加载 Bundle 时校验版本号，不匹配则忽略整个 Bundle，回退到动态翻译。

---

## 七、各后端运行时加载

### 7.1 Vulkan

Bundle 中存储 SPIR-V binary。运行时跳过 `PreprocessGLSL` + `CompileGLSLToSPIRV`，直接调用 `vkCreateShaderModule`：

```
现有路径: GLSL → PreprocessGLSL → CompileGLSLToSPIRV(shaderc) → vkCreateShaderModule
                ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                                ~20-30ms (跳过)

Bundle 路径: Bundle[SPIR-V] → vkCreateShaderModule
                                    ~1ms
```

### 7.2 Metal

Bundle 中存储 MSL source text。运行时跳过 `PreprocessGLSL` + `CompileGLSLToSPIRV` + `convertSPIRVToMSL(spirv-cross)`，直接调用 `[MTLDevice newLibraryWithSource:]`：

```
现有路径: GLSL → PreprocessGLSL → CompileGLSLToSPIRV → convertSPIRVToMSL → newLibraryWithSource
                ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                                    ~30-40ms (跳过)

Bundle 路径: Bundle[MSL] → newLibraryWithSource
                               ~3-5ms
```

### 7.3 WebGPU

Bundle 中存储 WGSL source text。运行时跳过 `preprocessGLSL` + `compileGLSLToSPIRV` + `tint SPIR-V→WGSL`，直接调用 `wgpuDeviceCreateShaderModule`：

```
现有路径: GLSL → preprocessGLSL → compileGLSLToSPIRV → tint → wgpuDeviceCreateShaderModule
                ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                              ~30-40ms (跳过)

Bundle 路径: Bundle[WGSL] → wgpuDeviceCreateShaderModule
                                     ~3-5ms
```

### 7.4 OpenGL

OpenGL 后端直接接受 GLSL，驱动自行编译，无 shaderc/spirv-cross/tint 参与。当前路径已足够快，无需构建期翻译。

---

## 八、代码改动总览

### 8.1 修改现有文件

| 文件 | 改动描述 | 估计行数 |
|------|---------|:-------:|
| `src/gpu/processors/Processor.h` | ClassID 宏改为编译期 hash | ~10 |
| 37 个 Processor `.h` 文件 | 适配新的 ClassID 宏 | 各 1 行 |
| `src/gpu/ShaderCaps.h/.cpp` | 新增无参构造 + `MakeForBackend()` 工厂 | ~40 |
| `src/gpu/ProgramBuilder.h/.cpp` | 新增 `shaderCaps_` 成员；GLSL 生成解耦 Context | ~20 |
| `src/gpu/UniformHandler.h/.cpp` | `addSampler` 参数改为 `TextureSamplerDesc` | ~20 |
| `src/gpu/glsl/GLSLProgramBuilder.cpp` | `finalize()` 增加 Bundle 查找逻辑 | ~30 |
| 9 个引用 `context->shaderCaps()` 的文件 | 改用 `shaderCaps_` 访问器 | 各 1-3 行 |
| 各后端 ShaderModule (`Vulkan`/`Metal`/`WebGPU`) | 新增从预翻译产物直接创建的路径 | 各 ~20 |

### 8.2 新增文件

| 文件 | 职责 | 估计行数 |
|------|------|:-------:|
| `src/gpu/ShaderBundle.h/.cpp` | Bundle 加载与查找 | ~200 |
| `tools/shader_precompile/main.cpp` | CLI 入口 | ~50 |
| `tools/shader_precompile/ShaderVariantEnumerator.h/.cpp` | 变体枚举 | ~300 |
| `tools/shader_precompile/ShaderPrecompiler.h/.cpp` | 离线翻译 + Bundle 生成 | ~200 |
| `tools/shader_precompile/CMakeLists.txt` | 构建工具的编译脚本 | ~30 |

### 8.3 不改动的部分

| 模块 | 原因 |
|------|------|
| OpenGL 后端 | 不需要构建期翻译 |
| `createRenderPipeline` | Pipeline 创建流程不变 |
| `ProgramInfo::setUniformsAndSamplers` | uniform 设值不变 |
| `GlobalCache` | 内存缓存逻辑不变 |
| 所有 Processor 的 `emitCode()` | GLSL 生成逻辑不变 |
| 所有 Processor 的 `setData()` | uniform 值设置不变 |

---

## 九、实施计划

### Phase 1：基础设施改造

**目标**：消除构建期翻译的技术前置障碍，不改变运行时行为。

- ClassID 改为编译期确定性 hash
- ProgramBuilder 的 ShaderCaps 依赖解耦
- addSampler 接口改造为 TextureSamplerDesc
- 全量测试通过，确保零功能回归

### Phase 2：离线翻译工具

**目标**：构建一个 CLI 工具，能枚举变体并产出 ShaderBundle。

- 实现 ShaderCaps::MakeForBackend() 工厂
- 实现 ShaderVariantEnumerator（初始版本 ~50 变体）
- 实现 ShaderPrecompiler（复用现有 ShaderCompiler / spirv-cross / tint）
- 实现 ShaderBundle 写入器
- 验证 Bundle 中的 key 和 GLSL 与运行时一致

### Phase 3：运行时加载

**目标**：运行时从 Bundle 加载预翻译产物。

- 实现 ShaderBundle 加载器与查找
- 各后端 ShaderModule 增加从预翻译产物创建的路径
- 改造 GLSLProgramBuilder::finalize() 插入 Bundle 查找
- 端到端验证：Bundle 路径与动态翻译路径的渲染结果一致

### Phase 4：集成与验证

**目标**：CI 集成，验证覆盖率和性能提升。

- CMake 集成 Bundle 构建规则
- CI 自动生成 Bundle 并随构建产物分发
- 运行时插桩统计 Bundle 命中率
- 补充变体清单，目标命中率 ≥90%

---

## 十、风险评估

| 风险 | 概率 | 影响 | 缓解措施 |
|------|:----:|:----:|---------|
| ClassID hash 冲突 | 极低 | 高（key 错误匹配） | 编译期 static_assert 检查所有 ClassID 互不相同 |
| 变体覆盖不足 (<80%) | 中 | 中（部分首帧仍需动态翻译） | 兜底路径保证正确性；持续收集 miss 数据补充变体 |
| Bundle 版本过时 | 低 | 高（加载过时的翻译产物导致渲染错误） | CI 自动生成 + Bundle version 校验 |
| emitCode 改造引入回归 | 低 | 高 | Phase 1 后全量测试验证 |
| 包体积增加 | 确定 | 低 | 每个后端 Bundle 约 200KB-1MB |
| 构建时间增加 | 确定 | 低 | 200 变体翻译约 30-60 秒 |

---

## 十一、预期效果

| 指标 | 当前值 | 目标值 |
|------|:-----:|:-----:|
| 单 shader 翻译耗时 | 20-40ms | **0ms**（Bundle 命中时跳过翻译） |
| 单 Program 创建耗时（Bundle 命中）| 40-80ms | **5-10ms**（仅 GPU 对象创建） |
| 首帧 shader 总开销（10 个 Program）| 400-800ms | **50-100ms** |
| Bundle 命中率 | 0% | ≥90% |
| Bundle 未命中时的开销 | N/A | 与当前相同（40-80ms，兜底路径） |
| Bundle 文件大小（单后端）| 0 | 200KB-1MB |
| 构建时间增量 | 0 | 30-60s |

---

## 附录 A：各后端翻译链路代码引用

### A.1 Vulkan 翻译链路

```cpp
// src/gpu/vulkan/VulkanShaderModule.cpp:38-56
VulkanShaderModule::VulkanShaderModule(VulkanGPU* gpu, const ShaderModuleDescriptor& descriptor) {
  std::string vulkanGLSL = PreprocessGLSL(descriptor.code);
  auto spirvBinary = CompileGLSLToSPIRV(gpu->shaderCompiler(), vulkanGLSL, descriptor.stage);
  if (spirvBinary.empty()) {
    LOGE("VulkanShaderModule: GLSL to SPIR-V compilation failed.");
    return;
  }
  VkShaderModuleCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = spirvBinary.size() * sizeof(uint32_t);
  createInfo.pCode = spirvBinary.data();
  auto result = vkCreateShaderModule(gpu->device(), &createInfo, nullptr, &shaderModule);
  // ...
}
```

### A.2 Metal 翻译链路

```cpp
// src/gpu/metal/MetalShaderModule.mm:191-199
std::string MetalShaderModule::convertGLSLToMSL(const shaderc::Compiler* compiler,
                                                const std::string& glslCode, ShaderStage stage) {
  std::string vulkanGLSL = PreprocessGLSL(glslCode);
  auto spirvBinary = CompileGLSLToSPIRV(compiler, vulkanGLSL, stage);
  if (spirvBinary.empty()) { return ""; }
  return convertSPIRVToMSL(spirvBinary, stage);
}
```

```cpp
// src/gpu/metal/MetalShaderModule.mm:82-144
static std::string convertSPIRVToMSL(const std::vector<uint32_t>& spirvBinary, ShaderStage stage) {
  spirv_cross::CompilerMSL mslCompiler(std::move(spvParser.get_parsed_ir()));
  spirv_cross::CompilerMSL::Options mslOptions;
  mslOptions.set_msl_version(2, 3);
  mslOptions.enable_decoration_binding = true;
  mslOptions.use_framebuffer_fetch_subpasses = true;
  // ... binding remapping ...
  return mslCompiler.compile();
}
```

### A.3 WebGPU 翻译链路

```cpp
// src/gpu/webgpu/WebGPUShaderModule.cpp:217-266
bool WebGPUShaderModule::compileShader(WGPUDevice device, const std::string& glslCode, ShaderStage stage) {
  std::string vulkanGLSL = preprocessGLSL(glslCode);
  auto spirvBinary = compileGLSLToSPIRV(vulkanGLSL, stage);
  if (spirvBinary.empty()) { return false; }
  // tint: SPIR-V → WGSL
  tint::spirv::reader::Options readerOptions;
  readerOptions.allow_non_uniform_derivatives = true;
  tint::Program program = tint::spirv::reader::Read(spirvBinary, readerOptions);
  auto result = tint::wgsl::writer::Generate(program, writerOptions);
  std::string wgslCode = result->wgsl;
  // Create WebGPU shader module with WGSL
  WGPUShaderModuleWGSLDescriptor wgslDescriptor = {};
  wgslDescriptor.code = wgslCode.c_str();
  shaderModule = wgpuDeviceCreateShaderModule(device, &moduleDesc);
  return shaderModule != nullptr;
}
```

### A.4 GLSL 生成入口

```cpp
// src/gpu/glsl/GLSLProgramBuilder.cpp:107-114
std::shared_ptr<Program> ProgramBuilder::CreateProgram(Context* context, const ProgramInfo* programInfo) {
  GLSLProgramBuilder builder(context, programInfo);
  if (!builder.emitAndInstallProcessors()) { return nullptr; }
  return builder.finalize();
}
```

```cpp
// src/gpu/ProgramBuilder.cpp:41-50
bool ProgramBuilder::emitAndInstallProcessors() {
  std::string inputColor;
  std::string inputCoverage;
  emitAndInstallGeoProc(&inputColor, &inputCoverage);
  emitAndInstallFragProcessors(&inputColor, &inputCoverage);
  emitAndInstallXferProc(inputColor, inputCoverage);
  emitFSOutputSwizzle();
  return checkSamplerCounts();
}
```

## 附录 B：Program Key 计算流程

```cpp
// src/gpu/ProgramInfo.cpp:124-151
std::shared_ptr<Program> ProgramInfo::getProgram() const {
  auto context = renderTarget->getContext();
  BytesKey programKey = {};
  // 1. GP key: classID + hasUVPerspective + onComputeProcessorKey + attribute formats
  geometryProcessor->computeProcessorKey(context, &programKey);
  // 2. FP keys: 对每个 FP 递归写入 classID + onComputeProcessorKey + texture keys + child keys
  for (const auto& processor : fragmentProcessors) {
    processor->computeProcessorKey(context, &programKey);
  }
  // 3. XP key (如果存在)
  if (xferProcessor != nullptr) {
    xferProcessor->computeProcessorKey(context, &programKey);
  }
  // 4. Pipeline state
  programKey.write(static_cast<uint32_t>(blendMode));
  programKey.write(static_cast<uint32_t>(getOutputSwizzle().asKey()));
  programKey.write(static_cast<uint32_t>(cullMode));
  programKey.write(static_cast<uint32_t>(renderTarget->format()));
  programKey.write(static_cast<uint32_t>(renderTarget->sampleCount()));
  // 查找或创建 Program
  auto program = context->globalCache()->findProgram(programKey);
  if (program == nullptr) {
    program = ProgramBuilder::CreateProgram(context, this);
    context->globalCache()->addProgram(programKey, program);
  }
  return program;
}
```

## 附录 C：ShaderCaps 各后端配置

```cpp
// src/gpu/ShaderCaps.cpp:53-102
ShaderCaps::ShaderCaps(GPU* gpu) {
  auto info = gpu->info();
  if (info->backend == Backend::OpenGL) {
    if (info->version.find("OpenGL ES") != std::string::npos) {
      usesPrecisionModifiers = true;
      versionDeclString = "#version 300 es";
    } else {
      usesPrecisionModifiers = false;
      versionDeclString = "#version 150";
    }
  } else {
    usesPrecisionModifiers = false;
    versionDeclString = "#version 450";
  }
  auto limits = gpu->limits();
  maxFragmentSamplers = limits->maxSamplersPerShaderStage;
  maxUBOSize = limits->maxUniformBufferBindingSize;
  uboOffsetAlignment = limits->minUniformBufferOffsetAlignment;
  if (info->backend == Backend::Metal) {
    frameBufferFetchSupport = true;
    frameBufferFetchUsesSubpassInput = true;
  } else if (info->backend == Backend::WebGPU) {
    frameBufferFetchSupport = false;
    frameBufferFetchUsesSubpassInput = true;
    requiresUniformControlFlow = true;
  } else if (HasExtension(info, "GL_EXT_shader_framebuffer_fetch")) {
    frameBufferFetchNeedsCustomOutput = true;
    frameBufferFetchSupport = true;
    frameBufferFetchColorName = "gl_LastFragData[0]";
    frameBufferFetchExtensionString = "GL_EXT_shader_framebuffer_fetch";
  }
  // ... 其他 OpenGL 扩展 ...
}
```

## 附录 D：WebGPU 后端的 PreprocessGLSL 差异

WebGPU 后端有一套独立的 `preprocessGLSL` 实现（`WebGPUShaderModule.cpp:165-177`），与共享的 `PreprocessGLSL`（`ShaderCompiler.cpp:136-145`）存在以下差异：

| 步骤 | 共享版 (Vulkan/Metal) | WebGPU 版 |
|------|---------------------|-----------|
| GLSL 版本升级 | `#version 450` | `#version 450` |
| UBO binding | `set=0, binding=N` | 无 set，`binding=N` |
| Sampler binding | `set=1, binding=N` | 分离为 `texture2D` + `sampler`，各有独立 binding |
| 纹理采样调用 | `texture(sampler, coord)` | `texture(sampler2D(tex, sampler), coord)` |
| input/output location | 添加 `location=N` | 添加 `location=N` |
| precision 声明 | 移除 | 移除 |

构建期工具在为 WebGPU 后端翻译时，需要使用 WebGPU 版的 preprocessGLSL 而非共享版，以及 WebGPU 版独立的 `compileGLSLToSPIRV`（target 为 `shaderc_env_version_vulkan_1_1`，不同于共享版的 `vulkan_1_0`）。

---

## 附录 E：Processor 完整清单

### GeometryProcessor (12)

| # | 类名 | Key 参数 |
|---|------|---------|
| 1 | DefaultGeometryProcessor | aa |
| 2 | QuadPerEdgeAAGeometryProcessor | aa, hasColor, hasUV, hasSubset, hasSubsetMatrix |
| 3 | EllipseGeometryProcessor | stroke, hasColor |
| 4 | ComplexEllipseGeometryProcessor | stroke, hasColor |
| 5 | NonAARRectGeometryProcessor | hasColor, stroke |
| 6 | ComplexNonAARRectGeometryProcessor | hasColor, stroke |
| 7 | AtlasTextGeometryProcessor | aa, hasColor, isAlphaOnly |
| 8 | ShapeInstancedGeometryProcessor | hasColors, aa |
| 9 | MeshGeometryProcessor | hasTexCoords, hasColors, hasCoverage |
| 10 | RoundStrokeRectGeometryProcessor | aa, hasColor, hasUV |
| 11 | HairlineLineGeometryProcessor | aa |
| 12 | HairlineQuadGeometryProcessor | aa |

### FragmentProcessor (23)

| # | 类名 | Key 参数 |
|---|------|---------|
| 1 | TextureEffect | alphaStart, isAlphaOnly, yuvFormat, yuvColorRange, needSubset, constraint, perspective |
| 2 | TiledTextureEffect | isAlphaOnly, shaderModeX, shaderModeY, constraint, perspective |
| 3 | AARectEffect | (无) |
| 4 | ConstColorProcessor | inputMode |
| 5 | ColorMatrixFragmentProcessor | (无) |
| 6 | AlphaThresholdFragmentProcessor | (无) |
| 7 | GaussianBlur1DFragmentProcessor | maxSigma |
| 8 | XfermodeFragmentProcessor | mode, child 类型 |
| 9 | ComposeFragmentProcessor | (组合 child key) |
| 10 | DeviceSpaceTextureEffect | (无) |
| 11 | LinearGradientLayout | (无) |
| 12 | RadialGradientLayout | (无) |
| 13 | ConicGradientLayout | (无) |
| 14 | DiamondGradientLayout | (无) |
| 15 | DualIntervalGradientColorizer | (无) |
| 16 | UnrolledBinaryGradientColorizer | intervals |
| 17 | TextureGradientColorizer | (无) |
| 18 | SingleIntervalGradientColorizer | (无) |
| 19 | ClampedGradientEffect | (无) |
| 20 | TiledGradientEffect | mirror |
| 21 | LumaColorFilterEffect | (无) |
| 22 | ColorSpaceEffect | (无) |
| 23 | RGBToHSLFragmentProcessor | (无) |

### XferProcessor (2)

| # | 类名 | Key 参数 |
|---|------|---------|
| 1 | EmptyXferProcessor | (无) |
| 2 | PorterDuffXferProcessor | blendMode, dstTexture |
