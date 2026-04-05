# TGFX UE RHI 后端接口设计文档

## 目录

1. [概述与架构](#1-概述与架构)
2. [Backend 枚举扩展](#2-backend-枚举扩展)
3. [UERHIDevice](#3-uerhidevice)
4. [UERHIGPU](#4-uerhigpu)
5. [UERHICommandQueue](#5-uerhicommandqueue)
6. [UERHICommandEncoder](#6-uerhicommandencoder)
7. [UERHICommandBuffer](#7-uerhicommandbuffer)
8. [UERHIRenderPass](#8-uerhirenderpass)
9. [UERHIRenderPipeline](#9-uerhirenderpipeline)
10. [UERHITexture](#10-uerhitexture)
11. [UERHIBuffer](#11-uerhibuffer)
12. [UERHISampler](#12-uerhisampler)
13. [UERHISemaphore](#13-uerhisemaphore)
14. [UERHIShaderModule](#14-uerhishadermodule)
15. [UERHICaps](#15-uerhicaps)
16. [枚举映射表](#16-枚举映射表)
17. [libpag UE 集成层](#17-libpag-ue-集成层)
18. [线程模型与生命周期](#18-线程模型与生命周期)
19. [Shader 编译策略](#19-shader-编译策略)

---

## 1. 概述与架构

### 1.1 设计目标

将 UE RHI 作为 tgfx 的独立渲染后端，使 libpag 能够在 UE 环境中直接调用 UE RHI 进行渲染，实现：

- **零纹理拷贝**：渲染结果直接是 UE RHI 纹理
- **统一 GPU 上下文**：与 UE 共享同一 GPU 上下文
- **无跨上下文同步**：消除 GPU-GPU 同步开销
- **跨平台一致性**：通过 UE RHI 自动适配所有 UE 支持的平台

### 1.2 架构图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           libpag                                        │
│   PAGPlayer → PAGSurface → UEDrawable → tgfx::Surface                  │
└─────────────────────────────────────┬───────────────────────────────────┘
                                      │
┌─────────────────────────────────────▼───────────────────────────────────┐
│                         tgfx UE RHI Backend                             │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ UERHIDevice                                                      │   │
│  │   └── UERHIGPU                                                   │   │
│  │         ├── UERHICommandQueue  ─────→ FRHICommandListImmediate   │   │
│  │         ├── UERHITexture       ─────→ FRHITexture                │   │
│  │         ├── UERHIBuffer        ─────→ FRHIBuffer                 │   │
│  │         ├── UERHISampler       ─────→ FRHISamplerState           │   │
│  │         ├── UERHIRenderPipeline ────→ FGraphicsPipelineState     │   │
│  │         └── UERHIShaderModule  ─────→ FRHIVertexShader/PixelShader│  │
│  │                                                                   │   │
│  │ UERHICommandEncoder                                              │   │
│  │   └── UERHIRenderPass ──────────────→ IRHICommandContext         │   │
│  └─────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────┬───────────────────────────────────┘
                                      │
┌─────────────────────────────────────▼───────────────────────────────────┐
│                         Unreal Engine RHI                               │
│   GDynamicRHI → FRHICommandListImmediate → Platform RHI (D3D12/Vulkan/Metal) │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.3 文件组织

```
tgfx/
├── include/tgfx/gpu/ue/
│   ├── UERHIDevice.h           # 公共头文件
│   └── UERHITypes.h            # UE RHI 类型定义
│
└── src/gpu/ue/
    ├── UERHIGPU.h
    ├── UERHIGPU.cpp
    ├── UERHIDevice.cpp
    ├── UERHICommandQueue.h
    ├── UERHICommandQueue.cpp
    ├── UERHICommandEncoder.h
    ├── UERHICommandEncoder.cpp
    ├── UERHICommandBuffer.h
    ├── UERHIRenderPass.h
    ├── UERHIRenderPass.cpp
    ├── UERHIRenderPipeline.h
    ├── UERHIRenderPipeline.cpp
    ├── UERHITexture.h
    ├── UERHITexture.cpp
    ├── UERHIBuffer.h
    ├── UERHIBuffer.cpp
    ├── UERHISampler.h
    ├── UERHISampler.cpp
    ├── UERHISemaphore.h
    ├── UERHISemaphore.cpp
    ├── UERHIShaderModule.h
    ├── UERHIShaderModule.cpp
    ├── UERHICaps.h
    └── UERHICaps.cpp
```

---

## 2. Backend 枚举扩展

### 2.1 修改 Backend 枚举

**文件**: `include/tgfx/gpu/Backend.h`

```cpp
enum class Backend {
  Unknown,
  OpenGL,
  Metal,
  Vulkan,
  WebGPU,
  UERHI      // 新增：Unreal Engine RHI
};
```

### 2.2 新增 UE RHI Backend 类型

**文件**: `include/tgfx/gpu/ue/UERHITypes.h`

```cpp
#pragma once

#include "tgfx/gpu/Backend.h"

// 前向声明 UE 类型，避免包含 UE 头文件
class FRHITexture;
class FRHIBuffer;
class FRHISamplerState;
class FRHICommandListImmediate;
class FRHIGPUFence;
class FRHIGraphicsPipelineState;
class FRHIVertexShader;
class FRHIPixelShader;
class FRHIVertexDeclaration;

namespace tgfx {

/**
 * UE RHI 纹理信息，用于 BackendTexture 构造
 */
struct UERHITextureInfo {
  FRHITexture* texture = nullptr;
  // 可选：额外的格式/状态信息
};

/**
 * UE RHI 同步信息，用于 BackendSemaphore 构造
 */
struct UERHISyncInfo {
  FRHIGPUFence* fence = nullptr;
};

}  // namespace tgfx
```

### 2.3 扩展 BackendTexture

**文件**: `include/tgfx/gpu/Backend.h` (修改)

```cpp
class BackendTexture {
public:
  // ... 现有构造函数 ...

  /**
   * Creates a UE RHI backend texture.
   */
  BackendTexture(const UERHITextureInfo& ueInfo, int width, int height);

  /**
   * If the backend API is UE RHI, copies a snapshot of the UERHITextureInfo struct into the passed
   * in pointer and returns true. Otherwise, returns false.
   */
  bool getUERHITextureInfo(UERHITextureInfo* ueTextureInfo) const;

private:
  union {
    GLTextureInfo glInfo;
    MetalTextureInfo metalInfo;
    UERHITextureInfo ueInfo;  // 新增
  };
};
```

---

## 3. UERHIDevice

### 3.1 公共接口

**文件**: `include/tgfx/gpu/ue/UERHIDevice.h`

```cpp
#pragma once

#include "tgfx/gpu/Device.h"

namespace tgfx {

/**
 * The UE RHI interface for drawing graphics.
 * 
 * UERHIDevice wraps the global UE RHI (GDynamicRHI) and provides a tgfx Device interface.
 * It should only be created and used on the UE Render Thread.
 * 
 * Thread Safety:
 * - UE RHI operations must be performed on the Render Thread
 * - lockContext() should be called from the Render Thread
 */
class UERHIDevice : public Device {
 public:
  /**
   * Creates a new UERHIDevice using the global GDynamicRHI.
   * Must be called from the UE Render Thread.
   * Returns nullptr if GDynamicRHI is not initialized.
   */
  static std::shared_ptr<UERHIDevice> Make();

  ~UERHIDevice() override;

  /**
   * Returns the underlying FRHICommandListImmediate for direct RHI access.
   * Only valid between lockContext() and unlock().
   */
  FRHICommandListImmediate* getRHICommandList() const;

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  explicit UERHIDevice(std::unique_ptr<class UERHIGPU> gpu);
  
  FRHICommandListImmediate* rhiCommandList = nullptr;
};

}  // namespace tgfx
```

### 3.2 实现要点

```cpp
// src/gpu/ue/UERHIDevice.cpp

#include "UERHIDevice.h"
#include "UERHIGPU.h"
#include "RHICommandList.h"  // UE header

namespace tgfx {

std::shared_ptr<UERHIDevice> UERHIDevice::Make() {
  // 确保在 Render Thread
  check(IsInRenderingThread());
  
  if (GDynamicRHI == nullptr) {
    return nullptr;
  }
  
  auto gpu = UERHIGPU::Make();
  if (gpu == nullptr) {
    return nullptr;
  }
  
  auto device = std::shared_ptr<UERHIDevice>(new UERHIDevice(std::move(gpu)));
  device->weakThis = device;
  return device;
}

bool UERHIDevice::onLockContext() {
  // UE RHI 在 Render Thread 上是线程安全的，不需要额外锁定
  // 但需要验证当前在正确的线程上
  if (!IsInRenderingThread()) {
    return false;
  }
  
  // 获取 immediate command list
  rhiCommandList = &FRHICommandListImmediate::Get();
  return true;
}

void UERHIDevice::onUnlockContext() {
  rhiCommandList = nullptr;
}

FRHICommandListImmediate* UERHIDevice::getRHICommandList() const {
  return rhiCommandList;
}

}  // namespace tgfx
```

### 3.3 设计说明

| 方面 | 设计决策 | 理由 |
|------|---------|------|
| 线程模型 | 仅支持 Render Thread | UE RHI 操作必须在 Render Thread 执行 |
| 锁定机制 | onLockContext 仅验证线程 | UE RHI 不需要额外的锁定机制 |
| 生命周期 | 与 UE 游戏运行时绑定 | Device 存活期间 GDynamicRHI 必须有效 |

---

## 4. UERHIGPU

### 4.1 接口定义

**文件**: `src/gpu/ue/UERHIGPU.h`

```cpp
#pragma once

#include <memory>
#include <unordered_map>
#include "tgfx/gpu/GPU.h"
#include "UERHICaps.h"

class FRHICommandListImmediate;
class FDynamicRHI;

namespace tgfx {

class UERHICommandQueue;

/**
 * UE RHI GPU implementation.
 */
class UERHIGPU : public GPU {
 public:
  /**
   * Creates a UERHIGPU wrapping GDynamicRHI.
   */
  static std::unique_ptr<UERHIGPU> Make();

  ~UERHIGPU() override;

  /**
   * Returns the underlying FDynamicRHI.
   */
  FDynamicRHI* dynamicRHI() const { return _dynamicRHI; }

  // ==================== GPU Interface Implementation ====================

  const GPUInfo* info() const override { return caps->info(); }
  const GPUFeatures* features() const override { return caps->features(); }
  const GPULimits* limits() const override { return caps->limits(); }
  CommandQueue* queue() const override;

  bool isFormatRenderable(PixelFormat format) const override;
  int getSampleCount(int requestedCount, PixelFormat format) const override;

  // === 资源创建 ===
  std::shared_ptr<GPUBuffer> createBuffer(size_t size, uint32_t usage) override;
  std::shared_ptr<Texture> createTexture(const TextureDescriptor& descriptor) override;
  std::shared_ptr<Sampler> createSampler(const SamplerDescriptor& descriptor) override;
  std::shared_ptr<ShaderModule> createShaderModule(const ShaderModuleDescriptor& descriptor) override;
  std::shared_ptr<RenderPipeline> createRenderPipeline(const RenderPipelineDescriptor& descriptor) override;
  std::shared_ptr<CommandEncoder> createCommandEncoder() override;

  // === 外部资源导入 ===
  std::vector<std::shared_ptr<Texture>> importHardwareTextures(HardwareBufferRef hardwareBuffer, uint32_t usage) override;
  std::shared_ptr<Texture> importBackendTexture(const BackendTexture& backendTexture, uint32_t usage, bool adopted = false) override;
  std::shared_ptr<Texture> importBackendRenderTarget(const BackendRenderTarget& backendRenderTarget) override;
  std::shared_ptr<Semaphore> importBackendSemaphore(const BackendSemaphore& semaphore) override;
  BackendSemaphore stealBackendSemaphore(std::shared_ptr<Semaphore> semaphore) override;

  // ==================== UE RHI 特有方法 ====================

  /**
   * 获取当前的 RHI Command List (由 Device 在 lockContext 时设置)
   */
  FRHICommandListImmediate* currentRHICommandList() const { return _currentRHICmdList; }
  void setCurrentRHICommandList(FRHICommandListImmediate* cmdList) { _currentRHICmdList = cmdList; }

  /**
   * 获取 UE PixelFormat
   */
  EPixelFormat getUEPixelFormat(PixelFormat format) const;

  /**
   * Sampler 缓存 (基于描述符复用)
   */
  std::shared_ptr<Sampler> getCachedSampler(const SamplerDescriptor& descriptor);

 private:
  explicit UERHIGPU(FDynamicRHI* dynamicRHI);

  FDynamicRHI* _dynamicRHI = nullptr;
  FRHICommandListImmediate* _currentRHICmdList = nullptr;
  std::unique_ptr<UERHICaps> caps = nullptr;
  std::unique_ptr<UERHICommandQueue> commandQueue = nullptr;
  
  // Sampler 缓存
  std::unordered_map<uint32_t, std::shared_ptr<Sampler>> samplerCache;
  static uint32_t MakeSamplerKey(const SamplerDescriptor& descriptor);
};

}  // namespace tgfx
```

### 4.2 接口映射表

| tgfx GPU 方法 | UE RHI 对应接口 | 说明 |
|--------------|----------------|------|
| `createBuffer()` | `RHICmdList.CreateBuffer()` | 创建 FRHIBuffer |
| `createTexture()` | `RHICmdList.CreateTexture()` | 创建 FRHITexture |
| `createSampler()` | `RHICreateSamplerState()` | 创建 FSamplerStateRHI |
| `createShaderModule()` | `RHICreateVertexShader/PixelShader()` | 需要预编译字节码 |
| `createRenderPipeline()` | `PipelineStateCache::GetAndOrCreateGraphicsPipelineState()` | 创建 PSO |
| `createCommandEncoder()` | 返回包装的 CommandEncoder | 编码到 FRHICommandList |
| `importBackendTexture()` | 包装外部 FRHITexture | adopted=false 不释放 |
| `importBackendSemaphore()` | 包装 FRHIGPUFence | GPU 同步 |

### 4.3 实现示例

```cpp
// createTexture 实现
std::shared_ptr<Texture> UERHIGPU::createTexture(const TextureDescriptor& descriptor) {
  EPixelFormat ueFormat = getUEPixelFormat(descriptor.format);
  if (ueFormat == PF_Unknown) {
    return nullptr;
  }

  ETextureCreateFlags texCreateFlags = TexCreate_None;
  if (descriptor.usage & TextureUsage::RENDER_ATTACHMENT) {
    texCreateFlags |= TexCreate_RenderTargetable;
  }
  if (descriptor.usage & TextureUsage::TEXTURE_BINDING) {
    texCreateFlags |= TexCreate_ShaderResource;
  }
  if (descriptor.sampleCount > 1) {
    texCreateFlags |= TexCreate_MSAA;
  }

  const FRHITextureCreateDesc createDesc =
      FRHITextureCreateDesc::Create2D(TEXT("TGFXTexture"), descriptor.width, descriptor.height, ueFormat)
          .SetNumMips(descriptor.mipLevelCount)
          .SetNumSamples(descriptor.sampleCount)
          .SetFlags(texCreateFlags);

  FTextureRHIRef rhiTexture = _currentRHICmdList->CreateTexture(createDesc);
  if (!rhiTexture.IsValid()) {
    return nullptr;
  }

  return std::make_shared<UERHITexture>(descriptor, rhiTexture);
}
```

---

## 5. UERHICommandQueue

### 5.1 接口定义

**文件**: `src/gpu/ue/UERHICommandQueue.h`

```cpp
#pragma once

#include "tgfx/gpu/CommandQueue.h"

class FRHICommandListImmediate;

namespace tgfx {

class UERHIGPU;
class UERHISemaphore;

/**
 * UE RHI command queue implementation.
 * 
 * Maps to FRHICommandListImmediate operations.
 * All operations are immediate and executed on submit.
 */
class UERHICommandQueue : public CommandQueue {
 public:
  explicit UERHICommandQueue(UERHIGPU* gpu);
  ~UERHICommandQueue() override;

  // ==================== CommandQueue Interface ====================

  void writeBuffer(std::shared_ptr<GPUBuffer> buffer, size_t bufferOffset,
                   const void* data, size_t dataSize) override;

  void writeTexture(std::shared_ptr<Texture> texture, const Rect& rect,
                    const void* pixels, size_t rowBytes) override;

  void submit(std::shared_ptr<CommandBuffer> commandBuffer) override;

  std::shared_ptr<Semaphore> insertSemaphore() override;

  void waitSemaphore(std::shared_ptr<Semaphore> semaphore) override;

  void waitUntilCompleted() override;

 protected:
  std::chrono::steady_clock::time_point completedFrameTime() const override;

 private:
  UERHIGPU* gpu = nullptr;
  std::shared_ptr<UERHISemaphore> pendingSignalSemaphore = nullptr;
  std::shared_ptr<UERHISemaphore> pendingWaitSemaphore = nullptr;
  std::atomic<int64_t> _completedFrameTime = {0};
};

}  // namespace tgfx
```

### 5.2 接口映射

| tgfx 方法 | UE RHI 映射 | 说明 |
|----------|------------|------|
| `writeBuffer()` | `RHILockBuffer` + memcpy + `RHIUnlockBuffer` | 或使用 `UpdateSubresource` |
| `writeTexture()` | `RHIUpdateTexture2D()` | 更新纹理像素 |
| `submit()` | `RHICmdList.ImmediateFlush()` | 刷新命令到 GPU |
| `insertSemaphore()` | `RHICreateGPUFence()` + `RHIWriteGPUFence()` | 插入 GPU fence |
| `waitSemaphore()` | `FRHIGPUFence::Wait()` | GPU 等待 fence |
| `waitUntilCompleted()` | `RHICmdList.ImmediateFlush(FlushRHIThread)` | 等待 GPU 完成 |

### 5.3 实现要点

```cpp
void UERHICommandQueue::writeTexture(std::shared_ptr<Texture> texture, const Rect& rect,
                                     const void* pixels, size_t rowBytes) {
  auto ueTexture = std::static_pointer_cast<UERHITexture>(texture);
  FRHITexture* rhiTexture = ueTexture->rhiTexture();
  FRHICommandListImmediate* cmdList = gpu->currentRHICommandList();

  FUpdateTextureRegion2D region(
      static_cast<uint32>(rect.x()),
      static_cast<uint32>(rect.y()),
      0, 0,
      static_cast<uint32>(rect.width()),
      static_cast<uint32>(rect.height())
  );

  cmdList->UpdateTexture2D(
      rhiTexture,
      0,  // MipIndex
      region,
      static_cast<uint32>(rowBytes),
      static_cast<const uint8*>(pixels)
  );
}

std::shared_ptr<Semaphore> UERHICommandQueue::insertSemaphore() {
  FRHICommandListImmediate* cmdList = gpu->currentRHICommandList();
  
  FGPUFenceRHIRef fence = RHICreateGPUFence(TEXT("TGFXSemaphore"));
  cmdList->WriteGPUFence(fence);
  
  auto semaphore = std::make_shared<UERHISemaphore>(fence);
  pendingSignalSemaphore = semaphore;
  return semaphore;
}

void UERHICommandQueue::waitUntilCompleted() {
  FRHICommandListImmediate* cmdList = gpu->currentRHICommandList();
  cmdList->ImmediateFlush(EImmediateFlushType::FlushRHIThread);
}
```

---

## 6. UERHICommandEncoder

### 6.1 接口定义

**文件**: `src/gpu/ue/UERHICommandEncoder.h`

```cpp
#pragma once

#include "tgfx/gpu/CommandEncoder.h"
#include <vector>

namespace tgfx {

class UERHIGPU;

/**
 * UE RHI command encoder implementation.
 * 
 * Encodes rendering commands that will be executed via FRHICommandList.
 */
class UERHICommandEncoder : public CommandEncoder {
 public:
  static std::shared_ptr<UERHICommandEncoder> Make(UERHIGPU* gpu);

  ~UERHICommandEncoder() override;

  // ==================== CommandEncoder Interface ====================

  GPU* gpu() const override;

  void copyTextureToTexture(std::shared_ptr<Texture> srcTexture, const Rect& srcRect,
                            std::shared_ptr<Texture> dstTexture, const Point& dstOffset) override;

  void copyTextureToBuffer(std::shared_ptr<Texture> srcTexture, const Rect& srcRect,
                           std::shared_ptr<GPUBuffer> dstBuffer, size_t dstOffset = 0,
                           size_t dstRowBytes = 0) override;

  void generateMipmapsForTexture(std::shared_ptr<Texture> texture) override;

 protected:
  std::shared_ptr<RenderPass> onBeginRenderPass(const RenderPassDescriptor& descriptor) override;
  std::shared_ptr<CommandBuffer> onFinish() override;

 private:
  explicit UERHICommandEncoder(UERHIGPU* gpu);

  UERHIGPU* _gpu = nullptr;
  bool hasCommands = false;  // 标记是否有已编码的命令
};

}  // namespace tgfx
```

### 6.2 接口映射

| tgfx 方法 | UE RHI 映射 | 说明 |
|----------|------------|------|
| `beginRenderPass()` | `RHIBeginRenderPass(FRHIRenderPassInfo)` | 开始渲染 Pass |
| `copyTextureToTexture()` | `RHICopyTexture()` | 纹理间拷贝 |
| `copyTextureToBuffer()` | `RHICopyToStagingBuffer()` | GPU 回读 |
| `generateMipmapsForTexture()` | 自定义 Compute 或 Draw 方式 | UE 无直接 API |
| `finish()` | 返回 UERHICommandBuffer | 结束编码 |

### 6.3 实现要点

```cpp
void UERHICommandEncoder::copyTextureToTexture(
    std::shared_ptr<Texture> srcTexture, const Rect& srcRect,
    std::shared_ptr<Texture> dstTexture, const Point& dstOffset) {
  
  auto ueSrc = std::static_pointer_cast<UERHITexture>(srcTexture);
  auto ueDst = std::static_pointer_cast<UERHITexture>(dstTexture);
  FRHICommandListImmediate* cmdList = _gpu->currentRHICommandList();

  FRHICopyTextureInfo copyInfo;
  copyInfo.Size = FIntVector(static_cast<int32>(srcRect.width()), 
                              static_cast<int32>(srcRect.height()), 1);
  copyInfo.SourcePosition = FIntVector(static_cast<int32>(srcRect.x()),
                                        static_cast<int32>(srcRect.y()), 0);
  copyInfo.DestPosition = FIntVector(static_cast<int32>(dstOffset.x),
                                      static_cast<int32>(dstOffset.y), 0);

  cmdList->CopyTexture(ueSrc->rhiTexture(), ueDst->rhiTexture(), copyInfo);
  hasCommands = true;
}

std::shared_ptr<RenderPass> UERHICommandEncoder::onBeginRenderPass(
    const RenderPassDescriptor& descriptor) {
  return UERHIRenderPass::Make(this, descriptor);
}

std::shared_ptr<CommandBuffer> UERHICommandEncoder::onFinish() {
  if (!hasCommands) {
    return nullptr;
  }
  return std::make_shared<UERHICommandBuffer>();
}
```

---

## 7. UERHICommandBuffer

### 7.1 接口定义

**文件**: `src/gpu/ue/UERHICommandBuffer.h`

```cpp
#pragma once

#include "tgfx/gpu/CommandBuffer.h"

namespace tgfx {

/**
 * UE RHI command buffer implementation.
 * 
 * Note: In UE RHI, commands are executed immediately on the FRHICommandList,
 * so this class serves primarily as a marker that encoding has completed.
 */
class UERHICommandBuffer : public CommandBuffer {
 public:
  UERHICommandBuffer() = default;
  ~UERHICommandBuffer() override = default;
};

}  // namespace tgfx
```

### 7.2 设计说明

UE RHI 是 **immediate mode** 风格，命令在调用时立即录入 FRHICommandList。因此 `UERHICommandBuffer` 仅作为编码完成的标记，实际命令已在 CommandEncoder 阶段录入。

---

## 8. UERHIRenderPass

### 8.1 接口定义

**文件**: `src/gpu/ue/UERHIRenderPass.h`

```cpp
#pragma once

#include "tgfx/gpu/RenderPass.h"
#include <memory>

class FRHICommandListImmediate;

namespace tgfx {

class UERHICommandEncoder;
class UERHIRenderPipeline;
class UERHIGPU;

/**
 * UE RHI render pass implementation.
 * 
 * Maps tgfx RenderPass operations to UE RHI IRHICommandContext methods.
 */
class UERHIRenderPass : public RenderPass {
 public:
  static std::shared_ptr<UERHIRenderPass> Make(UERHICommandEncoder* encoder,
                                                const RenderPassDescriptor& descriptor);

  ~UERHIRenderPass() override;

  // ==================== RenderPass Interface ====================

  GPU* gpu() const override;

  void setViewport(int x, int y, int width, int height) override;
  void setScissorRect(int x, int y, int width, int height) override;

  void setPipeline(std::shared_ptr<RenderPipeline> pipeline) override;

  void setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer,
                        size_t offset, size_t size) override;
  void setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                  std::shared_ptr<Sampler> sampler) override;
  void setVertexBuffer(unsigned slot, std::shared_ptr<GPUBuffer> buffer,
                       size_t offset = 0) override;
  void setIndexBuffer(std::shared_ptr<GPUBuffer> buffer,
                      IndexFormat format = IndexFormat::UInt16) override;
  void setStencilReference(uint32_t reference) override;

  void draw(PrimitiveType primitiveType, uint32_t vertexCount, uint32_t instanceCount = 1,
            uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
  void drawIndexed(PrimitiveType primitiveType, uint32_t indexCount, uint32_t instanceCount = 1,
                   uint32_t firstIndex = 0, int32_t baseVertex = 0,
                   uint32_t firstInstance = 0) override;

 protected:
  void onEnd() override;

 private:
  UERHIRenderPass(UERHICommandEncoder* encoder, const RenderPassDescriptor& descriptor);

  bool beginRenderPass();

  UERHICommandEncoder* commandEncoder = nullptr;
  UERHIGPU* _gpu = nullptr;
  FRHICommandListImmediate* rhiCmdList = nullptr;

  std::shared_ptr<UERHIRenderPipeline> currentPipeline = nullptr;

  // 状态缓存 (避免冗余 RHI 调用)
  int lastViewportX = -1, lastViewportY = -1;
  int lastViewportWidth = -1, lastViewportHeight = -1;
  int lastScissorX = -1, lastScissorY = -1;
  int lastScissorWidth = -1, lastScissorHeight = -1;

  static constexpr int MaxUniformBindings = 8;
  GPUBuffer* lastUniformBuffers[MaxUniformBindings] = {};
  size_t lastUniformOffsets[MaxUniformBindings] = {};

  static constexpr int MaxTextureBindings = 16;
  Texture* lastTextures[MaxTextureBindings] = {};
  Sampler* lastSamplers[MaxTextureBindings] = {};

  static constexpr int MaxVertexBufferSlots = 8;
  GPUBuffer* lastVertexBuffers[MaxVertexBufferSlots] = {};
  size_t lastVertexOffsets[MaxVertexBufferSlots] = {};

  // 索引缓冲状态
  std::shared_ptr<class UERHIBuffer> indexBuffer = nullptr;
  IndexFormat indexFormat = IndexFormat::UInt16;
};

}  // namespace tgfx
```

### 8.2 接口映射表

| tgfx RenderPass 方法 | UE RHI (IRHICommandContext) | 参数映射 |
|---------------------|---------------------------|---------|
| `setViewport(x, y, w, h)` | `RHISetViewport(MinX, MinY, MinZ, MaxX, MaxY, MaxZ)` | MinZ=0, MaxZ=1 |
| `setScissorRect(x, y, w, h)` | `RHISetScissorRect(bEnable, MinX, MinY, MaxX, MaxY)` | bEnable=true |
| `setPipeline(pipeline)` | `RHISetGraphicsPipelineState(PSO, StencilRef, bApplyAdditionalState)` | |
| `setUniformBuffer(binding, buf, offset, size)` | `RHISetShaderParameters()` | 通过 FRHIBatchedShaderParameters |
| `setTexture(binding, tex, sampler)` | `RHISetShaderParameters()` | 纹理 + 采样器绑定 |
| `setVertexBuffer(slot, buf, offset)` | `RHISetStreamSource(StreamIndex, Buffer, Offset)` | |
| `setIndexBuffer(buf, format)` | 缓存，draw 时传入 | |
| `setStencilReference(ref)` | `RHISetStencilRef(ref)` | |
| `draw(type, count, inst, first, firstInst)` | `RHIDrawPrimitive(BaseVertexIndex, NumPrimitives, NumInstances)` | 需要计算 NumPrimitives |
| `drawIndexed(...)` | `RHIDrawIndexedPrimitive(IndexBuffer, BaseVertex, FirstInstance, ...)` | |
| `end()` | `RHIEndRenderPass()` | |

### 8.3 实现要点

```cpp
bool UERHIRenderPass::beginRenderPass() {
  FRHIRenderPassInfo rhiPassInfo;

  // 转换颜色附件
  for (size_t i = 0; i < descriptor.colorAttachments.size(); ++i) {
    const auto& ca = descriptor.colorAttachments[i];
    auto ueTexture = std::static_pointer_cast<UERHITexture>(ca.texture);
    
    rhiPassInfo.ColorRenderTargets[i].RenderTarget = ueTexture->rhiTexture();
    rhiPassInfo.ColorRenderTargets[i].Action = ConvertRenderTargetActions(ca.loadAction, ca.storeAction);
    
    if (ca.resolveTexture) {
      auto ueResolve = std::static_pointer_cast<UERHITexture>(ca.resolveTexture);
      rhiPassInfo.ColorRenderTargets[i].ResolveTarget = ueResolve->rhiTexture();
    }
  }

  // 转换深度模板附件
  if (descriptor.depthStencilAttachment.texture) {
    const auto& ds = descriptor.depthStencilAttachment;
    auto ueTexture = std::static_pointer_cast<UERHITexture>(ds.texture);
    
    rhiPassInfo.DepthStencilRenderTarget.DepthStencilTarget = ueTexture->rhiTexture();
    rhiPassInfo.DepthStencilRenderTarget.Action = ConvertDepthStencilActions(ds.loadAction, ds.storeAction);
  }

  rhiCmdList->BeginRenderPass(rhiPassInfo, TEXT("TGFXRenderPass"));
  return true;
}

void UERHIRenderPass::draw(PrimitiveType primitiveType, uint32_t vertexCount,
                           uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
  // 计算图元数量
  uint32_t numPrimitives = CalculatePrimitiveCount(primitiveType, vertexCount);
  
  rhiCmdList->RHIDrawPrimitive(firstVertex, numPrimitives, instanceCount);
}

void UERHIRenderPass::drawIndexed(PrimitiveType primitiveType, uint32_t indexCount,
                                  uint32_t instanceCount, uint32_t firstIndex,
                                  int32_t baseVertex, uint32_t firstInstance) {
  // 注意: UE RHI 的 baseVertex 在某些平台可能不支持
  uint32_t numPrimitives = CalculatePrimitiveCount(primitiveType, indexCount);
  
  rhiCmdList->RHIDrawIndexedPrimitive(
      indexBuffer->rhiBuffer(),
      baseVertex,
      firstInstance,
      indexCount,  // NumVertices (实际是索引数)
      firstIndex,
      numPrimitives,
      instanceCount
  );
}

void UERHIRenderPass::onEnd() {
  rhiCmdList->EndRenderPass();
}
```

---

## 9. UERHIRenderPipeline

### 9.1 接口定义

**文件**: `src/gpu/ue/UERHIRenderPipeline.h`

```cpp
#pragma once

#include "tgfx/gpu/RenderPipeline.h"
#include <unordered_map>

class FRHIGraphicsPipelineState;
class FRHIVertexDeclaration;
class FRHIBlendState;
class FRHIRasterizerState;
class FRHIDepthStencilState;

namespace tgfx {

class UERHIGPU;
class UERHIShaderModule;

/**
 * UE RHI render pipeline implementation.
 * 
 * Wraps UE graphics pipeline state (PSO).
 */
class UERHIRenderPipeline : public RenderPipeline {
 public:
  static std::shared_ptr<UERHIRenderPipeline> Make(UERHIGPU* gpu,
                                                    const RenderPipelineDescriptor& descriptor);

  ~UERHIRenderPipeline() override;

  /**
   * Returns the UE graphics pipeline state.
   */
  FRHIGraphicsPipelineState* rhiPipelineState() const { return pipelineState; }

  /**
   * Returns the vertex declaration.
   */
  FRHIVertexDeclaration* rhiVertexDeclaration() const { return vertexDeclaration; }

  /**
   * Returns the FGraphicsPipelineStateInitializer for deferred PSO creation.
   */
  const FGraphicsPipelineStateInitializer& getPSOInitializer() const { return psoInitializer; }

  /**
   * Returns the texture binding index for a given logical binding.
   */
  unsigned getTextureIndex(unsigned binding) const;

  /**
   * Returns the shader visibility for a uniform block binding.
   */
  uint32_t getUniformBlockVisibility(unsigned binding) const;

  /**
   * Returns the primitive type for UE RHI.
   */
  EPrimitiveType getUEPrimitiveType() const { return uePrimitiveType; }

 private:
  UERHIRenderPipeline(UERHIGPU* gpu, const RenderPipelineDescriptor& descriptor);

  bool initialize(const RenderPipelineDescriptor& descriptor);

  UERHIGPU* _gpu = nullptr;
  
  // UE RHI 对象
  FRHIGraphicsPipelineState* pipelineState = nullptr;
  FRHIVertexDeclaration* vertexDeclaration = nullptr;
  FGraphicsPipelineStateInitializer psoInitializer;

  // Shader 引用
  std::shared_ptr<UERHIShaderModule> vertexShader = nullptr;
  std::shared_ptr<UERHIShaderModule> pixelShader = nullptr;

  // 绑定映射
  std::unordered_map<unsigned, unsigned> textureUnits;
  std::unordered_map<unsigned, uint32_t> uniformBlockVisibility;

  // 图元类型
  EPrimitiveType uePrimitiveType = PT_TriangleList;
};

}  // namespace tgfx
```

### 9.2 PSO 初始化流程

```cpp
bool UERHIRenderPipeline::initialize(const RenderPipelineDescriptor& descriptor) {
  // 1. 创建顶点声明
  TArray<FVertexElement> vertexElements;
  for (const auto& layout : descriptor.vertex.bufferLayouts) {
    uint32 offset = 0;
    for (const auto& attr : layout.attributes) {
      FVertexElement element;
      element.StreamIndex = /* slot */;
      element.Offset = offset;
      element.Type = ConvertVertexFormat(attr.format());
      element.AttributeIndex = /* location from shader */;
      element.Stride = layout.stride;
      element.bUseInstanceIndex = (layout.stepMode == VertexStepMode::Instance);
      vertexElements.Add(element);
      offset += attr.size();
    }
  }
  vertexDeclaration = RHICreateVertexDeclaration(vertexElements);

  // 2. 获取 Shader
  vertexShader = std::static_pointer_cast<UERHIShaderModule>(descriptor.vertex.module);
  pixelShader = std::static_pointer_cast<UERHIShaderModule>(descriptor.fragment.module);

  // 3. 创建状态对象
  FRHIBlendState* blendState = CreateBlendState(descriptor.fragment.colorAttachments);
  FRHIRasterizerState* rasterizerState = CreateRasterizerState(descriptor.primitive);
  FRHIDepthStencilState* depthStencilState = CreateDepthStencilState(descriptor.depthStencil);

  // 4. 填充 PSO 初始化器
  psoInitializer.BoundShaderState.VertexDeclarationRHI = vertexDeclaration;
  psoInitializer.BoundShaderState.VertexShaderRHI = vertexShader->rhiVertexShader();
  psoInitializer.BoundShaderState.PixelShaderRHI = pixelShader->rhiPixelShader();
  psoInitializer.BlendState = blendState;
  psoInitializer.RasterizerState = rasterizerState;
  psoInitializer.DepthStencilState = depthStencilState;
  psoInitializer.PrimitiveType = uePrimitiveType;
  // ... 填充渲染目标格式等

  // 5. 创建或获取缓存的 PSO
  pipelineState = PipelineStateCache::GetAndOrCreateGraphicsPipelineState(
      _gpu->currentRHICommandList(), psoInitializer, EApplyRendertargetOption::CheckApply);

  return pipelineState != nullptr;
}
```

---

## 10. UERHITexture

### 10.1 接口定义

**文件**: `src/gpu/ue/UERHITexture.h`

```cpp
#pragma once

#include "tgfx/gpu/Texture.h"

class FRHITexture;
typedef TRefCountPtr<FRHITexture> FTextureRHIRef;

namespace tgfx {

/**
 * UE RHI texture implementation.
 */
class UERHITexture : public Texture {
 public:
  /**
   * Creates a new UE RHI texture with the given descriptor.
   */
  static std::shared_ptr<UERHITexture> Make(const TextureDescriptor& descriptor,
                                             FTextureRHIRef texture);

  /**
   * Creates an external texture wrapper (adopted = false means we don't own it).
   */
  static std::shared_ptr<UERHITexture> MakeFrom(const TextureDescriptor& descriptor,
                                                 FTextureRHIRef texture, bool adopted);

  ~UERHITexture() override;

  /**
   * Returns the underlying FRHITexture.
   */
  FRHITexture* rhiTexture() const { return _texture.GetReference(); }

  /**
   * Returns the FTextureRHIRef for reference counting.
   */
  const FTextureRHIRef& rhiTextureRef() const { return _texture; }

  // ==================== Texture Interface ====================

  TextureType type() const override { return TextureType::TwoD; }

  BackendTexture getBackendTexture() const override;

  BackendRenderTarget getBackendRenderTarget() const override;

 private:
  UERHITexture(const TextureDescriptor& descriptor, FTextureRHIRef texture, bool adopted);

  FTextureRHIRef _texture;
  bool _adopted = true;  // true = 我们拥有纹理，析构时释放
};

}  // namespace tgfx
```

### 10.2 实现要点

```cpp
UERHITexture::~UERHITexture() {
  if (!_adopted) {
    // 外部纹理：解除引用但不释放
    // FTextureRHIRef 的析构会自动减少引用计数
    // 但我们需要确保不触发实际的资源销毁
    // 在 UE 中，只要有其他引用，资源就不会被销毁
  }
  // adopted = true 时，FTextureRHIRef 析构会正常处理引用计数
}

BackendTexture UERHITexture::getBackendTexture() const {
  UERHITextureInfo info;
  info.texture = _texture.GetReference();
  return BackendTexture(info, descriptor.width, descriptor.height);
}

BackendRenderTarget UERHITexture::getBackendRenderTarget() const {
  if (!(descriptor.usage & TextureUsage::RENDER_ATTACHMENT)) {
    return BackendRenderTarget();  // 无效
  }
  UERHITextureInfo info;
  info.texture = _texture.GetReference();
  return BackendRenderTarget(info, descriptor.width, descriptor.height);
}
```

---

## 11. UERHIBuffer

### 11.1 接口定义

**文件**: `src/gpu/ue/UERHIBuffer.h`

```cpp
#pragma once

#include "tgfx/gpu/GPUBuffer.h"

class FRHIBuffer;
typedef TRefCountPtr<FRHIBuffer> FBufferRHIRef;

namespace tgfx {

/**
 * UE RHI buffer implementation.
 */
class UERHIBuffer : public GPUBuffer {
 public:
  static std::shared_ptr<UERHIBuffer> Make(size_t size, uint32_t usage, FBufferRHIRef buffer);

  ~UERHIBuffer() override;

  /**
   * Returns the underlying FRHIBuffer.
   */
  FRHIBuffer* rhiBuffer() const { return _buffer.GetReference(); }

  // ==================== GPUBuffer Interface ====================

  bool isReady() const override;

  void* map(size_t offset = 0, size_t size = GPU_BUFFER_WHOLE_SIZE) override;

  void unmap() override;

 private:
  UERHIBuffer(size_t size, uint32_t usage, FBufferRHIRef buffer);

  FBufferRHIRef _buffer;
  void* mappedPointer = nullptr;
  bool isMapped = false;
};

}  // namespace tgfx
```

### 11.2 实现要点

```cpp
void* UERHIBuffer::map(size_t offset, size_t mapSize) {
  if (isMapped) {
    return nullptr;
  }

  size_t actualSize = (mapSize == GPU_BUFFER_WHOLE_SIZE) ? (_size - offset) : mapSize;
  
  // UE RHI Buffer Lock
  mappedPointer = RHILockBuffer(_buffer.GetReference(), offset, actualSize, RLM_WriteOnly);
  if (mappedPointer) {
    isMapped = true;
  }
  return mappedPointer;
}

void UERHIBuffer::unmap() {
  if (!isMapped) {
    return;
  }
  RHIUnlockBuffer(_buffer.GetReference());
  mappedPointer = nullptr;
  isMapped = false;
}

bool UERHIBuffer::isReady() const {
  // 对于 READBACK buffer，需要检查 GPU 是否完成
  if (_usage & GPUBufferUsage::READBACK) {
    // UE 没有直接的 buffer ready 检查
    // 通常通过 GPU Fence 同步
    return true;  // 假设已同步
  }
  return true;
}
```

---

## 12. UERHISampler

### 12.1 接口定义

**文件**: `src/gpu/ue/UERHISampler.h`

```cpp
#pragma once

#include "tgfx/gpu/Sampler.h"

class FRHISamplerState;
typedef TRefCountPtr<FRHISamplerState> FSamplerStateRHIRef;

namespace tgfx {

/**
 * UE RHI sampler implementation.
 */
class UERHISampler : public Sampler {
 public:
  static std::shared_ptr<UERHISampler> Make(const SamplerDescriptor& descriptor);

  ~UERHISampler() override = default;

  /**
   * Returns the underlying FRHISamplerState.
   */
  FRHISamplerState* rhiSamplerState() const { return _sampler.GetReference(); }

 private:
  explicit UERHISampler(FSamplerStateRHIRef sampler);

  FSamplerStateRHIRef _sampler;
};

}  // namespace tgfx
```

### 12.2 实现要点

```cpp
std::shared_ptr<UERHISampler> UERHISampler::Make(const SamplerDescriptor& descriptor) {
  FSamplerStateInitializerRHI initializer;
  
  initializer.Filter = ConvertFilter(descriptor.minFilter, descriptor.magFilter, descriptor.mipmapMode);
  initializer.AddressU = ConvertAddressMode(descriptor.addressModeX);
  initializer.AddressV = ConvertAddressMode(descriptor.addressModeY);
  initializer.AddressW = SF_Clamp;  // tgfx 只使用 2D 纹理
  initializer.MipBias = 0;
  initializer.MaxAnisotropy = 1;
  initializer.BorderColor = FLinearColor::Transparent;
  initializer.ComparisonFunction = SCF_Never;
  initializer.MinMipLevel = 0;
  initializer.MaxMipLevel = FLT_MAX;

  FSamplerStateRHIRef sampler = RHICreateSamplerState(initializer);
  return std::make_shared<UERHISampler>(sampler);
}

ESamplerFilter ConvertFilter(FilterMode min, FilterMode mag, MipmapMode mip) {
  if (min == FilterMode::Linear && mag == FilterMode::Linear) {
    if (mip == MipmapMode::Linear) return SF_Trilinear;
    if (mip == MipmapMode::Nearest) return SF_Bilinear;
    return SF_Bilinear;
  }
  return SF_Point;
}

ESamplerAddressMode ConvertAddressMode(AddressMode mode) {
  switch (mode) {
    case AddressMode::ClampToEdge: return AM_Clamp;
    case AddressMode::Repeat: return AM_Wrap;
    case AddressMode::MirrorRepeat: return AM_Mirror;
    case AddressMode::ClampToBorder: return AM_Border;
    default: return AM_Clamp;
  }
}
```

---

## 13. UERHISemaphore

### 13.1 接口定义

**文件**: `src/gpu/ue/UERHISemaphore.h`

```cpp
#pragma once

#include "tgfx/gpu/Semaphore.h"

class FRHIGPUFence;
typedef TRefCountPtr<FRHIGPUFence> FGPUFenceRHIRef;

namespace tgfx {

/**
 * UE RHI semaphore implementation.
 * 
 * Wraps FRHIGPUFence for GPU synchronization.
 */
class UERHISemaphore : public Semaphore {
 public:
  explicit UERHISemaphore(FGPUFenceRHIRef fence);

  ~UERHISemaphore() override = default;

  /**
   * Returns the underlying GPU fence.
   */
  FRHIGPUFence* rhiFence() const { return _fence.GetReference(); }

  /**
   * Polls if the fence has been signaled.
   */
  bool poll() const;

  /**
   * Waits for the fence to be signaled (blocking).
   * Must be called from Render Thread.
   */
  void wait(FRHICommandListImmediate& cmdList) const;

  // ==================== Semaphore Interface ====================

  BackendSemaphore getBackendSemaphore() const override;

 private:
  FGPUFenceRHIRef _fence;
};

}  // namespace tgfx
```

### 13.2 实现要点

```cpp
bool UERHISemaphore::poll() const {
  return _fence->Poll();
}

void UERHISemaphore::wait(FRHICommandListImmediate& cmdList) const {
  _fence->Wait(cmdList, FRHIGPUMask::All());
}

BackendSemaphore UERHISemaphore::getBackendSemaphore() const {
  UERHISyncInfo info;
  info.fence = _fence.GetReference();
  return BackendSemaphore(info);
}
```

---

## 14. UERHIShaderModule

> **重要更新**：基于 Rive UE 5.7 插件最新源码分析（详见附录 O），运行时 Shader 编译方案（shaderc +
> SPIRV-Cross + D3DCompile）已被证明不是最优路径。Rive 已成功使用 **UE Global Shader +
> Permutation 预编译方案**，所有 Shader 在 UE 构建时预编译，运行时零编译开销。建议 PAG 也采用此
> 方案，以下原始设计保留作为备选参考。如采用附录 O 方案，本章 UERHIShaderModule 类不再需要。

### 14.1 接口定义

**文件**: `src/gpu/ue/UERHIShaderModule.h`

```cpp
#pragma once

#include "tgfx/gpu/ShaderModule.h"
#include <string>
#include <vector>

class FRHIVertexShader;
class FRHIPixelShader;
typedef TRefCountPtr<FRHIVertexShader> FVertexShaderRHIRef;
typedef TRefCountPtr<FRHIPixelShader> FPixelShaderRHIRef;

namespace tgfx {

class UERHIGPU;

/**
 * UE RHI shader module implementation.
 * 
 * Handles shader compilation from GLSL to UE RHI compatible format.
 */
class UERHIShaderModule : public ShaderModule {
 public:
  /**
   * Creates a UE RHI shader module from GLSL source.
   * 
   * Compilation pipeline:
   *   GLSL (#version 450) → SPIR-V (shaderc) → HLSL (SPIRV-Cross) → UE Shader Bytecode
   * 
   * The compiled bytecode is cached by shader hash.
   */
  static std::shared_ptr<UERHIShaderModule> Make(UERHIGPU* gpu,
                                                  const ShaderModuleDescriptor& descriptor);

  ~UERHIShaderModule() override;

  /**
   * Returns the shader stage.
   */
  ShaderStage stage() const { return _stage; }

  /**
   * Returns the original GLSL source code.
   */
  const std::string& glslCode() const { return _glslCode; }

  /**
   * Returns the compiled SPIR-V binary.
   */
  const std::vector<uint32_t>& spirvBinary() const { return _spirvBinary; }

  /**
   * Returns the UE RHI vertex shader (only valid if stage == Vertex).
   */
  FRHIVertexShader* rhiVertexShader() const { return _vertexShader.GetReference(); }

  /**
   * Returns the UE RHI pixel shader (only valid if stage == Fragment).
   */
  FRHIPixelShader* rhiPixelShader() const { return _pixelShader.GetReference(); }

 private:
  UERHIShaderModule(ShaderStage stage, const std::string& glslCode);

  bool compile(UERHIGPU* gpu);

  ShaderStage _stage = ShaderStage::Vertex;
  std::string _glslCode;
  std::vector<uint32_t> _spirvBinary;

  FVertexShaderRHIRef _vertexShader;
  FPixelShaderRHIRef _pixelShader;
};

}  // namespace tgfx
```

### 14.2 Shader 编译管线

```
┌─────────────┐    shaderc    ┌──────────┐   SPIRV-Cross   ┌──────────┐
│ GLSL #450   │ ────────────→ │ SPIR-V   │ ──────────────→ │ HLSL     │
│ (tgfx 源码) │               │ (binary) │                 │ (source) │
└─────────────┘               └──────────┘                 └────┬─────┘
                                                                │
                                                  UE ShaderCompiler
                                                                │
                                                           ┌────▼─────┐
                                                           │ Platform │
                                                           │ Bytecode │
                                                           │ (DXIL/   │
                                                           │  SPIR-V/ │
                                                           │  metallib│
                                                           └──────────┘
```

### 14.3 实现要点

```cpp
bool UERHIShaderModule::compile(UERHIGPU* gpu) {
  // Step 1: GLSL → SPIR-V (复用 tgfx 现有的 shaderc 管线)
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);

  shaderc_shader_kind kind = (_stage == ShaderStage::Vertex)
                                 ? shaderc_vertex_shader
                                 : shaderc_fragment_shader;

  auto result = compiler.CompileGlslToSpv(_glslCode, kind, "shader", options);
  if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
    return false;
  }
  _spirvBinary.assign(result.cbegin(), result.cend());

  // Step 2: SPIR-V → HLSL (使用 SPIRV-Cross)
  spirv_cross::CompilerHLSL hlslCompiler(_spirvBinary);
  spirv_cross::CompilerHLSL::Options hlslOptions;
  hlslOptions.shader_model = 50;  // SM 5.0
  hlslCompiler.set_hlsl_options(hlslOptions);
  std::string hlslCode = hlslCompiler.compile();

  // Step 3: HLSL → UE Shader Bytecode
  // 注意: UE 不支持运行时 HLSL 编译
  // 方案 A: 使用 D3DCompile (仅 Windows)
  // 方案 B: 预编译 shader 库 (推荐)
  // 方案 C: 直接提交 SPIR-V (Vulkan 平台)

  TArray<uint8> shaderCode = CompileShaderForCurrentPlatform(hlslCode, _stage);

  FSHAHash hash;
  FSHA1::HashBuffer(shaderCode.GetData(), shaderCode.Num(), hash.Hash);

  if (_stage == ShaderStage::Vertex) {
    _vertexShader = RHICreateVertexShader(shaderCode, hash);
  } else {
    _pixelShader = RHICreatePixelShader(shaderCode, hash);
  }

  return (_vertexShader.IsValid() || _pixelShader.IsValid());
}
```

### 14.4 Shader 缓存策略

```cpp
// UERHIGPU 中维护 shader 缓存
class UERHIGPU {
  // 基于 GLSL 源码 hash 的缓存
  std::unordered_map<size_t, std::shared_ptr<UERHIShaderModule>> shaderCache;
  
  std::shared_ptr<ShaderModule> createShaderModule(const ShaderModuleDescriptor& descriptor) override {
    // 计算 hash
    size_t hash = std::hash<std::string>{}(descriptor.code);
    hash ^= static_cast<size_t>(descriptor.stage) << 32;
    
    auto it = shaderCache.find(hash);
    if (it != shaderCache.end()) {
      return it->second;
    }
    
    auto module = UERHIShaderModule::Make(this, descriptor);
    if (module) {
      shaderCache[hash] = module;
    }
    return module;
  }
};
```

---

## 15. UERHICaps

### 15.1 接口定义

**文件**: `src/gpu/ue/UERHICaps.h`

```cpp
#pragma once

#include "tgfx/gpu/GPUFeatures.h"
#include "tgfx/gpu/GPUInfo.h"
#include "tgfx/gpu/GPULimits.h"

namespace tgfx {

/**
 * Queries and caches UE RHI GPU capabilities.
 */
class UERHICaps {
 public:
  UERHICaps();

  const GPUInfo* info() const { return &_info; }
  const GPUFeatures* features() const { return &_features; }
  const GPULimits* limits() const { return &_limits; }

  bool isFormatRenderable(PixelFormat format) const;
  EPixelFormat getUEPixelFormat(PixelFormat format) const;

 private:
  void initialize();

  GPUInfo _info;
  GPUFeatures _features;
  GPULimits _limits;
};

}  // namespace tgfx
```

### 15.2 初始化逻辑

```cpp
void UERHICaps::initialize() {
  // GPUInfo
  _info.backend = Backend::UERHI;
  _info.version = TCHAR_TO_UTF8(*GDynamicRHI->GetName());
  _info.renderer = TCHAR_TO_UTF8(*GRHIAdapterName);
  _info.vendor = TCHAR_TO_UTF8(*GRHIAdapterInternalDriverVersion);

  // GPUFeatures
  _features.semaphore = true;  // UE RHI 支持 GPU Fence
  _features.clampToBorder = true;  // 所有现代 API 支持
  _features.textureBarrier = true;  // UE RHI 内部管理资源屏障

  // GPULimits
  _limits.maxTextureDimension2D = GMaxTextureDimensions;  // UE 全局变量
  _limits.maxSamplersPerShaderStage = 16;  // 保守估计
  _limits.maxUniformBufferBindingSize = 65536;  // 64KB
  _limits.minUniformBufferOffsetAlignment = 256;  // D3D12 要求
}
```

---

## 16. 枚举映射表

### 16.1 PixelFormat 映射

| tgfx PixelFormat | UE EPixelFormat | 说明 |
|-----------------|----------------|------|
| `ALPHA_8` | `PF_A8` | 8位 Alpha |
| `GRAY_8` | `PF_G8` | 8位灰度 |
| `RG_88` | `PF_R8G8` | 双通道 8 位 |
| `RGBA_8888` | `PF_R8G8B8A8` | 标准 RGBA |
| `BGRA_8888` | `PF_B8G8R8A8` | BGRA 字节序 |
| `DEPTH24_STENCIL8` | `PF_DepthStencil` | 深度 + 模板 |

```cpp
EPixelFormat UERHICaps::getUEPixelFormat(PixelFormat format) const {
  switch (format) {
    case PixelFormat::ALPHA_8: return PF_A8;
    case PixelFormat::GRAY_8: return PF_G8;
    case PixelFormat::RG_88: return PF_R8G8;
    case PixelFormat::RGBA_8888: return PF_R8G8B8A8;
    case PixelFormat::BGRA_8888: return PF_B8G8R8A8;
    case PixelFormat::DEPTH24_STENCIL8: return PF_DepthStencil;
    default: return PF_Unknown;
  }
}
```

### 16.2 BlendFactor 映射

| tgfx BlendFactor | UE EBlendFactor |
|-----------------|-----------------|
| `Zero` | `BF_Zero` |
| `One` | `BF_One` |
| `Src` | `BF_SourceColor` |
| `OneMinusSrc` | `BF_InverseSourceColor` |
| `Dst` | `BF_DestColor` |
| `OneMinusDst` | `BF_InverseDestColor` |
| `SrcAlpha` | `BF_SourceAlpha` |
| `OneMinusSrcAlpha` | `BF_InverseSourceAlpha` |
| `DstAlpha` | `BF_DestAlpha` |
| `OneMinusDstAlpha` | `BF_InverseDestAlpha` |

### 16.3 BlendOperation 映射

| tgfx BlendOperation | UE EBlendOperation |
|--------------------|-------------------|
| `Add` | `BO_Add` |
| `Subtract` | `BO_Subtract` |
| `ReverseSubtract` | `BO_ReverseSubtract` |
| `Min` | `BO_Min` |
| `Max` | `BO_Max` |

### 16.4 CompareFunction 映射

| tgfx CompareFunction | UE ECompareFunction |
|---------------------|-------------------|
| `Never` | `CF_Never` |
| `Less` | `CF_Less` |
| `Equal` | `CF_Equal` |
| `LessEqual` | `CF_LessEqual` |
| `Greater` | `CF_Greater` |
| `NotEqual` | `CF_NotEqual` |
| `GreaterEqual` | `CF_GreaterEqual` |
| `Always` | `CF_Always` |

### 16.5 StencilOperation 映射

| tgfx StencilOperation | UE EStencilOp |
|----------------------|--------------|
| `Keep` | `SO_Keep` |
| `Zero` | `SO_Zero` |
| `Replace` | `SO_Replace` |
| `Invert` | `SO_Invert` |
| `IncrementClamp` | `SO_SaturatedIncrement` |
| `DecrementClamp` | `SO_SaturatedDecrement` |
| `IncrementWrap` | `SO_Increment` |
| `DecrementWrap` | `SO_Decrement` |

### 16.6 CullMode 映射

| tgfx CullMode | UE ERasterizerCullMode |
|---------------|----------------------|
| `None` | `CM_None` |
| `Front` | `CM_CW` (需根据 FrontFace 调整) |
| `Back` | `CM_CCW` (需根据 FrontFace 调整) |

### 16.7 PrimitiveType 映射

| tgfx PrimitiveType | UE EPrimitiveType |
|-------------------|--------------------|
| `Triangles` | `PT_TriangleList` |
| `TriangleStrip` | `PT_TriangleStrip` |

### 16.8 VertexFormat 映射

| tgfx VertexFormat | UE EVertexElementType |
|------------------|-----------------------|
| `Float` | `VET_Float1` |
| `Float2` | `VET_Float2` |
| `Float3` | `VET_Float3` |
| `Float4` | `VET_Float4` |
| `Half2` | `VET_Half2` |
| `Half4` | `VET_Half4` |
| `Int` | `VET_UInt` |
| `UByte4Normalized` | `VET_UByte4N` |

### 16.9 LoadAction / StoreAction 映射

| tgfx LoadAction + StoreAction | UE ERenderTargetActions |
|------------------------------|------------------------|
| `DontCare` + `Store` | `DontLoad_Store` |
| `DontCare` + `DontCare` | `DontLoad_DontStore` |
| `Load` + `Store` | `Load_Store` |
| `Load` + `DontCare` | `Load_DontStore` |
| `Clear` + `Store` | `Clear_Store` |
| `Clear` + `DontCare` | `Clear_DontStore` |

```cpp
ERenderTargetActions ConvertRenderTargetActions(LoadAction load, StoreAction store) {
  if (load == LoadAction::DontCare && store == StoreAction::Store)
    return ERenderTargetActions::DontLoad_Store;
  if (load == LoadAction::DontCare && store == StoreAction::DontCare)
    return ERenderTargetActions::DontLoad_DontStore;
  if (load == LoadAction::Load && store == StoreAction::Store)
    return ERenderTargetActions::Load_Store;
  if (load == LoadAction::Load && store == StoreAction::DontCare)
    return ERenderTargetActions::Load_DontStore;
  if (load == LoadAction::Clear && store == StoreAction::Store)
    return ERenderTargetActions::Clear_Store;
  if (load == LoadAction::Clear && store == StoreAction::DontCare)
    return ERenderTargetActions::Clear_DontStore;
  return ERenderTargetActions::DontLoad_DontStore;
}
```

### 16.10 AddressMode 映射

| tgfx AddressMode | UE ESamplerAddressMode |
|-----------------|----------------------|
| `ClampToEdge` | `AM_Clamp` |
| `Repeat` | `AM_Wrap` |
| `MirrorRepeat` | `AM_Mirror` |
| `ClampToBorder` | `AM_Border` |

### 16.11 FilterMode 映射

| tgfx FilterMode (min/mag) + MipmapMode | UE ESamplerFilter |
|----------------------------------------|-------------------|
| `Nearest` + `None` | `SF_Point` |
| `Linear` + `None` | `SF_Bilinear` |
| `Linear` + `Nearest` | `SF_Bilinear` |
| `Linear` + `Linear` | `SF_Trilinear` |
| `Nearest` + `Nearest` | `SF_Point` |

---

## 17. libpag UE 集成层

### 17.1 UEDrawable

**文件**: `libpag/src/platform/ue/UEDrawable.h`

```cpp
#pragma once

#include "rendering/drawables/Drawable.h"

class FRHITexture;
typedef TRefCountPtr<FRHITexture> FTextureRHIRef;

namespace pag {

/**
 * UE RHI Drawable for rendering PAG content to a UE texture.
 *
 * This drawable creates a tgfx::Surface backed by a UE RHI texture,
 * allowing PAG rendering results to be directly used in UE without
 * any texture copy.
 */
class UEDrawable : public Drawable {
 public:
  /**
   * Creates a UEDrawable with the specified size.
   * The drawable creates its own UE RHI texture as the render target.
   *
   * Must be called from the UE Render Thread.
   */
  static std::shared_ptr<UEDrawable> Make(int width, int height,
                                           std::shared_ptr<tgfx::Device> device);

  /**
   * Creates a UEDrawable from an existing UE RHI texture.
   * PAG will render directly to this texture.
   */
  static std::shared_ptr<UEDrawable> MakeFrom(FTextureRHIRef texture,
                                               std::shared_ptr<tgfx::Device> device);

  int width() const override { return _width; }
  int height() const override { return _height; }
  std::shared_ptr<tgfx::Device> getDevice() override { return device; }

  /**
   * Returns the underlying UE RHI texture for UMG display.
   */
  FRHITexture* getRHITexture() const;

 protected:
  std::shared_ptr<tgfx::Surface> onCreateSurface(tgfx::Context* context) override;

 private:
  UEDrawable(int width, int height, std::shared_ptr<tgfx::Device> device,
             FTextureRHIRef externalTexture = nullptr);

  int _width = 0;
  int _height = 0;
  std::shared_ptr<tgfx::Device> device = nullptr;
  FTextureRHIRef externalTexture = nullptr;  // 外部提供的纹理 (可选)
};

}  // namespace pag
```

### 17.2 UEDrawable 实现

```cpp
std::shared_ptr<tgfx::Surface> UEDrawable::onCreateSurface(tgfx::Context* context) {
  if (externalTexture.IsValid()) {
    // 使用外部纹理
    tgfx::UERHITextureInfo info;
    info.texture = externalTexture.GetReference();
    tgfx::BackendTexture backendTexture(info, _width, _height);
    return tgfx::Surface::MakeFrom(context, backendTexture, tgfx::ImageOrigin::TopLeft);
  }

  // 创建内部纹理
  return tgfx::Surface::Make(context, _width, _height, tgfx::ColorType::RGBA_8888);
}

FRHITexture* UEDrawable::getRHITexture() const {
  if (externalTexture.IsValid()) {
    return externalTexture.GetReference();
  }
  if (surface == nullptr) {
    return nullptr;
  }
  // 从 tgfx::Surface 获取底层 UE 纹理
  tgfx::BackendTexture bt = surface->getBackendTexture();
  tgfx::UERHITextureInfo info;
  if (bt.getUERHITextureInfo(&info)) {
    return info.texture;
  }
  return nullptr;
}
```

### 17.3 UPAGWidget (UMG Widget)

**文件**: `PAGPlugin/Source/PAGPlugin/Public/UPAGWidget.h`

```cpp
#pragma once

#include "Components/Widget.h"
#include "UPAGWidget.generated.h"

class UPAGPlayer;

/**
 * UMG Widget for displaying PAG animation content.
 *
 * Usage in Blueprint:
 *   1. Add UPAGWidget to your UMG panel
 *   2. Set PAG file path
 *   3. Call Play()
 */
UCLASS()
class PAGPLUGIN_API UPAGWidget : public UWidget {
  GENERATED_BODY()

 public:
  UPAGWidget(const FObjectInitializer& ObjectInitializer);

  /** Set the PAG file to display. */
  UFUNCTION(BlueprintCallable, Category = "PAG")
  void SetPAGFile(const FString& FilePath);

  /** Set progress (0.0 - 1.0). */
  UFUNCTION(BlueprintCallable, Category = "PAG")
  void SetProgress(float Progress);

  /** Start playback. */
  UFUNCTION(BlueprintCallable, Category = "PAG")
  void Play();

  /** Stop playback. */
  UFUNCTION(BlueprintCallable, Category = "PAG")
  void Stop();

  /** Get the current progress. */
  UFUNCTION(BlueprintCallable, Category = "PAG")
  float GetProgress() const;

  /** Get the PAG file duration in seconds. */
  UFUNCTION(BlueprintCallable, Category = "PAG")
  float GetDuration() const;

 protected:
  virtual TSharedRef<SWidget> RebuildWidget() override;
  virtual void ReleaseSlateResources(bool bReleaseChildren) override;
  virtual void SynchronizeProperties() override;

  void Flush();  // 在 Render Thread 执行 PAG 渲染

 private:
  /** Path to the PAG file. */
  UPROPERTY(EditAnywhere, Category = "PAG")
  FString PAGFilePath;

  // PAG 核心对象
  std::shared_ptr<pag::PAGPlayer> pagPlayer = nullptr;
  std::shared_ptr<pag::PAGSurface> pagSurface = nullptr;
  std::shared_ptr<pag::UEDrawable> drawable = nullptr;

  // Slate 纹理
  TSharedPtr<FSlateTexture2DRHIRef> slateTexture = nullptr;

  // 播放状态
  bool bIsPlaying = false;
  float currentProgress = 0.0f;
};
```

### 17.4 UPAGWidget 渲染流程

```cpp
void UPAGWidget::Flush() {
  if (!pagPlayer || !pagSurface) return;

  // 在 Render Thread 执行 PAG 渲染
  ENQUEUE_RENDER_COMMAND(PAGFlush)([this](FRHICommandListImmediate& RHICmdList) {
    // 1. 锁定 tgfx Device
    auto* context = drawable->getDevice()->lockContext();
    if (!context) return;

    // 2. PAG 渲染
    pagPlayer->flush();

    // 3. 解锁
    drawable->getDevice()->unlock();

    // 4. 更新 Slate 纹理引用
    FRHITexture* rhiTexture = drawable->getRHITexture();
    if (rhiTexture && slateTexture.IsValid()) {
      FTextureRHIRef textureRef = rhiTexture;
      slateTexture->SetRHIRef(textureRef, drawable->width(), drawable->height());
    }
  });
}
```

---

## 18. 线程模型与生命周期

### 18.1 线程模型

```
┌──────────────────┐     ENQUEUE_RENDER_COMMAND    ┌──────────────────────┐
│  UE Game Thread  │ ──────────────────────────────→│  UE Render Thread    │
│                  │                                │                      │
│  UPAGWidget      │                                │  UERHIDevice         │
│    .SetProgress()│                                │    .lockContext()     │
│    .Flush()      │                                │  PAGPlayer.flush()   │
│                  │                                │    → tgfx Canvas     │
│                  │                                │    → UERHIRenderPass │
│                  │                                │    → RHI 调用        │
│                  │                                │  UERHIDevice         │
│                  │                                │    .unlock()         │
└──────────────────┘                                └──────────────────────┘
```

**线程约束**:
- `UERHIDevice::lockContext()` 必须在 Render Thread 调用
- 所有 tgfx GPU 操作必须在 lockContext/unlock 之间
- `PAGPlayer` 的非 GPU 操作（如 prepare）可以在 Game Thread
- `PAGPlayer::flush()` 必须在 Render Thread

### 18.2 资源生命周期

```
                              创建                                 销毁
UERHITexture  ────→ std::shared_ptr (引用计数) ────→ shared_ptr 归零
                    FTextureRHIRef (UE 引用计数)      UE 延迟删除队列

UERHIBuffer   ────→ std::shared_ptr ────→ FBufferRHIRef 释放

UERHISampler  ────→ std::shared_ptr + GPU 缓存 ────→ GPU 销毁时清理
```

**关键原则**:
1. tgfx 资源通过 `std::shared_ptr` 管理生命周期
2. UE RHI 资源通过 `TRefCountPtr` (即 `FTextureRHIRef` 等) 管理
3. 外部纹理 (adopted=false)：tgfx 不释放底层 UE 资源
4. 内部纹理 (adopted=true)：tgfx shared_ptr 归零时，UE TRefCountPtr 随之释放
5. Sampler 在 UERHIGPU 中缓存，UERHIGPU 析构时统一清理

### 18.3 错误处理

```
GPU 资源创建失败 → 返回 nullptr → 上层检查并处理
RenderPass 开始失败 → beginRenderPass 返回 nullptr
Shader 编译失败 → createShaderModule 返回 nullptr
Context 锁定失败 → lockContext 返回 nullptr → 跳过本帧渲染
```

---

## 19. Shader 编译策略

> **重要更新**：本章原始设计以"运行时编译"（策略 B）为主要方案。基于 Rive UE 5.7 插件源码分析，
> **推荐全面采用策略 A（UE Global Shader 预编译）**，使用 Shader Permutation 替代运行时动态拼装。
> 详见附录 O 的完整方案。以下原始分析保留作为背景参考。

### 19.1 问题分析

UE RHI 的 `RHICreateVertexShader/RHICreatePixelShader` 接口接收**预编译的平台字节码** (`TArrayView<const uint8>`)，不支持运行时源码编译。

tgfx 统一使用 GLSL `#version 450` 编写 shader，需要转换为 UE 兼容格式。

### 19.2 推荐方案：混合编译策略

```
                     构建时 (离线)                        运行时
┌────────────┐                              ┌──────────────────────┐
│ tgfx GLSL  │                              │ tgfx GLSL (动态生成) │
│ (已知源码) │                              │ (如 effect shader)   │
└─────┬──────┘                              └──────────┬───────────┘
      │                                                │
      ▼                                                ▼
 预编译为 UE                                   GLSL → SPIR-V (shaderc)
 Shader Asset                                        │
 (.usf / .ush)                                       ▼
      │                                     SPIR-V → HLSL (SPIRV-Cross)
      ▼                                              │
 UE Cook Pipeline                                    ▼
      │                                     D3DCompile / dxc (仅 Windows)
      ▼                                     或提交 SPIR-V (Vulkan)
 平台字节码                                          │
 (DXIL/SPIRV/metallib)                              ▼
      │                                     平台字节码 (缓存到磁盘)
      ▼                                              │
 RHICreateShader                                     ▼
                                            RHICreateShader
```

**策略 A: 静态 Shader（推荐大部分场景）**
- 构建时将 tgfx 已知的 shader 预编译为 UE Shader Asset
- 运行时直接从 Shader Library 加载
- 优点：零运行时编译开销，与 UE PSO 缓存完全兼容
- 缺点：新增 shader 需要重新构建

**策略 B: 运行时编译（动态 shader 场景）**
- 使用 shaderc + SPIRV-Cross 在运行时编译
- 编译结果缓存到磁盘，后续直接加载
- 优点：灵活
- 缺点：首次编译有延迟，平台兼容性需要验证

**推荐**：优先使用策略 A，策略 B 作为后备。

### 19.3 UE Shader 集成方式

```cpp
// 在 UE 的 .usf 文件中预定义 tgfx shader 变体
// PAGPlugin/Shaders/Private/TGFXShader.usf

// 或者，使用 UE 的 Global Shader 机制
class FTGFXVertexShader : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTGFXVertexShader);
  // ...
};

class FTGFXPixelShader : public FGlobalShader {
  DECLARE_GLOBAL_SHADER(FTGFXPixelShader);
  // ...
};

// 运行时获取编译好的 shader
TShaderMapRef<FTGFXVertexShader> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
TShaderMapRef<FTGFXPixelShader> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
```

---

## 附录 A: 类依赖关系

```
UERHIDevice
  ├── owns → UERHIGPU
  │           ├── owns → UERHICommandQueue
  │           ├── owns → UERHICaps
  │           ├── creates → UERHITexture
  │           ├── creates → UERHIBuffer
  │           ├── creates → UERHISampler (cached)
  │           ├── creates → UERHIShaderModule (cached)
  │           ├── creates → UERHIRenderPipeline
  │           └── creates → UERHICommandEncoder
  │                          ├── creates → UERHIRenderPass
  │                          └── creates → UERHICommandBuffer
  └── refs → FRHICommandListImmediate (UE owned)
```

## 附录 B: 与 Metal 后端的对应关系

| Metal 后端类 | UE RHI 后端类 | 封装的 UE 类型 |
|-------------|--------------|--------------|
| `MetalDevice` | `UERHIDevice` | GDynamicRHI |
| `MetalGPU` | `UERHIGPU` | FDynamicRHI |
| `MetalCommandQueue` | `UERHICommandQueue` | FRHICommandListImmediate |
| `MetalCommandEncoder` | `UERHICommandEncoder` | (immediate mode) |
| `MetalCommandBuffer` | `UERHICommandBuffer` | (marker only) |
| `MetalRenderPass` | `UERHIRenderPass` | IRHICommandContext |
| `MetalRenderPipeline` | `UERHIRenderPipeline` | FGraphicsPipelineState |
| `MetalTexture` | `UERHITexture` | FRHITexture |
| `MetalBuffer` | `UERHIBuffer` | FRHIBuffer |
| `MetalSampler` | `UERHISampler` | FRHISamplerState |
| `MetalSemaphore` | `UERHISemaphore` | FRHIGPUFence |
| `MetalShaderModule` | `UERHIShaderModule` | FRHIVertexShader/PixelShader |
| `MetalCaps` | `UERHICaps` | GRHIGlobals |

## 附录 C: 预估代码量

| 文件 | 预估行数 | 说明 |
|------|---------|------|
| UERHIDevice.h/cpp | ~150 | 设备管理 |
| UERHIGPU.h/cpp | ~500 | 核心工厂类 |
| UERHICommandQueue.h/cpp | ~300 | 命令队列 |
| UERHICommandEncoder.h/cpp | ~250 | 命令编码 |
| UERHICommandBuffer.h | ~30 | 标记类 |
| UERHIRenderPass.h/cpp | ~500 | 渲染通道（最复杂） |
| UERHIRenderPipeline.h/cpp | ~400 | PSO 管理 |
| UERHITexture.h/cpp | ~200 | 纹理封装 |
| UERHIBuffer.h/cpp | ~150 | 缓冲封装 |
| UERHISampler.h/cpp | ~100 | 采样器封装 |
| UERHISemaphore.h/cpp | ~100 | 同步原语 |
| UERHIShaderModule.h/cpp | ~400 | Shader 编译 |
| UERHICaps.h/cpp | ~200 | GPU 能力查询 |
| UERHITypes.h | ~50 | 类型定义 |
| **tgfx 侧合计** | **~3,330** | |
| UEDrawable.h/cpp | ~200 | libpag Drawable |
| UPAGWidget.h/cpp | ~400 | UMG Widget |
| **libpag 侧合计** | **~600** | |
| **总计** | **~3,930** | |

---

## 附录 D: [P0] Shader 编译完整方案

> **重要更新**：本附录描述的运行时编译管线（GLSL -> SPIR-V -> HLSL -> D3DCompile）已不再是推荐
> 方案。基于 Rive UE 5.7 的实践，推荐使用 UE Global Shader + Permutation 预编译方案（附录 O）。
> 本附录保留作为备选方案参考。

### D.1 问题本质

tgfx 的 shader 是**运行时动态生成**的：`ProgramBuilder` 根据 DrawOp 的 GeometryProcessor + FragmentProcessor 组合，在运行时拼接 GLSL 源码（`#version 450`，Vulkan 风格），然后调用 `GPU::createShaderModule()` 编译。

UE RHI 的 `RHICreateVertexShader(TArrayView<const uint8> Code, const FSHAHash& Hash)` 接收**预编译的平台字节码**（D3D12 平台为 DXBC/DXIL，Vulkan 为 SPIR-V，Metal 为 metallib），**不支持运行时 HLSL 源码编译**。

因此，UE RHI 后端的核心挑战是：如何将 tgfx 运行时生成的 GLSL 转换为 UE RHI 接受的平台字节码。

### D.2 最终方案：绕过 UE RHI Shader 接口，直接使用平台原生编译

**核心思路**：`UERHIShaderModule::Make()` 不调用 `RHICreateVertexShader/RHICreatePixelShader`，而是利用 UE 已集成的编译器工具链，在运行时将 GLSL 编译为平台原生字节码，再通过平台特定 RHI 接口创建 shader。

**编译管线**（按平台分支）：

```
                        tgfx GLSL (#version 450)
                                │
                        shaderc (已集成于 tgfx)
                                │
                            SPIR-V
                      ┌─────────┼─────────┐
                      │         │         │
                 [D3D12]    [Vulkan]   [Metal]
                      │         │         │
              SPIRV-Cross   直接使用   SPIRV-Cross
              (→ HLSL)      SPIR-V    (→ MSL)
                      │         │         │
              D3DCompile    UE RHI    MTLLibrary
              /DXC          直接      编译
              (运行时)      接受       (运行时)
                      │         │         │
                   DXBC/     SPIR-V    metallib
                   DXIL       字节码    字节码
                      │         │         │
                      └─────────┼─────────┘
                                │
                    平台特定 RHI 创建 Shader
```

### D.3 各平台实现细节

#### D.3.1 D3D12 平台

```cpp
// 使用 d3dcompiler_47.dll 或 dxcompiler.dll 运行时编译
// UE StandaloneRenderer 中已有此模式的先例

class UERHIShaderCompiler_D3D12 {
public:
  static bool Compile(const std::string& glslCode, ShaderStage stage,
                      TArray<uint8>& outBytecode) {
    // Step 1: GLSL → SPIR-V (shaderc)
    std::vector<uint32_t> spirv = CompileGLSLToSPIRV(glslCode, stage);
    if (spirv.empty()) return false;

    // Step 2: SPIR-V → HLSL (SPIRV-Cross)
    spirv_cross::CompilerHLSL hlslCompiler(spirv);
    spirv_cross::CompilerHLSL::Options options;
    options.shader_model = 50;  // SM 5.0 for DXBC, 或 60+ for DXIL
    hlslCompiler.set_hlsl_options(options);
    std::string hlslCode = hlslCompiler.compile();

    // Step 3: HLSL → DXBC (D3DCompile)
    // 动态加载 d3dcompiler_47.dll (UE 已包含此 DLL)
    HMODULE hCompiler = LoadLibraryW(L"d3dcompiler_47.dll");
    auto pD3DCompile = (pD3DCompile_t)GetProcAddress(hCompiler, "D3DCompile");

    const char* target = (stage == ShaderStage::Vertex) ? "vs_5_0" : "ps_5_0";
    const char* entry = "main";

    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = pD3DCompile(
        hlslCode.c_str(), hlslCode.size(), nullptr, nullptr, nullptr,
        entry, target,
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        &shaderBlob, &errorBlob);

    if (FAILED(hr)) {
      // 记录 errorBlob 中的错误信息
      if (errorBlob) errorBlob->Release();
      return false;
    }

    outBytecode.SetNum(shaderBlob->GetBufferSize());
    FMemory::Memcpy(outBytecode.GetData(), shaderBlob->GetBufferPointer(),
                    shaderBlob->GetBufferSize());
    shaderBlob->Release();
    return true;
  }
};
```

**创建 Shader（绕过 UE 序列化格式，直接使用平台 API）**：

```cpp
// 方式 A: 通过 UE 平台特定 RHI 接口
ID3D12DynamicRHI* d3d12RHI = GetID3D12DynamicRHI();
ID3D12Device* d3dDevice = d3d12RHI->RHIGetDevice(0);

// 直接用 D3D12 bytecode 创建 PSO（在 RenderPipeline 阶段处理，而非 ShaderModule）
// PSO 创建时直接嵌入 D3D12_SHADER_BYTECODE { pShaderBytecode, BytecodeLength }
```

```cpp
// 方式 B: 构造 UE 序列化格式 (更符合 UE 架构)
// UE 的 shader Code 格式 = ShaderResourceTable header + optional data + native bytecode
// 可以构造最简的 SRT header + 编译后的字节码
TArray<uint8> MakeUEShaderCode(const TArray<uint8>& nativeBytecode) {
  TArray<uint8> ueCode;
  // 写入最小化的 FShaderResourceTable 序列化数据
  FMemoryWriter Ar(ueCode);
  FShaderResourceTable srt;
  srt.Serialize(Ar);
  // 追加原生字节码
  ueCode.Append(nativeBytecode);
  return ueCode;
}
```

#### D.3.2 Vulkan 平台

```cpp
// Vulkan 原生支持 SPIR-V，最简单的路径
class UERHIShaderCompiler_Vulkan {
public:
  static bool Compile(const std::string& glslCode, ShaderStage stage,
                      TArray<uint8>& outBytecode) {
    // Step 1: GLSL → SPIR-V (shaderc)
    std::vector<uint32_t> spirv = CompileGLSLToSPIRV(glslCode, stage);
    if (spirv.empty()) return false;

    // SPIR-V 就是 Vulkan 的原生格式
    outBytecode.SetNum(spirv.size() * sizeof(uint32_t));
    FMemory::Memcpy(outBytecode.GetData(), spirv.data(), outBytecode.Num());
    return true;
  }
};
```

#### D.3.3 Metal 平台

```cpp
// 复用 tgfx Metal 后端已有的 GLSL → MSL 管线
class UERHIShaderCompiler_Metal {
public:
  static bool Compile(const std::string& glslCode, ShaderStage stage,
                      TArray<uint8>& outBytecode) {
    // Step 1: GLSL → SPIR-V (shaderc)
    std::vector<uint32_t> spirv = CompileGLSLToSPIRV(glslCode, stage);

    // Step 2: SPIR-V → MSL (SPIRV-Cross)
    spirv_cross::CompilerMSL mslCompiler(spirv);
    // ... 配置选项 (与 MetalShaderModule 相同)
    std::string mslCode = mslCompiler.compile();

    // Step 3: MSL → metallib (MTLDevice 运行时编译)
    // Metal 原生支持运行时编译 MSL
    IMetalDynamicRHI* metalRHI = GetIMetalDynamicRHI();
    // 通过 metalRHI 获取 MTLDevice 并编译
    // 注意：此路径与 tgfx MetalShaderModule 完全一致
    return true;
  }
};
```

### D.4 统一 ShaderModule 接口

```cpp
// src/gpu/ue/UERHIShaderModule.h

class UERHIShaderModule : public ShaderModule {
 public:
  static std::shared_ptr<UERHIShaderModule> Make(UERHIGPU* gpu,
                                                  const ShaderModuleDescriptor& descriptor);

  ShaderStage stage() const { return _stage; }
  const std::string& glslCode() const { return _glslCode; }
  const std::vector<uint32_t>& spirvBinary() const { return _spirvBinary; }

  // D3D12 编译产物
  const TArray<uint8>& dxBytecode() const { return _dxBytecode; }

  // Vulkan 编译产物 (= SPIR-V)
  const TArray<uint8>& vkBytecode() const { return _vkBytecode; }

  // Metal 编译产物 (MSL 源码，运行时编译为 MTLLibrary)
  const std::string& mslCode() const { return _mslCode; }

  // SPIRV-Cross 反射信息 (用于参数绑定)
  const ShaderReflection& reflection() const { return _reflection; }

 private:
  ShaderStage _stage;
  std::string _glslCode;
  std::vector<uint32_t> _spirvBinary;

  // 平台特定编译产物 (仅当前平台有效)
  TArray<uint8> _dxBytecode;   // D3D12
  TArray<uint8> _vkBytecode;   // Vulkan
  std::string _mslCode;         // Metal

  // 反射信息
  ShaderReflection _reflection;
};
```

### D.5 Shader 缓存

```cpp
// UERHIGPU 中基于 GLSL hash 的二级缓存

class UERHIGPU {
  // 内存缓存 (当前会话)
  std::unordered_map<size_t, std::shared_ptr<UERHIShaderModule>> shaderMemoryCache;

  // 磁盘缓存 (跨会话持久化)
  // 缓存路径: {ProjectDir}/Saved/PAGShaderCache/{hash}.bin
  FString shaderCacheDir;

  std::shared_ptr<ShaderModule> createShaderModule(
      const ShaderModuleDescriptor& descriptor) override {
    // 1. 计算 hash
    size_t hash = CityHash64(descriptor.code.data(), descriptor.code.size());
    hash ^= static_cast<size_t>(descriptor.stage) << 48;

    // 2. 内存缓存查找
    auto it = shaderMemoryCache.find(hash);
    if (it != shaderMemoryCache.end()) {
      return it->second;
    }

    // 3. 磁盘缓存查找
    FString cachePath = shaderCacheDir / FString::Printf(TEXT("%llx.bin"), hash);
    TArray<uint8> cachedBytecode;
    if (FFileHelper::LoadFileToArray(cachedBytecode, *cachePath)) {
      auto module = UERHIShaderModule::MakeFromCache(this, descriptor, cachedBytecode);
      if (module) {
        shaderMemoryCache[hash] = module;
        return module;
      }
    }

    // 4. 运行时编译
    auto module = UERHIShaderModule::Make(this, descriptor);
    if (module) {
      shaderMemoryCache[hash] = module;
      // 持久化到磁盘
      FFileHelper::SaveArrayToFile(module->platformBytecode(), *cachePath);
    }
    return module;
  }
};
```

### D.6 SPIRV-Cross 反射信息提取

Shader 参数绑定需要知道 HLSL 中各参数的寄存器分配。SPIRV-Cross 在转换 SPIR-V → HLSL 时提供完整的反射信息：

```cpp
struct ShaderReflection {
  // Uniform Buffer 绑定信息
  struct UBOBinding {
    std::string name;        // "VertexUniformBlock" / "FragmentUniformBlock"
    unsigned binding;        // SPIR-V binding point (0 或 1)
    unsigned hlslRegister;   // HLSL 中的 cb 寄存器 (b0, b1)
    size_t size;             // buffer 大小
  };
  std::vector<UBOBinding> uniformBuffers;

  // 纹理绑定信息
  struct TextureBinding {
    std::string name;        // sampler 名称
    unsigned binding;        // SPIR-V binding point (从 2 开始)
    unsigned hlslTextureRegister;  // HLSL 中的 t 寄存器
    unsigned hlslSamplerRegister;  // HLSL 中的 s 寄存器
  };
  std::vector<TextureBinding> textures;
};

// 从 SPIRV-Cross 提取
ShaderReflection ExtractReflection(spirv_cross::CompilerHLSL& compiler) {
  ShaderReflection reflection;

  // 提取 UBO 信息
  auto resources = compiler.get_shader_resources();
  for (auto& ubo : resources.uniform_buffers) {
    ShaderReflection::UBOBinding binding;
    binding.name = compiler.get_name(ubo.id);
    binding.binding = compiler.get_decoration(ubo.id, spv::DecorationBinding);
    binding.hlslRegister = compiler.get_decoration(ubo.id, spv::DecorationBinding);
    // SPIRV-Cross 默认将 SPIR-V binding N 映射到 HLSL register bN
    auto& type = compiler.get_type(ubo.base_type_id);
    binding.size = compiler.get_declared_struct_size(type);
    reflection.uniformBuffers.push_back(binding);
  }

  // 提取纹理信息
  for (auto& tex : resources.sampled_images) {
    ShaderReflection::TextureBinding binding;
    binding.name = compiler.get_name(tex.id);
    binding.binding = compiler.get_decoration(tex.id, spv::DecorationBinding);
    binding.hlslTextureRegister = compiler.get_decoration(tex.id, spv::DecorationBinding);
    binding.hlslSamplerRegister = compiler.get_decoration(tex.id, spv::DecorationBinding);
    reflection.textures.push_back(binding);
  }

  return reflection;
}
```

---

## 附录 E: [P0] Uniform Buffer 和纹理参数绑定完整方案

### E.1 tgfx Uniform 绑定模型回顾

tgfx 使用以下固定的绑定布局：

```
binding 0: VertexUniformBlock   (UBO, std140 布局, 仅 vertex shader 可见)
binding 1: FragmentUniformBlock (UBO, std140 布局, 仅 fragment shader 可见)
binding 2: 第 1 个纹理 sampler  (combined image sampler)
binding 3: 第 2 个纹理 sampler
binding N: 第 (N-1) 个纹理 sampler
```

数据流：
```
1. ProgramBuilder 生成 GLSL，确定 uniform 名称和类型
2. UniformData 按 std140 规则计算内存布局 (offset, size, alignment)
3. 各 Processor::setData() 将数据写入 UniformData 的内存 buffer
4. ProgramInfo::getUniformBuffer() 从 GPU 分配 UBO，将 UniformData 写入
5. renderPass->setUniformBuffer(binding, buffer, offset, size) 绑定 UBO
6. renderPass->setTexture(binding, texture, sampler) 绑定纹理
```

### E.2 UE RHI 参数绑定方案

#### 方案选择：直接使用 FRHIBatchedShaderParameters

UE RHI 提供了 `FRHIBatchedShaderParameters` 用于批量设置 shader 参数。这比操作 `FRHIUniformBuffer` 更灵活，可以直接映射 tgfx 的 UBO 数据。

#### E.2.1 UERHIRenderPass::setUniformBuffer 实现

```cpp
void UERHIRenderPass::setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer,
                                       size_t offset, size_t size) {
  // 状态缓存检查
  if (binding < MaxUniformBindings &&
      lastUniformBuffers[binding] == buffer.get() &&
      lastUniformOffsets[binding] == offset) {
    return;
  }
  if (binding < MaxUniformBindings) {
    lastUniformBuffers[binding] = buffer.get();
    lastUniformOffsets[binding] = offset;
  }

  // 获取 UBO 中的原始数据
  auto ueBuffer = std::static_pointer_cast<UERHIBuffer>(buffer);
  const uint8* uboData = static_cast<const uint8*>(ueBuffer->mappedData()) + offset;

  // 根据 binding point 确定目标 shader stage
  if (binding == VERTEX_UBO_BINDING_POINT && currentPipeline) {
    // 将 UBO 数据作为 constant buffer 绑定到 vertex shader
    FRHIBatchedShaderParameters& batchedParams = rhiCmdList->GetScratchShaderParameters();
    // 整块 UBO 数据作为 constant buffer 参数 (BufferIndex=0, BaseIndex=0)
    batchedParams.SetShaderParameter(0, 0, size, uboData);
    auto* vertexShader = currentPipeline->getVertexShaderRHI();
    rhiCmdList->SetBatchedShaderParameters(vertexShader, batchedParams);
  }
  else if (binding == FRAGMENT_UBO_BINDING_POINT && currentPipeline) {
    FRHIBatchedShaderParameters& batchedParams = rhiCmdList->GetScratchShaderParameters();
    batchedParams.SetShaderParameter(0, 0, size, uboData);
    auto* pixelShader = currentPipeline->getPixelShaderRHI();
    rhiCmdList->SetBatchedShaderParameters(pixelShader, batchedParams);
  }
}
```

#### E.2.2 替代方案：使用 Constant Buffer 直接绑定

如果 `SetShaderParameter` 的 loose parameter 方式不适用于大块 UBO 数据，可以改用 `FRHIUniformBuffer`：

```cpp
void UERHIRenderPass::setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer,
                                       size_t offset, size_t size) {
  auto ueBuffer = std::static_pointer_cast<UERHIBuffer>(buffer);

  // 通过平台特定 RHI 直接绑定 constant buffer
  // D3D12: 绑定到 root signature 的 CBV slot
  // Vulkan: 绑定到 descriptor set 的 UBO binding
  // Metal: setVertexBuffer / setFragmentBuffer

  if (binding == VERTEX_UBO_BINDING_POINT) {
    // 使用 RHISetShaderParameters 中的 UniformBuffer 资源绑定
    FRHIBatchedShaderParameters& params = rhiCmdList->GetScratchShaderParameters();
    // 创建临时 uniform buffer 包装
    // 或直接 bind buffer as constant buffer view
    params.SetShaderUniformBuffer(binding, CreateTempUniformBuffer(ueBuffer, offset, size));
    rhiCmdList->SetBatchedShaderParameters(
        currentPipeline->getVertexShaderRHI(), params);
  }
  // ... 类似处理 fragment
}
```

#### E.2.3 最终推荐方案：平台特定 Constant Buffer 绑定

由于 UE RHI 的 `FRHIBatchedShaderParameters::SetShaderParameter` 是为 loose parameters 设计的（单个标量/向量），不适合直接传递大块 UBO 数据，**推荐通过平台特定 RHI 接口直接绑定 constant buffer**：

```cpp
void UERHIRenderPass::setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer,
                                       size_t offset, size_t size) {
  auto ueBuffer = std::static_pointer_cast<UERHIBuffer>(buffer);
  FRHIBuffer* rhiBuffer = ueBuffer->rhiBuffer();

  // 方式: 通过 RHISetStreamSource 的变体或直接平台绑定
  // 实际上，UE 中 constant buffer 的标准做法是使用 FRHIUniformBuffer
  // 但这里 tgfx 的 UBO 是动态更新的，最佳方式是：

  // 1. 将 GPUBuffer 中的数据拷贝到 FRHIUniformBuffer
  FRHIUniformBufferLayoutInitializer layout;
  layout.ConstantBufferSize = size;
  FUniformBufferRHIRef uniformBuffer = RHICreateUniformBuffer(
      static_cast<const uint8*>(ueBuffer->mappedData()) + offset,
      layout,
      UniformBuffer_SingleFrame);  // 单帧生命周期

  // 2. 通过 batched parameters 绑定
  FRHIBatchedShaderParameters& params = rhiCmdList->GetScratchShaderParameters();
  params.SetShaderUniformBuffer(binding, uniformBuffer);

  if (binding == VERTEX_UBO_BINDING_POINT) {
    rhiCmdList->SetBatchedShaderParameters(
        currentPipeline->getVertexShaderRHI(), params);
  } else if (binding == FRAGMENT_UBO_BINDING_POINT) {
    rhiCmdList->SetBatchedShaderParameters(
        currentPipeline->getPixelShaderRHI(), params);
  }
}
```

### E.3 纹理和采样器绑定

```cpp
void UERHIRenderPass::setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                                 std::shared_ptr<Sampler> sampler) {
  // 状态缓存
  if (binding < MaxTextureBindings &&
      lastTextures[binding] == texture.get() &&
      lastSamplers[binding] == sampler.get()) {
    return;
  }
  if (binding < MaxTextureBindings) {
    lastTextures[binding] = texture.get();
    lastSamplers[binding] = sampler.get();
  }

  auto ueTexture = std::static_pointer_cast<UERHITexture>(texture);
  auto ueSampler = std::static_pointer_cast<UERHISampler>(sampler);

  // 从 pipeline 的反射信息获取实际的寄存器索引
  const auto& reflection = currentPipeline->getFragmentReflection();
  unsigned textureRegister = binding - TEXTURE_BINDING_POINT_START;

  // 纹理绑定到 pixel shader
  FRHIBatchedShaderParameters& params = rhiCmdList->GetScratchShaderParameters();
  params.SetShaderTexture(textureRegister, ueTexture->rhiTexture());
  params.SetShaderSampler(textureRegister, ueSampler->rhiSamplerState());
  rhiCmdList->SetBatchedShaderParameters(
      currentPipeline->getPixelShaderRHI(), params);
}
```

### E.4 HLSL 中的绑定布局

SPIRV-Cross 将 tgfx GLSL 的绑定点转换为 HLSL 寄存器：

**输入 GLSL：**
```glsl
layout(std140) uniform VertexUniformBlock {  // binding = 0
    vec4 tgfx_RTAdjust;
    mat3 uMatrix_S1_c0;
};

layout(std140) uniform FragmentUniformBlock {  // binding = 1
    vec4 uColor_S1_c0;
};

uniform sampler2D TextureSampler_0;  // binding = 2
uniform sampler2D TextureSampler_1;  // binding = 3
```

**SPIRV-Cross 输出 HLSL：**
```hlsl
cbuffer VertexUniformBlock : register(b0)  // binding 0 → register b0
{
    float4 tgfx_RTAdjust;
    float3x3 uMatrix_S1_c0;
};

cbuffer FragmentUniformBlock : register(b1)  // binding 1 → register b1
{
    float4 uColor_S1_c0;
};

Texture2D<float4> TextureSampler_0 : register(t0);  // binding 2 → register t0
SamplerState _TextureSampler_0_sampler : register(s0);  // 自动生成
Texture2D<float4> TextureSampler_1 : register(t1);  // binding 3 → register t1
SamplerState _TextureSampler_1_sampler : register(s1);
```

**映射规则（SPIRV-Cross 默认行为）：**

| tgfx binding | SPIR-V binding | HLSL register | 说明 |
|-------------|---------------|---------------|------|
| 0 (Vertex UBO) | 0 | `b0` | Constant Buffer |
| 1 (Fragment UBO) | 1 | `b1` | Constant Buffer |
| 2 (1st texture) | 2 | `t0` + `s0` | Texture + Sampler |
| 3 (2nd texture) | 3 | `t1` + `s1` | Texture + Sampler |
| N (Nth texture) | N | `t(N-2)` + `s(N-2)` | Texture + Sampler |

**重要：** SPIRV-Cross 可能会重新编号寄存器（从 0 开始），因此纹理的 HLSL 寄存器索引是 `binding - TEXTURE_BINDING_POINT_START`。

### E.5 Uniform Buffer 内存布局兼容性

tgfx 使用 **std140** 布局规则。HLSL 的 `cbuffer` 默认使用**打包规则**，与 std140 有差异：

| 类型 | std140 (GLSL) | HLSL 默认 | 差异 |
|------|-------------|-----------|------|
| `float3` | 16 字节对齐 | 12 字节 | **不兼容** |
| `mat3` | 3×16=48 字节 | 3×12=36 字节 | **不兼容** |
| `float` | 4 字节对齐 | 4 字节 | 兼容 |
| `float4` | 16 字节对齐 | 16 字节 | 兼容 |
| `mat4` | 4×16=64 字节 | 4×16=64 字节 | 兼容 |

**解决方案**：SPIRV-Cross 在转换时会自动处理布局差异，在 HLSL `cbuffer` 中为 `float3` 等类型添加 padding 以匹配 std140。但需要确认 SPIRV-Cross 选项：

```cpp
spirv_cross::CompilerHLSL::Options options;
options.shader_model = 50;
// 关键选项：保持 std140 兼容布局
options.flatten_matrix_vertex_input_semantics = true;
compiler.set_hlsl_options(options);
```

如果 SPIRV-Cross 不能完全保证布局一致，则需要在 tgfx 的 `UniformData` 中使用 HLSL 兼容的布局规则（tgfx 已经按 std140 对齐，float3 已经是 16 字节对齐，所以实际上是兼容的）。

### E.6 完整绑定流程图

```
┌──────────────────────────────────────────────────────────────────────────┐
│                    ProgramInfo::setUniformsAndSamplers()                 │
│                                                                          │
│  1. 分配 GPUBuffer 作为 UBO                                              │
│  2. 各 Processor::setData() 填充 UniformData → 写入 GPUBuffer           │
│  3. renderPass->setUniformBuffer(0, buffer, vOffset, vSize)             │
│     renderPass->setUniformBuffer(1, buffer, fOffset, fSize)             │
│  4. renderPass->setTexture(2, tex0, sampler0)                           │
│     renderPass->setTexture(3, tex1, sampler1)                           │
└─────────────────────────────────┬────────────────────────────────────────┘
                                  │
┌─────────────────────────────────▼────────────────────────────────────────┐
│                    UERHIRenderPass 实现                                   │
│                                                                          │
│  setUniformBuffer(0, ...) →                                              │
│    创建 FRHIUniformBuffer(数据, layout, SingleFrame)                      │
│    batchedParams.SetShaderUniformBuffer(0, uniformBuffer)                │
│    rhiCmdList->SetBatchedShaderParameters(vertexShader, params)          │
│                                                                          │
│  setUniformBuffer(1, ...) →                                              │
│    创建 FRHIUniformBuffer(数据, layout, SingleFrame)                      │
│    batchedParams.SetShaderUniformBuffer(0, uniformBuffer)  // PS 的 b0   │
│    rhiCmdList->SetBatchedShaderParameters(pixelShader, params)           │
│                                                                          │
│  setTexture(2, tex, sampler) →                                           │
│    textureIndex = 2 - TEXTURE_BINDING_POINT_START = 0                   │
│    batchedParams.SetShaderTexture(0, rhiTexture)      // t0             │
│    batchedParams.SetShaderSampler(0, rhiSampler)      // s0             │
│    rhiCmdList->SetBatchedShaderParameters(pixelShader, params)           │
└──────────────────────────────────────────────────────────────────────────┘
```

### E.7 关键注意事项

1. **FRHIUniformBuffer 生命周期**：使用 `UniformBuffer_SingleFrame` 标志创建，UE 会在帧结束时自动释放
2. **每帧 UBO 更新**：tgfx 每帧重新填充 UniformData 并绑定新的 UBO，不复用上一帧的数据
3. **SPIRV-Cross combined image sampler 分离**：HLSL 不支持 combined image sampler，SPIRV-Cross 会自动将 `sampler2D` 拆分为 `Texture2D` + `SamplerState`，需要在绑定时分别设置
4. **多次 SetBatchedShaderParameters 调用**：UE 允许对同一 shader 多次调用 SetBatchedShaderParameters，后设置的参数会覆盖先前设置的同索引参数
5. **Vertex Shader 纹理绑定**：tgfx 的纹理全部在 Fragment Shader 中使用，不需要考虑 Vertex Shader 纹理绑定

---

## 附录 F: [P0] 统一 Shader 编译与 PSO 创建方案

> **重要更新**：本附录描述的运行时 BuildUEShaderCode + RHICreateShader 方案已不再是推荐路径。
> 推荐使用 UE Global Shader 系统（附录 O），Shader 由 UE 编译管线自动预编译，PSO 通过
> `TShaderMapRef` + `SetGraphicsPipelineState` 创建。本附录保留作为备选方案参考。

### F.1 矛盾分析

正文第 9 章（UERHIRenderPipeline）设计使用 UE 标准 PSO 流程：
```
FGraphicsPipelineStateInitializer → RHICreateGraphicsPipelineState()
```
这要求 `BoundShaderState` 中包含有效的 `FRHIVertexShader*` / `FRHIPixelShader*`。

附录 D 提出"绕过 UE RHI Shader 接口"，这与上述要求矛盾。

### F.2 最终统一方案：构造最小化 UE Shader Code 格式

**核心发现**：UE RHI 的 `RHICreateVertexShader(Code, Hash)` 处理流程为：

```cpp
// D3D12Shaders.cpp
FVertexShaderRHIRef RHICreateVertexShader(Code, Hash) {
  InitStandardShader(new FD3D12VertexShader(), Code);
    → SerializeShaderResourceTable(Ar)  // 读取 SRT header
    → ReadShaderOptionalData()          // 读取可选数据
    → Code = ShaderCode.GetOffsetShaderCode(Offset)  // 提取原生字节码
}
```

**`FShaderResourceTable` 结构**（`RHIResources.h:754`）：
```cpp
struct FShaderResourceTable {
  uint32 ResourceTableBits = 0;               // 默认 0
  TArray<uint32> ShaderResourceViewMap;        // 默认空
  TArray<uint32> SamplerMap;                   // 默认空
  TArray<uint32> UnorderedAccessViewMap;       // 默认空
  TArray<uint32> ResourceTableLayoutHashes;    // 默认空
  TArray<uint32> TextureMap;                   // 默认空
  TArray<uint32> ResourceCollectionMap;        // 默认空
};
```

**一个全空的 FShaderResourceTable 序列化后仅需约 28 字节**（1 个 uint32 + 6 个空 TArray 的长度头）。因此可以在运行时构造一个合法的 UE shader code：

```
[空 FShaderResourceTable 序列化] + [空 optional data 标记] + [原生平台字节码]
```

这样就能通过标准的 `RHICreateVertexShader/RHICreatePixelShader` 创建有效的 shader 对象，不需要"绕过"。

### F.3 完整实现：UERHIShaderModule 统一版

```cpp
class UERHIShaderModule : public ShaderModule {
 public:
  static std::shared_ptr<UERHIShaderModule> Make(UERHIGPU* gpu,
                                                  const ShaderModuleDescriptor& descriptor) {
    auto module = std::shared_ptr<UERHIShaderModule>(
        new UERHIShaderModule(descriptor.stage, descriptor.code));
    if (!module->compile(gpu)) {
      return nullptr;
    }
    return module;
  }

  ShaderStage stage() const { return _stage; }
  FRHIVertexShader* rhiVertexShader() const { return _vertexShader.GetReference(); }
  FRHIPixelShader* rhiPixelShader() const { return _pixelShader.GetReference(); }

 private:
  bool compile(UERHIGPU* gpu) {
    // Step 1: GLSL → SPIR-V (shaderc, 已集成于 tgfx)
    _spirvBinary = CompileGLSLToSPIRV(_glslCode, _stage);
    if (_spirvBinary.empty()) return false;

    // Step 2: SPIR-V → 平台字节码
    TArray<uint8> platformBytecode;
    if (!CompileToPlatformBytecode(_spirvBinary, _stage, platformBytecode)) {
      return false;
    }

    // Step 3: 构造 UE Shader Code 格式
    TArray<uint8> ueShaderCode = BuildUEShaderCode(platformBytecode);

    // Step 4: 计算 hash
    FSHAHash hash;
    FSHA1::HashBuffer(ueShaderCode.GetData(), ueShaderCode.Num(), hash.Hash);

    // Step 5: 通过标准 UE RHI 接口创建 shader
    if (_stage == ShaderStage::Vertex) {
      _vertexShader = GDynamicRHI->RHICreateVertexShader(ueShaderCode, hash);
      return _vertexShader.IsValid();
    } else {
      _pixelShader = GDynamicRHI->RHICreatePixelShader(ueShaderCode, hash);
      return _pixelShader.IsValid();
    }
  }

  // 构造 UE 兼容的 shader code 格式
  static TArray<uint8> BuildUEShaderCode(const TArray<uint8>& platformBytecode) {
    TArray<uint8> result;
    FMemoryWriter ar(result);

    // 写入空的 FShaderResourceTable
    FShaderResourceTable emptySRT;
    ar << emptySRT;

    // 写入 optional data 终止标记
    // UE 的 FShaderCodeReader::ReadShaderOptionalData 从 Code 末尾反向查找
    // 通过在字节码前不放置任何 optional data key 来跳过
    // (ReadShaderOptionalData 会发现没有有效的 key 而直接返回)

    // 写入原生字节码
    result.Append(platformBytecode);

    return result;
  }

  // 平台分支编译
  static bool CompileToPlatformBytecode(const std::vector<uint32_t>& spirv,
                                         ShaderStage stage,
                                         TArray<uint8>& outBytecode) {
    ERHIInterfaceType rhiType = GDynamicRHI->GetInterfaceType();

    switch (rhiType) {
      case ERHIInterfaceType::D3D12: {
        // SPIR-V → HLSL → DXBC
        std::string hlsl = ConvertSPIRVToHLSL(spirv, stage);
        return CompileHLSLToDXBC(hlsl, stage, outBytecode);
      }
      case ERHIInterfaceType::Vulkan: {
        // 直接使用 SPIR-V
        outBytecode.SetNum(spirv.size() * sizeof(uint32_t));
        FMemory::Memcpy(outBytecode.GetData(), spirv.data(), outBytecode.Num());
        return true;
      }
      case ERHIInterfaceType::Metal: {
        // SPIR-V → MSL → metallib
        std::string msl = ConvertSPIRVToMSL(spirv, stage);
        return CompileMSLToMetallib(msl, stage, outBytecode);
      }
      default:
        return false;
    }
  }

  // SPIR-V → HLSL 转换
  static std::string ConvertSPIRVToHLSL(const std::vector<uint32_t>& spirv,
                                          ShaderStage stage) {
    spirv_cross::CompilerHLSL compiler(spirv);
    spirv_cross::CompilerHLSL::Options options;
    options.shader_model = 50;  // SM 5.0
    compiler.set_hlsl_options(options);
    return compiler.compile();
  }

  // HLSL → DXBC 运行时编译
  static bool CompileHLSLToDXBC(const std::string& hlsl, ShaderStage stage,
                                 TArray<uint8>& outBytecode) {
    // 动态加载 d3dcompiler_47.dll
    static HMODULE hCompiler = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!hCompiler) return false;

    typedef HRESULT(WINAPI* pD3DCompile_t)(
        LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
        LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    static auto pD3DCompile =
        (pD3DCompile_t)GetProcAddress(hCompiler, "D3DCompile");
    if (!pD3DCompile) return false;

    const char* target = (stage == ShaderStage::Vertex) ? "vs_5_0" : "ps_5_0";
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = pD3DCompile(hlsl.c_str(), hlsl.size(), nullptr, nullptr, nullptr,
                             "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                             &shaderBlob, &errorBlob);
    if (errorBlob) errorBlob->Release();
    if (FAILED(hr)) return false;

    outBytecode.SetNum(shaderBlob->GetBufferSize());
    FMemory::Memcpy(outBytecode.GetData(), shaderBlob->GetBufferPointer(),
                    shaderBlob->GetBufferSize());
    shaderBlob->Release();
    return true;
  }

  ShaderStage _stage;
  std::string _glslCode;
  std::vector<uint32_t> _spirvBinary;
  FVertexShaderRHIRef _vertexShader;
  FPixelShaderRHIRef _pixelShader;
};
```

### F.4 统一后的 UERHIRenderPipeline 创建流程

**正文第 9 章现在完全兼容**：

```cpp
bool UERHIRenderPipeline::initialize(const RenderPipelineDescriptor& descriptor) {
  // 1. 获取 UE RHI Shader 对象 (通过 UERHIShaderModule::rhiVertexShader/rhiPixelShader)
  auto vs = std::static_pointer_cast<UERHIShaderModule>(descriptor.vertex.module);
  auto ps = std::static_pointer_cast<UERHIShaderModule>(descriptor.fragment.module);

  // 2. 创建顶点声明
  FVertexDeclarationElementList elements;
  // ... 从 descriptor.vertex.bufferLayouts 转换 ...
  vertexDeclaration = RHICreateVertexDeclaration(elements);

  // 3. 创建状态对象
  FRHIBlendState* blendState = CreateBlendState(descriptor.fragment.colorAttachments);
  FRHIRasterizerState* rasterizerState = CreateRasterizerState(descriptor.primitive);
  FRHIDepthStencilState* depthStencilState = CreateDepthStencilState(descriptor.depthStencil);

  // 4. 填充 PSO 初始化器 —— 直接使用标准 UE RHI shader 对象
  psoInitializer.BoundShaderState.VertexDeclarationRHI = vertexDeclaration;
  psoInitializer.BoundShaderState.VertexShaderRHI = vs->rhiVertexShader();  // 有效的 FRHIVertexShader*
  psoInitializer.BoundShaderState.PixelShaderRHI = ps->rhiPixelShader();    // 有效的 FRHIPixelShader*
  psoInitializer.BlendState = blendState;
  psoInitializer.RasterizerState = rasterizerState;
  psoInitializer.DepthStencilState = depthStencilState;
  psoInitializer.PrimitiveType = ConvertPrimitiveType(descriptor);
  // ... 填充渲染目标格式 ...

  // 5. 标准 UE PSO 创建 —— 无矛盾
  SetGraphicsPipelineState(*_gpu->currentRHICommandList(), psoInitializer, 0,
                           EApplyRendertargetOption::DoNothing);

  return true;
}
```

### F.5 FShaderCodeReader::ReadShaderOptionalData 兼容性

UE 的 optional data 是从 Code 数组**末尾反向读取**的。格式为：

```
[... native bytecode ...] [optional data value] [optional data size (uint32)] [optional data key (uint8)]
```

反向扫描时，如果最后一个字节不是已知的 key（`FShaderCodeLibraryKey` 等），则认为没有 optional data，直接将 SRT 之后的所有数据作为原生字节码。

因此 `BuildUEShaderCode` 只要确保原生字节码的最后一个字节不恰好等于某个 optional data key 即可。实践中 DXBC/SPIR-V/metallib 的尾部字节几乎不可能冲突。如需确保，可在字节码末尾追加一个不是任何 key 的标记字节。

### F.6 必须包含 FShaderCodePackedResourceCounts

D3D12RHI 的 `ReadShaderOptionalData` 中强制要求存在此结构：

```cpp
// D3D12Shaders.cpp:30-35
const FShaderCodePackedResourceCounts* PackedResourceCounts =
    InShaderCode.FindOptionalData<FShaderCodePackedResourceCounts>();
if (!PackedResourceCounts) {
  return false;  // shader 创建失败
}
```

因此 `BuildUEShaderCode` 必须在字节码末尾按 UE optional data 格式追加此结构：

```cpp
static TArray<uint8> BuildUEShaderCode(const TArray<uint8>& platformBytecode) {
  TArray<uint8> result;
  FMemoryWriter ar(result);

  // 1. 空 FShaderResourceTable
  FShaderResourceTable emptySRT;
  ar << emptySRT;

  // 2. 原生字节码 (SRT 之后，optional data 之前)
  int32 bytecodeOffset = result.Num();
  result.Append(platformBytecode);

  // 3. 在末尾追加 FShaderCodePackedResourceCounts 作为 optional data
  // UE optional data 格式：[value bytes][size as uint32][key as uint8]
  FShaderCodePackedResourceCounts counts = {};
  // 根据 shader 反射信息填充：
  // counts.NumSamplers = numTextures;
  // counts.NumSRVs = numTextures;
  // counts.NumCBs = numUBOs;

  const uint8* countsData = reinterpret_cast<const uint8*>(&counts);
  uint32 countsSize = sizeof(FShaderCodePackedResourceCounts);
  uint8 countsKey = FShaderCodePackedResourceCounts::Key;  // UE 定义的 key 常量

  result.Append(countsData, countsSize);  // value
  result.Append(reinterpret_cast<const uint8*>(&countsSize), sizeof(uint32));  // size
  result.Add(countsKey);  // key

  return result;
}
```

### F.7 Vulkan 平台的 Code 格式差异

Vulkan RHI 有独立的 header 格式，Code 结构为：

```
[FVulkanShaderHeader] + [FShaderResourceTable] + [SPIR-V size (int32)] + [SPIR-V bytecode]
```

需要构造 `FVulkanShaderHeader`，其中关键字段包括 UBO/sampler/texture 的 binding 信息。这比 D3D12 复杂，但同样可以从 SPIRV-Cross 反射信息中填充。

Metal 的 Code 格式为：

```
[uint8 OfflineCompiledFlag] + [FMetalCodeHeader with SRT] + [metallib/MSL source]
```

### F.8 平台特定 BuildUEShaderCode 总结

| 平台 | Code 格式 | 复杂度 |
|------|----------|--------|
| D3D12 | `[空SRT] + [DXBC] + [PackedResourceCounts optional data]` | 低 |
| Vulkan | `[FVulkanShaderHeader] + [SRT] + [SPIR-V size] + [SPIR-V]` | 中 |
| Metal | `[OfflineFlag] + [FMetalCodeHeader] + [metallib]` | 中 |

**建议实施顺序**：先完成 D3D12（最简单），验证方案可行后再扩展到 Vulkan 和 Metal。

### F.9 方案验证要点

| 验证项 | 验证方法 | 风险等级 |
|-------|---------|---------|
| 空 SRT + PackedResourceCounts 创建 shader 成功 | 用最简单的 pass-through shader 测试 | **必须首先验证** |
| SPIRV-Cross HLSL 输出通过 D3DCompile | 用典型 tgfx shader 测试 | 中 |
| PSO 创建成功并能正确绘制 | 渲染一个简单矩形 | 中 |
| Vulkan FVulkanShaderHeader 构造正确性 | 对比 UE cook 产生的 Vulkan shader | 中 |
| D3DCompile 运行时 DLL 可用 | 确认 UE 分发包含 d3dcompiler_47.dll | 低 (已确认) |

### F.10 关键结论

1. **不需要"绕过"UE RHI**：通过构造最小化 UE shader code 格式（含必需的 `FShaderCodePackedResourceCounts`），可以正常使用 `RHICreateVertexShader/RHICreatePixelShader`
2. **正文第 9 章无需修改**：`UERHIRenderPipeline` 使用标准 `FGraphicsPipelineStateInitializer` + 标准 PSO 创建流程
3. **附录 D 的编译管线仍然有效**：各平台的 GLSL → 平台字节码编译路径不变，只是最终产物被包装为 UE 格式
4. **矛盾解除**：ShaderModule 产出标准 UE RHI shader 对象 → Pipeline 使用标准 UE PSO 流程 → RenderPass 使用标准 `RHISetGraphicsPipelineState`
5. **D3D12 优先**：D3D12 的 code 格式最简单，建议作为第一个验证平台

---

## 附录 G: BuildUEShaderCode 精确二进制格式

### G.1 UE Shader Code 完整二进制布局

经过对 `FShaderCodeReader`（`ShaderCore.h:982-1127`）的逐行分析，UE shader code 的精确格式为：

```
┌──────────────────────────────────────────────────────────────┐
│               FShaderResourceTable 序列化数据                  │
│  (由 FArchive << SRT 写入)                                    │
│  空 SRT ≈ 28 bytes: uint32(0) + 6个空TArray(各4bytes头)       │
├──────────────────────────────────────────────────────────────┤
│               原生平台字节码 (DXBC/DXIL/SPIR-V)               │
│  从 SRT 结束位置到 ActualShaderCodeSize 结束位置              │
│  ActualShaderCodeSize = Code.Num() - OptionalDataSize        │
├──────────────────────────────────────────────────────────────┤
│               Optional Data 区域                              │
│  格式: [key(uint8)][size(uint32)][value(size bytes)] 重复    │
│  正向存储，正向扫描                                           │
├──────────────────────────────────────────────────────────────┤
│               OptionalDataSize (int32, 4 bytes)              │
│  = optional data 区域的大小 + 4 (包括自身)                    │
│  存储在 Code 数组的最后 4 字节                                 │
│  如果没有 optional data，此值 = 4 (仅包括自身)                │
└──────────────────────────────────────────────────────────────┘
```

### G.2 FShaderCodePackedResourceCounts 精确定义

```cpp
// ShaderCore.h:758-782

enum class EShaderResourceUsageFlags : uint8 {
  None                = 0,
  GlobalUniformBuffer = 1 << 0,  // 使用全局 uniform buffer
  BindlessResources   = 1 << 1,
  BindlessSamplers    = 1 << 2,
  RootConstants       = 1 << 3,
  NoDerivativeOps     = 1 << 4,  // shader 不使用 ddx/ddy
  ShaderBundle        = 1 << 5,
  DiagnosticBuffer    = 1 << 6,
};

struct FShaderCodePackedResourceCounts {
  static const EShaderOptionalDataKey Key = EShaderOptionalDataKey::PackedResourceCounts;
  // Key 的 uint8 值 = 'p' (0x70)

  EShaderResourceUsageFlags UsageFlags;  // 1 byte
  uint8 NumSamplers;                     // 采样器数量
  uint8 NumSRVs;                         // Shader Resource View 数量
  uint8 NumCBs;                          // Constant Buffer 数量
  uint8 NumUAVs;                         // Unordered Access View 数量
};
// sizeof(FShaderCodePackedResourceCounts) = 5 bytes
```

### G.3 BuildUEShaderCode 精确实现（D3D12）

```cpp
static TArray<uint8> BuildUEShaderCode(
    const TArray<uint8>& platformBytecode,
    uint8 numSamplers,  // 纹理数量（从 SPIRV-Cross 反射获取）
    uint8 numCBs        // Constant Buffer 数量（tgfx 固定为 1-2 个 UBO）
) {
  TArray<uint8> code;

  // ===== Part 1: 序列化空的 FShaderResourceTable =====
  {
    FMemoryWriter ar(code);
    FShaderResourceTable emptySRT;
    ar << emptySRT;  // 约 28 bytes
  }

  // ===== Part 2: 原生字节码 =====
  code.Append(platformBytecode);

  // ===== Part 3: Optional Data =====
  // 构造 FShaderCodePackedResourceCounts
  FShaderCodePackedResourceCounts counts;
  counts.UsageFlags = EShaderResourceUsageFlags::None;
  counts.NumSamplers = numSamplers;    // 例如: 2 (两个纹理)
  counts.NumSRVs = numSamplers;        // SRV 数量 = 纹理数量
  counts.NumCBs = numCBs;              // 例如: 1 (vertex UBO) 或 1 (fragment UBO)
  counts.NumUAVs = 0;

  // 写入 optional data entry: [key][size][value]
  uint8 key = static_cast<uint8>(EShaderOptionalDataKey::PackedResourceCounts); // = 'p' = 0x70
  uint32 valueSize = sizeof(FShaderCodePackedResourceCounts);  // = 5

  int32 optionalDataSizeAccum = 0;

  code.Add(key);                                                    // 1 byte: key
  code.Append(reinterpret_cast<const uint8*>(&valueSize), sizeof(uint32));  // 4 bytes: size
  code.Append(reinterpret_cast<const uint8*>(&counts), valueSize);  // 5 bytes: value
  optionalDataSizeAccum += 1 + 4 + valueSize;  // = 10

  // ===== Part 4: OptionalDataSize (最后 4 字节) =====
  int32 totalOptionalDataSize = optionalDataSizeAccum + sizeof(int32);  // = 14
  code.Append(reinterpret_cast<const uint8*>(&totalOptionalDataSize), sizeof(int32));

  return code;
}

// 验证: Code 布局为
// [SRT ~28B] [DXBC bytes] [key=0x70][size=5][counts 5B] [optDataSize=14]
// 
// GetOptionalDataSize() 读取最后 4 字节 = 14
// GetActualShaderCodeSize() = Code.Num() - 14
// GetOffsetShaderCode(SRT_offset) 提取 SRT 后到 ActualShaderCodeSize 的数据 = DXBC 字节码
// FindOptionalData<FShaderCodePackedResourceCounts>() 在 optional data 区域找到 key='p'
```

### G.4 tgfx Shader 对应的 ResourceCounts 填充规则

| tgfx 资源 | UE ResourceCounts 字段 | 值 |
|----------|----------------------|---|
| VertexUniformBlock (binding=0) | `NumCBs` (vertex shader) | 1 |
| FragmentUniformBlock (binding=1) | `NumCBs` (pixel shader) | 1 |
| 纹理 sampler (binding=2+) | `NumSamplers` + `NumSRVs` | 纹理数量 |
| 无 UAV | `NumUAVs` | 0 |
| UsageFlags | `UsageFlags` | `None` (tgfx 不使用全局 UB 等) |

**注意**：vertex shader 和 pixel shader 各自有独立的 `FShaderCodePackedResourceCounts`，需要分别计算。

---

## 附录 H: RHI 资源状态转换

### H.1 概述

在 D3D12/Vulkan 后端，纹理需要在不同用途之间显式转换状态（Resource Barrier / Layout Transition）。UE RHI 通过 `FRHITransitionInfo` 和 `RHITransition` 接口封装此机制。

### H.2 转换时机

```
[渲染前] 纹理从 SRV/Unknown 状态 → RenderTarget 状态
  → RHIBeginRenderPass()
  → 绘制命令...
  → RHIEndRenderPass()
[渲染后] 纹理从 RenderTarget 状态 → SRV 状态（如果后续需要采样）
```

### H.3 UERHIRenderPass 中的资源转换实现

```cpp
bool UERHIRenderPass::beginRenderPass() {
  // === 资源状态转换 (RenderPass 开始前) ===
  for (const auto& ca : descriptor.colorAttachments) {
    if (!ca.texture) continue;
    auto ueTexture = std::static_pointer_cast<UERHITexture>(ca.texture);

    // 如果纹理之前作为 SRV 使用，需要转换到 RenderTarget 状态
    rhiCmdList->Transition(FRHITransitionInfo(
        ueTexture->rhiTexture(),
        ERHIAccess::Unknown,       // 之前的状态（Unknown 让 RHI 自动推断）
        ERHIAccess::RTV             // 目标状态：Render Target View
    ));
  }

  if (descriptor.depthStencilAttachment.texture) {
    auto ueDS = std::static_pointer_cast<UERHITexture>(
        descriptor.depthStencilAttachment.texture);
    rhiCmdList->Transition(FRHITransitionInfo(
        ueDS->rhiTexture(),
        ERHIAccess::Unknown,
        ERHIAccess::DSVWrite        // 深度模板写入状态
    ));
  }

  // === 构建 FRHIRenderPassInfo ===
  FRHIRenderPassInfo rhiPassInfo;
  // ... (已有代码，见正文 8.3 节) ...

  rhiCmdList->BeginRenderPass(rhiPassInfo, TEXT("TGFXRenderPass"));
  return true;
}

void UERHIRenderPass::onEnd() {
  rhiCmdList->EndRenderPass();

  // === 资源状态转换 (RenderPass 结束后) ===
  // 将颜色附件转回 SRV 状态（供后续 pass 采样）
  for (const auto& ca : descriptor.colorAttachments) {
    if (!ca.texture) continue;
    auto ueTexture = std::static_pointer_cast<UERHITexture>(ca.texture);
    rhiCmdList->Transition(FRHITransitionInfo(
        ueTexture->rhiTexture(),
        ERHIAccess::RTV,
        ERHIAccess::SRVMask         // 可被着色器采样
    ));
  }
}
```

### H.4 ERHIAccess 常用值

| ERHIAccess 值 | 含义 | tgfx 对应场景 |
|--------------|------|-------------|
| `Unknown` | 自动推断（方便但不如显式高效） | 初始状态 |
| `SRVMask` | 着色器资源视图（可采样） | 纹理作为输入 |
| `RTV` | 渲染目标写入 | 纹理作为颜色附件 |
| `DSVWrite` | 深度模板写入 | 深度模板附件 |
| `DSVRead` | 深度模板只读 | 深度测试但不写入 |
| `CopySrc` | 拷贝源 | copyTextureToTexture 源 |
| `CopyDest` | 拷贝目标 | copyTextureToTexture 目标 |

---

## 附录 I: Mipmap 生成方案

### I.1 UE 中的 Mipmap 生成

UE 提供 `FGenerateMips` 工具类，通过 compute shader 生成 mipmap。

### I.2 UERHICommandEncoder::generateMipmapsForTexture 实现

```cpp
void UERHICommandEncoder::generateMipmapsForTexture(std::shared_ptr<Texture> texture) {
  auto ueTexture = std::static_pointer_cast<UERHITexture>(texture);
  FRHITexture* rhiTexture = ueTexture->rhiTexture();
  FRHICommandListImmediate* cmdList = _gpu->currentRHICommandList();

  // 检查纹理是否支持 mipmap 生成
  if (rhiTexture->GetNumMips() <= 1) {
    return;
  }

  // 使用 UE 的 FGenerateMips 工具
  // 需要 RenderGraph (FRDGBuilder) 环境
  FRDGBuilder graphBuilder(*cmdList);
  FRDGTextureRef rdgTexture = graphBuilder.RegisterExternalTexture(
      CreateRenderTarget(rhiTexture, TEXT("TGFXMipGenTarget")));
  FGenerateMips::Execute(&graphBuilder, rdgTexture, FSamplerStateInitializerRHI());
  graphBuilder.Execute();

  hasCommands = true;
}
```

**备选方案**（如果 FRDGBuilder 不方便使用）：手动逐级 blit

```cpp
void UERHICommandEncoder::generateMipmapsForTexture(std::shared_ptr<Texture> texture) {
  auto ueTexture = std::static_pointer_cast<UERHITexture>(texture);
  FRHITexture* rhiTexture = ueTexture->rhiTexture();
  int numMips = rhiTexture->GetNumMips();
  int width = rhiTexture->GetDesc().Extent.X;
  int height = rhiTexture->GetDesc().Extent.Y;

  // 逐级从 mip N-1 复制到 mip N（使用 CopyTexture 或自定义 downsample shader）
  for (int mip = 1; mip < numMips; ++mip) {
    int srcWidth = FMath::Max(1, width >> (mip - 1));
    int srcHeight = FMath::Max(1, height >> (mip - 1));
    int dstWidth = FMath::Max(1, width >> mip);
    int dstHeight = FMath::Max(1, height >> mip);

    FRHICopyTextureInfo copyInfo;
    copyInfo.Size = FIntVector(dstWidth, dstHeight, 1);
    copyInfo.SourceMipIndex = mip - 1;
    copyInfo.DestMipIndex = mip;
    _gpu->currentRHICommandList()->CopyTexture(rhiTexture, rhiTexture, copyInfo);
  }
  hasCommands = true;
}
```

---

## 附录 J: 缺失的枚举映射补全

### J.1 VertexFormat 完整映射

| tgfx VertexFormat | UE EVertexElementType | 说明 |
|------------------|-----------------------|------|
| `Float` | `VET_Float1` | 32-bit float |
| `Float2` | `VET_Float2` | 2x 32-bit float |
| `Float3` | `VET_Float3` | 3x 32-bit float |
| `Float4` | `VET_Float4` | 4x 32-bit float |
| `Half` | `VET_Half2` (padding) | UE 无 VET_Half1，用 Half2 代替 |
| `Half2` | `VET_Half2` | 2x 16-bit float |
| `Half3` | `VET_Half4` (padding) | UE 无 VET_Half3，用 Half4 代替 |
| `Half4` | `VET_Half4` | 4x 16-bit float |
| `Int` | `VET_UInt` | 32-bit uint (UE 无 signed int) |
| `Int2` | 不支持 | UE 无 VET_Int2，需拆分或用 Float2 |
| `Int3` | 不支持 | UE 无 VET_Int3 |
| `Int4` | 不支持 | UE 无 VET_Int4 |
| `UByteNormalized` | `VET_UByte4N` (padding) | UE 无单字节，用 4 字节代替 |
| `UByte2Normalized` | `VET_UByte4N` (padding) | 用 4 字节代替 |
| `UByte3Normalized` | `VET_UByte4N` (padding) | 用 4 字节代替 |
| `UByte4Normalized` | `VET_UByte4N` | 4x uint8 归一化 |

**注意**：对于 UE 不直接支持的格式（Half、UByte1-3、Int2-4），需要在 shader 中做适当的 padding 或类型转换。实践中 tgfx 主要使用 Float2/Float3/Float4 和 UByte4Normalized，其他格式很少出现。

```cpp
EVertexElementType ConvertVertexFormat(VertexFormat format) {
  switch (format) {
    case VertexFormat::Float:            return VET_Float1;
    case VertexFormat::Float2:           return VET_Float2;
    case VertexFormat::Float3:           return VET_Float3;
    case VertexFormat::Float4:           return VET_Float4;
    case VertexFormat::Half:             return VET_Half2;  // padding
    case VertexFormat::Half2:            return VET_Half2;
    case VertexFormat::Half3:            return VET_Half4;  // padding
    case VertexFormat::Half4:            return VET_Half4;
    case VertexFormat::Int:              return VET_UInt;
    case VertexFormat::Int2:             return VET_Float2;  // reinterpret
    case VertexFormat::Int3:             return VET_Float3;  // reinterpret
    case VertexFormat::Int4:             return VET_Float4;  // reinterpret
    case VertexFormat::UByteNormalized:  return VET_UByte4N; // padding
    case VertexFormat::UByte2Normalized: return VET_UByte4N; // padding
    case VertexFormat::UByte3Normalized: return VET_UByte4N; // padding
    case VertexFormat::UByte4Normalized: return VET_UByte4N;
    default:                             return VET_Float4;
  }
}
```

---

## 附录 K: UPAGWidget RebuildWidget 实现

### K.1 完整 Slate Widget 构建

```cpp
TSharedRef<SWidget> UPAGWidget::RebuildWidget() {
  // 创建 Slate Image Widget
  slateImage = SNew(SImage);

  // 创建 FSlateTexture2DRHIRef（在 Render Thread 上初始化）
  int32 width = 256;   // 初始尺寸，后续根据 PAG 文件调整
  int32 height = 256;

  slateTexture = MakeShared<FSlateTexture2DRHIRef>(
      width, height, PF_B8G8R8A8, nullptr, TexCreate_None, true);

  // 注册为 render resource
  BeginInitResource(slateTexture.Get());

  // 创建动态 brush 引用 slate texture
  slateBrush = MakeShareable(new FSlateBrush());
  slateBrush->SetResourceObject(slateTexture.Get());
  slateBrush->ImageSize = FVector2D(width, height);
  slateBrush->DrawAs = ESlateBrushDrawType::Image;

  slateImage->SetImage(slateBrush.Get());

  return slateImage.ToSharedRef();
}

void UPAGWidget::ReleaseSlateResources(bool bReleaseChildren) {
  Super::ReleaseSlateResources(bReleaseChildren);
  slateImage.Reset();

  if (slateTexture.IsValid()) {
    BeginReleaseResource(slateTexture.Get());
    // 等待 render thread 释放完成
    FlushRenderingCommands();
    slateTexture.Reset();
  }
  slateBrush.Reset();
}

void UPAGWidget::SynchronizeProperties() {
  Super::SynchronizeProperties();

  if (slateImage.IsValid() && slateBrush.IsValid()) {
    slateImage->SetImage(slateBrush.Get());
  }
}
```

### K.2 Flush 实现（连接 PAG 渲染和 Slate 显示）

```cpp
void UPAGWidget::Flush() {
  if (!pagPlayer || !drawable) return;

  // 捕获需要的变量
  auto drawablePtr = drawable;
  auto playerPtr = pagPlayer;
  auto texturePtr = slateTexture;

  ENQUEUE_RENDER_COMMAND(PAGFlush)(
      [drawablePtr, playerPtr, texturePtr](FRHICommandListImmediate& RHICmdList) {
    // 1. 锁定 tgfx Device
    auto device = drawablePtr->getDevice();
    auto* context = device->lockContext();
    if (!context) return;

    // 2. 执行 PAG 渲染
    playerPtr->flush();

    // 3. 获取渲染结果纹理
    FRHITexture* rhiTexture = drawablePtr->getRHITexture();

    // 4. 解锁
    device->unlock();

    // 5. 更新 Slate 纹理
    if (rhiTexture && texturePtr.IsValid()) {
      FTextureRHIRef textureRef = rhiTexture;
      texturePtr->SetRHIRef(textureRef,
                            rhiTexture->GetDesc().Extent.X,
                            rhiTexture->GetDesc().Extent.Y);
    }
  });
}
```

---

## 附录 L: UBO 数据流修正与 setUniformBuffer 最终方案

### L.1 问题分析

附录 E 中 `setUniformBuffer` 的实现假设可直接访问 `ueBuffer->mappedData()`，但 tgfx 的实际 UBO 数据流为：

```
1. ProgramInfo::getUniformBuffer()
   → GlobalCache 获取/创建 GPUBuffer (UNIFORM usage)
   → buffer->map(offset, totalSize)      ← 返回 mapped 指针
   → UniformData::setBuffer(mappedPtr)   ← 各 Processor 通过此指针写入数据

2. ProgramInfo::bindUniformBufferAndUnloadToGPU()
   → uniformBuffer->unmap()              ← 数据已提交到 GPU
   → renderPass->setUniformBuffer(0, buffer, vertexOffset, vertexSize)
   → renderPass->setUniformBuffer(1, buffer, fragmentOffset, fragmentSize)
```

**关键约束**：`setUniformBuffer` 被调用时 buffer 已经 unmap，CPU 数据不可直接访问。Metal 后端直接将 `MTLBuffer` 以 offset 绑定（Metal 支持直接绑定 buffer 到 shader）。但 UE RHI 的 `SetShaderUniformBuffer` 只接受 `FRHIUniformBuffer*`，创建 `FRHIUniformBuffer` 需要 `const void* Contents`（CPU 数据指针）。

### L.2 最终方案：UERHIBuffer 保留 CPU 暂存区

**设计**：`UERHIBuffer` 在 UNIFORM usage 时维护 CPU 侧暂存拷贝。`map()` 返回暂存区指针，Processor 写入数据；`unmap()` 将数据上传到 GPU buffer 但**保留暂存区**；`setUniformBuffer` 从暂存区创建 `FRHIUniformBuffer`。

```cpp
// src/gpu/ue/UERHIBuffer.h (修正版)

class UERHIBuffer : public GPUBuffer {
 public:
  static std::shared_ptr<UERHIBuffer> Make(UERHIGPU* gpu, size_t size, uint32_t usage);
  ~UERHIBuffer() override;

  FRHIBuffer* rhiBuffer() const { return _buffer.GetReference(); }

  /**
   * CPU 暂存区指针（仅 UNIFORM buffer 有效）。
   * map → Processor 写入 → unmap 后，暂存区仍保留最新数据。
   */
  const uint8* stagingData() const { return _stagingBuffer.GetData(); }

  bool isReady() const override;
  void* map(size_t offset = 0, size_t size = GPU_BUFFER_WHOLE_SIZE) override;
  void unmap() override;

 private:
  UERHIBuffer(size_t size, uint32_t usage, FBufferRHIRef buffer);

  FBufferRHIRef _buffer;
  TArray<uint8> _stagingBuffer;   // CPU 暂存（UNIFORM buffer 专用）
  size_t _mapOffset = 0;
  size_t _mapSize = 0;
  bool _isMapped = false;
};
```

### L.3 UERHIBuffer 实现

```cpp
std::shared_ptr<UERHIBuffer> UERHIBuffer::Make(UERHIGPU* gpu, size_t size, uint32_t usage) {
  EBufferUsageFlags ueUsage = EBufferUsageFlags::None;
  if (usage & GPUBufferUsage::VERTEX)  ueUsage |= EBufferUsageFlags::VertexBuffer;
  if (usage & GPUBufferUsage::INDEX)   ueUsage |= EBufferUsageFlags::IndexBuffer;
  if (usage & GPUBufferUsage::UNIFORM) ueUsage |= EBufferUsageFlags::UniformBuffer | EBufferUsageFlags::Volatile;
  if (usage & GPUBufferUsage::READBACK) ueUsage |= EBufferUsageFlags::KeepCPUAccessible;

  FRHICommandListImmediate* cmdList = gpu->currentRHICommandList();
  const FRHIBufferCreateDesc desc =
      FRHIBufferCreateDesc::Create(TEXT("TGFXBuffer"), ueUsage).SetSize(size).SetStride(0);
  FBufferRHIRef rhiBuffer = cmdList->CreateBuffer(desc);
  if (!rhiBuffer.IsValid()) return nullptr;

  auto buffer = std::shared_ptr<UERHIBuffer>(new UERHIBuffer(size, usage, rhiBuffer));
  if (usage & GPUBufferUsage::UNIFORM) {
    buffer->_stagingBuffer.SetNumZeroed(size);
  }
  return buffer;
}

void* UERHIBuffer::map(size_t offset, size_t mapSize) {
  if (_isMapped) return nullptr;
  size_t actualSize = (mapSize == GPU_BUFFER_WHOLE_SIZE) ? (_size - offset) : mapSize;

  if (_usage & GPUBufferUsage::UNIFORM) {
    // 返回暂存区指针，Processor 直接写入
    _mapOffset = offset;
    _mapSize = actualSize;
    _isMapped = true;
    return _stagingBuffer.GetData() + offset;
  }

  // 非 UNIFORM: 使用 RHI lock
  void* ptr = RHILockBuffer(_buffer.GetReference(), offset, actualSize, RLM_WriteOnly);
  if (ptr) { _mapOffset = offset; _mapSize = actualSize; _isMapped = true; }
  return ptr;
}

void UERHIBuffer::unmap() {
  if (!_isMapped) return;

  if (_usage & GPUBufferUsage::UNIFORM) {
    // 暂存区 → GPU buffer（不清空暂存区）
    void* dest = RHILockBuffer(_buffer.GetReference(), _mapOffset, _mapSize, RLM_WriteOnly);
    if (dest) {
      FMemory::Memcpy(dest, _stagingBuffer.GetData() + _mapOffset, _mapSize);
      RHIUnlockBuffer(_buffer.GetReference());
    }
  } else {
    RHIUnlockBuffer(_buffer.GetReference());
  }
  _isMapped = false;
}
```

### L.4 setUniformBuffer 最终实现（替代附录 E.2）

```cpp
void UERHIRenderPass::setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer,
                                       size_t offset, size_t size) {
  if (!currentPipeline || !buffer) return;

  // 状态缓存
  if (binding < MaxUniformBindings &&
      lastUniformBuffers[binding] == buffer.get() &&
      lastUniformOffsets[binding] == offset) {
    return;
  }
  if (binding < MaxUniformBindings) {
    lastUniformBuffers[binding] = buffer.get();
    lastUniformOffsets[binding] = offset;
  }

  auto ueBuffer = std::static_pointer_cast<UERHIBuffer>(buffer);

  // 从 CPU 暂存区创建 FRHIUniformBuffer (SingleFrame 帧内分配器，开销极低)
  FRHIUniformBufferLayoutInitializer layoutInit;
  layoutInit.ConstantBufferSize = size;
  static TMap<uint32, FUniformBufferLayoutRHIRef> layoutCache;
  FUniformBufferLayoutRHIRef& layout = layoutCache.FindOrAdd((uint32)size);
  if (!layout.IsValid()) {
    layout = RHICreateUniformBufferLayout(layoutInit);
  }

  const uint8* cpuData = ueBuffer->stagingData() + offset;
  FUniformBufferRHIRef uniformBuffer = RHICreateUniformBuffer(
      cpuData, layout, UniformBuffer_SingleFrame,
      EUniformBufferValidation::SkipValidation);

  // 绑定到对应 shader stage
  FRHIBatchedShaderParameters& params = rhiCmdList->GetScratchShaderParameters();
  params.SetShaderUniformBuffer(0, uniformBuffer);  // 绑定到 UB slot 0

  if (binding == VERTEX_UBO_BINDING_POINT) {
    rhiCmdList->SetBatchedShaderParameters(currentPipeline->getVertexShaderRHI(), params);
  } else if (binding == FRAGMENT_UBO_BINDING_POINT) {
    rhiCmdList->SetBatchedShaderParameters(currentPipeline->getPixelShaderRHI(), params);
  }
}
```

### L.5 性能说明

- `RHICreateUniformBuffer` + `UniformBuffer_SingleFrame` 使用帧内环形分配器，创建开销 < 1us
- UNIFORM buffer 通常 < 1KB，暂存区常驻内存开销可忽略
- 每帧每 draw call 最多创建 2 个 SingleFrame UB（vertex + fragment）
- 与 Metal 后端相比多了一次 staging → GPU copy（unmap 时），但 UNIFORM buffer 极小

---

## 附录 M: CreateBlendState / CreateRasterizerState / CreateDepthStencilState 完整实现

### M.1 CreateBlendState

```cpp
static FRHIBlendState* CreateBlendState(
    const std::vector<PipelineColorAttachment>& colorAttachments) {
  FBlendStateInitializerRHI init;
  init.bUseIndependentRenderTargetBlendStates = (colorAttachments.size() > 1);

  for (size_t i = 0; i < colorAttachments.size() && i < MaxSimultaneousRenderTargets; ++i) {
    const auto& ca = colorAttachments[i];
    auto& rt = init.RenderTargets[i];
    if (ca.blendEnable) {
      rt.ColorBlendOp   = ConvertBlendOp(ca.colorBlendOp);
      rt.ColorSrcBlend   = ConvertBlendFactor(ca.srcColorBlendFactor);
      rt.ColorDestBlend  = ConvertBlendFactor(ca.dstColorBlendFactor);
      rt.AlphaBlendOp    = ConvertBlendOp(ca.alphaBlendOp);
      rt.AlphaSrcBlend   = ConvertBlendFactor(ca.srcAlphaBlendFactor);
      rt.AlphaDestBlend  = ConvertBlendFactor(ca.dstAlphaBlendFactor);
    } else {
      rt.ColorBlendOp = BO_Add;  rt.ColorSrcBlend = BF_One;  rt.ColorDestBlend = BF_Zero;
      rt.AlphaBlendOp = BO_Add;  rt.AlphaSrcBlend = BF_One;  rt.AlphaDestBlend = BF_Zero;
    }
    rt.ColorWriteMask = ConvertColorWriteMask(ca.colorWriteMask);
  }
  return RHICreateBlendState(init);
}
```

### M.2 CreateRasterizerState

```cpp
static FRHIRasterizerState* CreateRasterizerState(const PrimitiveDescriptor& prim) {
  FRasterizerStateInitializerRHI init;
  init.FillMode = FM_Solid;
  init.DepthBias = 0.0f;
  init.SlopeScaleDepthBias = 0.0f;
  init.bAllowMSAA = true;

  // tgfx FrontFace::CW 与 UE 默认 CCW 不同，需要翻转 cull 判断
  if (prim.cullMode == CullMode::None) {
    init.CullMode = CM_None;
  } else if (prim.frontFace == FrontFace::CW) {
    init.CullMode = (prim.cullMode == CullMode::Back) ? CM_CW : CM_CCW;
  } else {
    init.CullMode = (prim.cullMode == CullMode::Back) ? CM_CCW : CM_CW;
  }
  return RHICreateRasterizerState(init);
}
```

### M.3 CreateDepthStencilState

```cpp
static FRHIDepthStencilState* CreateDepthStencilState(const DepthStencilDescriptor& ds) {
  FDepthStencilStateInitializerRHI init(
      ds.depthWriteEnabled,
      ConvertCompareFunction(ds.depthCompare),
      ConvertCompareFunction(ds.stencilFront.compare),
      ConvertStencilOp(ds.stencilFront.failOp),
      ConvertStencilOp(ds.stencilFront.depthFailOp),
      ConvertStencilOp(ds.stencilFront.passOp),
      ConvertCompareFunction(ds.stencilBack.compare),
      ConvertStencilOp(ds.stencilBack.failOp),
      ConvertStencilOp(ds.stencilBack.depthFailOp),
      ConvertStencilOp(ds.stencilBack.passOp),
      ds.stencilReadMask,
      ds.stencilWriteMask);
  return RHICreateDepthStencilState(init);
}
```

### M.4 PSO RenderTargetFormats 填充（补充 Ch.9 的 `// ... 填充渲染目标格式等`）

```cpp
// UERHIRenderPipeline::initialize() 中，紧接 psoInitializer.PrimitiveType 之后

psoInitializer.RenderTargetsEnabled = descriptor.fragment.colorAttachments.size();
for (size_t i = 0; i < descriptor.fragment.colorAttachments.size(); ++i) {
  EPixelFormat fmt = _gpu->getUEPixelFormat(descriptor.fragment.colorAttachments[i].format);
  psoInitializer.RenderTargetFormats[i] = UE_PIXELFORMAT_TO_UINT8(fmt);
  psoInitializer.RenderTargetFlags[i] = TexCreate_RenderTargetable | TexCreate_ShaderResource;
}

if (descriptor.depthStencil.format != PixelFormat::Unknown) {
  psoInitializer.DepthStencilTargetFormat = _gpu->getUEPixelFormat(descriptor.depthStencil.format);
  psoInitializer.DepthStencilTargetFlag = TexCreate_DepthStencilTargetable;
}

psoInitializer.NumSamples = descriptor.multisample.count;
```

---

## 附录 N: 顶点声明 StreamIndex 与 AttributeIndex 分配规则

### N.1 问题

Ch.9.2 的 PSO 初始化流程中，`FVertexElement` 的 `StreamIndex` 和 `AttributeIndex` 使用了占位注释 `/* slot */` 和 `/* location from shader */`，未给出明确值。

### N.2 分配规则（参考 Metal 后端 `MetalRenderPipeline::configureVertexDescriptor`）

tgfx 的 `RenderPipelineDescriptor.vertex.bufferLayouts` 是一个数组，每个 layout 对应一个顶点流（slot）。其中：
- slot 0 = per-vertex 数据（位置、UV、颜色等）
- slot 1 = per-instance 数据（实例化绘制时）

**StreamIndex** = bufferLayout 在数组中的索引（即 slot）。

**AttributeIndex** = 跨所有 bufferLayout 全局递增的序号（从 0 开始）。例如 slot 0 有 3 个属性（index 0,1,2），slot 1 有 2 个属性（index 3,4）。

这与 SPIRV-Cross 生成 HLSL 时的 `TEXCOORD` 语义编号一致：`TEXCOORD0, TEXCOORD1, TEXCOORD2, ...`。

### N.3 修正后的完整实现

```cpp
// UERHIRenderPipeline::initialize() 中顶点声明创建

FVertexDeclarationElementList vertexElements;
uint8 globalAttributeIndex = 0;

for (size_t bufferIndex = 0; bufferIndex < descriptor.vertex.bufferLayouts.size();
     ++bufferIndex) {
  const auto& layout = descriptor.vertex.bufferLayouts[bufferIndex];
  uint32 currentOffset = 0;

  for (size_t attrIndex = 0; attrIndex < layout.attributes.size(); ++attrIndex) {
    const auto& attr = layout.attributes[attrIndex];

    FVertexElement element;
    element.StreamIndex = static_cast<uint8>(bufferIndex);       // slot 索引
    element.Offset = static_cast<uint8>(currentOffset);          // 属性在 buffer 中的偏移
    element.Type = ConvertVertexFormat(attr.format());            // 附录 J
    element.AttributeIndex = globalAttributeIndex;                // 全局递增序号
    element.Stride = static_cast<uint16>(layout.stride);
    element.bUseInstanceIndex = (layout.stepMode == VertexStepMode::Instance) ? 1 : 0;

    vertexElements.Add(element);
    currentOffset += attr.size();
    globalAttributeIndex++;
  }
}

vertexDeclaration = RHICreateVertexDeclaration(vertexElements);
```

### N.4 HLSL 语义对应关系

SPIRV-Cross 将 GLSL 的 `layout(location = N) in` 转换为 HLSL 的 `TEXCOORD` 语义：

```hlsl
// SPIRV-Cross 输出的 HLSL vertex shader 输入
struct VertexInput {
  float2 inPosition : TEXCOORD0;    // globalAttributeIndex = 0
  float2 inUVCoord  : TEXCOORD1;    // globalAttributeIndex = 1
  float4 inColor    : TEXCOORD2;    // globalAttributeIndex = 2
  // 如果有 instance 属性:
  float4 instData   : TEXCOORD3;    // globalAttributeIndex = 3 (slot 1)
};
```

UE 的 `FVertexElement::AttributeIndex` 对应 HLSL 中 `TEXCOORD{N}` 的 N。因此 `globalAttributeIndex` 直接匹配 shader 中的语义编号。

---

## 附录 O: [P0] Shader 方案修正 -- UE Global Shader + Permutation 预编译

> 本附录基于 Rive UE 5.7 插件（`RivePlug2459fedbda56V2`）最新源码分析，替代第 14 章、第 19 章、
> 附录 D/F/G 中的运行时编译方案，作为 PAG UE RHI 后端的 **推荐 Shader 方案**。

### O.1 方案概述

**核心思路**：将 tgfx 的 GLSL Shader 源码在构建时转换为 UE 兼容的 `.ush`/`.usf` 文件，使用 UE 的
Global Shader 系统（`FGlobalShader`）注册，通过 Shader Permutation 替代运行时动态拼装。

```
构建时:
  tgfx GLSL 源码 → 构建工具转换 → Generated/*.ush (HLSL 兼容)
  入口 .usf 文件（手写，每种 DrawType 一个）
  C++ Shader 类声明 + IMPLEMENT_GLOBAL_SHADER 注册

UE 编译管线:
  .usf + .ush → UE Shader Compiler → 各平台字节码 (DXIL/SPIR-V/metallib)
  × Permutation 组合数 = 预编译所有变体

运行时:
  ProgramInfo → Permutation 选择 → TShaderMapRef 获取预编译 Shader
  → SetGraphicsPipelineState → 绘制
```

### O.2 Shader 文件组织

```
PAGPlugin/
├── Shaders/Private/TGFX/
│   ├── tgfx_base.ush                  -- 公共头文件（include Generated 模块）
│   ├── tgfx_permutation_adapter.ush   -- Permutation 宏适配层
│   ├── color_filter.usf               -- 颜色滤镜入口
│   ├── blend_compose.usf              -- 混合组合入口
│   ├── texture_effect.usf             -- 纹理效果入口
│   ├── gradient_fill.usf              -- 渐变填充入口
│   ├── image_draw.usf                 -- 图片绘制入口
│   ├── ... (每种 tgfx DrawOp/Processor 一个 .usf)
│   └── Generated/
│       ├── tgfx_types.ush             -- 数据类型映射（GLSL -> HLSL）
│       ├── tgfx_uniforms.ush          -- Uniform 定义
│       ├── tgfx_common.ush            -- 公共工具函数
│       ├── color_processor.ush        -- 颜色处理 Shader 模块
│       ├── coverage_processor.ush     -- 覆盖率处理模块
│       ├── xfer_processor.ush         -- 传输处理模块
│       └── ... (从 tgfx GLSL 自动生成)
│
├── Source/PAGShaders/
│   ├── Public/
│   │   ├── PAGShaderTypes.h           -- Shader 类声明 + 参数结构
│   │   └── PAGShadersModule.h
│   └── Private/
│       ├── PAGShaderTypes.cpp         -- IMPLEMENT_GLOBAL_SHADER 注册
│       └── PAGShadersModule.cpp       -- AddShaderSourceDirectoryMapping
```

### O.3 入口 .usf 文件模式

每个 `.usf` 文件极简，仅定义特性开关 + include 公共头：

```hlsl
// gradient_fill.usf
#define TGFX_GRADIENT_FILL
#include "/Engine/Public/Platform.ush"
#include "tgfx_permutation_adapter.ush"
#include "tgfx_base.ush"
```

### O.4 Permutation 适配层

`tgfx_permutation_adapter.ush` 将 UE Permutation 宏转换为 tgfx Shader 期望的 `#ifdef` 风格：

```hlsl
// UE Permutation 始终 #define XX 0 或 #define XX 1
// tgfx Shader 使用 #ifdef XX 判断，需要在值为 0 时 #undef
#if !TGFX_HAS_COLOR_FILTER
#undef TGFX_HAS_COLOR_FILTER
#endif

#if !TGFX_HAS_COVERAGE_MASK
#undef TGFX_HAS_COVERAGE_MASK
#endif

#if !TGFX_HAS_CLIP
#undef TGFX_HAS_CLIP
#endif

// ... 对所有 tgfx 动态拼装选项做同样的适配
```

### O.5 Shader Permutation 定义

将 tgfx 的动态 Shader 拼装选项映射为 UE Permutation 维度：

```cpp
// PAGShaderTypes.h
class FHasColorFilter   : SHADER_PERMUTATION_BOOL("TGFX_HAS_COLOR_FILTER");
class FHasCoverageMask  : SHADER_PERMUTATION_BOOL("TGFX_HAS_COVERAGE_MASK");
class FHasClip          : SHADER_PERMUTATION_BOOL("TGFX_HAS_CLIP");
class FUseTexture       : SHADER_PERMUTATION_BOOL("TGFX_USE_TEXTURE");
class FUseGradient      : SHADER_PERMUTATION_BOOL("TGFX_USE_GRADIENT");
class FBlendModeIndex   : SHADER_PERMUTATION_RANGE_INT("TGFX_BLEND_MODE", 0, 3);

typedef TShaderPermutationDomain<
    FHasColorFilter, FHasCoverageMask, FHasClip,
    FUseTexture, FUseGradient, FBlendModeIndex>
    TGFXPixelPermutationDomain;

typedef TShaderPermutationDomain<FHasClip, FUseTexture>
    TGFXVertexPermutationDomain;
```

过滤无效组合控制编译数量：

```cpp
template <typename ShaderClass>
static bool ShouldCompilePermutation(const FShaderPermutationParameters& Params)
{
    typename ShaderClass::FPermutationDomain Perm(Params.PermutationId);
    if (Perm.Get<FUseTexture>() && Perm.Get<FUseGradient>())
        return false; // 互斥
    return true;
}
```

### O.6 Shader 类声明与注册

```cpp
// PAGShaderTypes.h
BEGIN_UNIFORM_BUFFER_STRUCT(FTGFXFlushUniforms, PAGSHADERS_API)
SHADER_PARAMETER(float, viewportWidth)
SHADER_PARAMETER(float, viewportHeight)
SHADER_PARAMETER(UE::HLSL::float4x4, viewMatrix)
END_UNIFORM_BUFFER_STRUCT();

BEGIN_SHADER_PARAMETER_STRUCT(FTGFXPixelDrawUniforms, PAGSHADERS_API)
SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FTGFXFlushUniforms, FlushUniforms)
SHADER_PARAMETER_RDG_TEXTURE(Texture2D, inputTexture)
SHADER_PARAMETER_SAMPLER(SamplerState, inputSampler)
END_SHADER_PARAMETER_STRUCT()

class FTGFXGradientFillPixelShader : public FGlobalShader {
    DECLARE_EXPORTED_GLOBAL_SHADER(FTGFXGradientFillPixelShader, PAGSHADERS_API);
    SHADER_USE_PARAMETER_STRUCT(FTGFXGradientFillPixelShader, FGlobalShader);
    using FParameters = FTGFXPixelDrawUniforms;
    using FPermutationDomain = TGFXPixelPermutationDomain;
};
```

```cpp
// PAGShaderTypes.cpp
IMPLEMENT_GLOBAL_SHADER(FTGFXGradientFillPixelShader,
    "/Plugin/PAG/Private/TGFX/gradient_fill.usf",
    "FragmentMain", SF_Pixel);

void FPAGShadersModule::StartupModule() {
    FString ShaderDir = FPaths::Combine(
        IPluginManager::Get().FindPlugin(TEXT("PAG"))->GetBaseDir(),
        TEXT("Shaders"));
    AddShaderSourceDirectoryMapping(TEXT("/Plugin/PAG"), ShaderDir);
}
```

### O.7 运行时 Permutation 选择

```cpp
void SelectShaderPermutation(
    const ProgramInfo& programInfo,
    TGFXPixelPermutationDomain& pixelPerm,
    TGFXVertexPermutationDomain& vertexPerm)
{
    pixelPerm.Set<FHasColorFilter>(programInfo.hasColorFilter());
    pixelPerm.Set<FHasCoverageMask>(programInfo.hasCoverageMask());
    pixelPerm.Set<FHasClip>(programInfo.hasClip());
    pixelPerm.Set<FUseTexture>(programInfo.usesTexture());
    pixelPerm.Set<FUseGradient>(programInfo.usesGradient());
    vertexPerm.Set<FHasClip>(programInfo.hasClip());
    vertexPerm.Set<FUseTexture>(programInfo.usesTexture());
}

// 获取预编译 Shader 并绘制
auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
TShaderMapRef<FTGFXGradientFillPixelShader> PixelShader(ShaderMap, pixelPerm);
TShaderMapRef<FTGFXGradientFillVertexShader> VertexShader(ShaderMap, vertexPerm);
```

### O.8 GLSL -> HLSL 兼容 .ush 转换

构建工具转换规则：

| GLSL | HLSL (.ush) |
|------|-------------|
| `vec2/vec3/vec4` | `float2/float3/float4` |
| `mat4` | `float4x4` |
| `layout(binding=N) uniform` | C++ 侧 `SHADER_PARAMETER` 定义 |
| `in`/`out` varying | `TEXCOORD{N}` 语义 |
| `texture(sampler, uv)` | `sampler.Sample(state, uv)` |
| `#ifdef FEATURE` | 保持不变（通过 Permutation 适配层控制） |

参考 Rive 的 `rhi.minified.ush`：通过宏适配层（定义 `INLINE`、`OUT()`、`INOUT()`、
`ATTR_BLOCK_BEGIN` 等）最小化 Shader 源码修改。

### O.9 与原方案对比

| 维度 | 原方案（第 14 章/附录 D/F） | 新方案（本附录） |
|------|---------------------------|-----------------|
| **编译时机** | 运行时（shaderc + SPIRV-Cross + D3DCompile） | 构建时（UE Shader 编译管线） |
| **运行时开销** | 首次编译 + 磁盘缓存 | 零编译开销 |
| **UE 集成度** | 低（绕过 UE Shader 系统） | 高（完全融入 UE Global Shader） |
| **第三方依赖** | 需 shaderc、SPIRV-Cross、d3dcompiler_47.dll | 无额外依赖 |
| **PSO 缓存** | 需自行实现 | 自动享受 UE PSO 缓存 |
| **调试工具** | 需自行集成 | 可使用 UE Shader Debugger、RenderDoc |
| **已验证** | 未验证 | Rive UE 5.7 已在生产环境验证 |

### O.10 对设计文档其他章节的影响

1. **第 9 章（UERHIRenderPipeline）**：PSO 通过 `TShaderMapRef` + `SetGraphicsPipelineState()`
   创建，不再需要手动 `RHICreateVertexShader` / `RHICreatePixelShader`

2. **第 14 章（UERHIShaderModule）**：该类不再需要，Shader 由 UE 管线处理

3. **附录 D/F/G**：运行时编译方案保留作为备选，推荐方案为本附录

4. **附录 E（Uniform 绑定）**：通过 `BEGIN_UNIFORM_BUFFER_STRUCT` /
   `BEGIN_SHADER_PARAMETER_STRUCT` 定义，使用 `SetShaderParameters()` 绑定