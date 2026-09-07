# Metal 快速入门

## 1. Metal 是什么

Metal 是 Apple 推出的低级别图形与计算 API，专为 Apple 硬件（iOS、macOS、tvOS、visionOS）设计。它在 2014 年随 iOS 8 发布，目标是取代 OpenGL ES，提供更低的 CPU 开销和更直接的 GPU 控制。

**核心优势：** Apple 软硬一体的生态使 Metal 能做到极致的驱动精简和硬件特性暴露，同时 API 比 Vulkan 更简洁易用。

---

## 2. 核心架构概览

```
Application
    │
    ▼
MTLDevice ─────────────────────────────────────────┐
    │                                               │
    ├── MTLCommandQueue                             │
    │       └── MTLCommandBuffer                    │
    │               ├── MTLRenderCommandEncoder      │
    │               ├── MTLComputeCommandEncoder     │
    │               └── MTLBlitCommandEncoder        │
    │                                               │
    ├── MTLRenderPipelineState                      │
    ├── MTLDepthStencilState                        │
    │                                               │
    ├── MTLBuffer                                   │
    ├── MTLTexture                                  │
    ├── MTLSamplerState                             │
    │                                               │
    ├── MTLLibrary ── MTLFunction                   │
    │                                               │
    └── MTLRenderPassDescriptor ── MTLTexture (RT)  │
                                                    │
CAMetalLayer ── CAMetalDrawable ────────────────────┘
```

---

## 3. 关键对象与概念

### 3.1 MTLDevice

GPU 的抽象，Metal 中一切资源的工厂。一台设备通常只有一个 MTLDevice 实例。

```objc
id<MTLDevice> device = MTLCreateSystemDefaultDevice();

// macOS 上可枚举所有 GPU
NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
```

与 Vulkan 的对比：Metal 没有 Instance/PhysicalDevice 的二级结构，`MTLDevice` 直接对应 `VkDevice` + `VkPhysicalDevice`。

### 3.2 Command Queue 与 Command Buffer

- **MTLCommandQueue**：长生命周期对象，从 Device 创建，线程安全。通常整个 App 只需要一个。
- **MTLCommandBuffer**：每帧从 Queue 创建，封装一帧内所有 GPU 命令。提交后不可复用。

```objc
id<MTLCommandQueue> queue = [device newCommandQueue];

// 每帧
id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
// ... 编码命令 ...
[commandBuffer commit];
```

**关键设计差异：** OpenGL 是即时模式（调用即执行），Metal 和 Vulkan 都是录制-提交模式。但 Metal 的 CommandBuffer 比 Vulkan 简单得多——不需要 CommandPool，不需要手动 reset，每帧直接从 Queue 创建即可。

### 3.3 Command Encoder

Metal 使用三种 Encoder 编码不同类型的 GPU 命令。同一个 CommandBuffer 内可以依次创建多个 Encoder，但**同一时刻只能有一个活跃的 Encoder**。

| Encoder 类型 | 用途 | 对应操作 |
|-------------|------|---------|
| **MTLRenderCommandEncoder** | 图形渲染 | 绑定管线、设置资源、绘制 |
| **MTLComputeCommandEncoder** | 通用计算 | dispatch 计算着色器 |
| **MTLBlitCommandEncoder** | 数据搬运 | 纹理/Buffer 拷贝、mipmap 生成 |

```objc
// 渲染编码器
id<MTLRenderCommandEncoder> encoder =
    [commandBuffer renderCommandEncoderWithDescriptor:renderPassDesc];
[encoder setRenderPipelineState:pipelineState];
[encoder setVertexBuffer:vertexBuffer offset:0 atIndex:0];
[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
[encoder endEncoding];

// Blit 编码器
id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
[blit copyFromTexture:src toTexture:dst];
[blit generateMipmapsForTexture:texture];
[blit endEncoding];
```

### 3.4 Render Pass

通过 **MTLRenderPassDescriptor** 定义，描述一次渲染的目标和行为：

```objc
MTLRenderPassDescriptor *desc = [MTLRenderPassDescriptor new];

// 颜色附件
desc.colorAttachments[0].texture = drawable.texture;
desc.colorAttachments[0].loadAction = MTLLoadActionClear;
desc.colorAttachments[0].storeAction = MTLStoreActionStore;
desc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

// 深度附件
desc.depthAttachment.texture = depthTexture;
desc.depthAttachment.loadAction = MTLLoadActionClear;
desc.depthAttachment.storeAction = MTLStoreActionDontCare;
desc.depthAttachment.clearDepth = 1.0;
```

**Load/Store Action** 是 Metal 针对 Tile-Based GPU 的关键优化点：

| Action | Load | Store |
|--------|------|-------|
| **Clear** | 渲染前清空 | — |
| **Load** | 从显存加载内容 | — |
| **DontCare** | 不关心初始值 | 渲染后丢弃 |
| **Store** | — | 写回显存 |

在 Apple GPU（Tile-Based Deferred Rendering）上，正确设置 Load/Store Action 可以**显著减少带宽消耗**。例如深度缓冲通常设为 `DontCare` store，因为渲染完成后不再需要。

### 3.5 Render Pipeline State

Metal 的管线状态也是**预编译的不可变对象**，但比 Vulkan 更简洁：

```objc
MTLRenderPipelineDescriptor *pipelineDesc = [MTLRenderPipelineDescriptor new];
pipelineDesc.vertexFunction = vertexFunction;
pipelineDesc.fragmentFunction = fragmentFunction;
pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
pipelineDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

// 顶点布局
MTLVertexDescriptor *vertexDesc = [MTLVertexDescriptor new];
vertexDesc.attributes[0].format = MTLVertexFormatFloat3;  // position
vertexDesc.attributes[0].offset = 0;
vertexDesc.attributes[0].bufferIndex = 0;
vertexDesc.layouts[0].stride = sizeof(Vertex);
pipelineDesc.vertexDescriptor = vertexDesc;

// 混合
pipelineDesc.colorAttachments[0].blendingEnabled = YES;
pipelineDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
pipelineDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

// 编译管线（可能耗时，应缓存或异步创建）
NSError *error = nil;
id<MTLRenderPipelineState> pipelineState =
    [device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
```

**Metal vs Vulkan 管线差异：**
- Metal 不需要显式的 VkRenderPass 与管线绑定，只需匹配像素格式
- 深度/模板状态是独立对象（MTLDepthStencilState），不属于管线
- 视口和裁剪是动态设置的，不需要像 Vulkan 那样声明 DynamicState

### 3.6 Shader（Metal Shading Language）

Metal 使用自有的 **Metal Shading Language (MSL)**，语法基于 C++14：

```metal
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
};

vertex VertexOut vertexShader(VertexIn in [[stage_in]],
                              constant float4x4 &mvp [[buffer(1)]]) {
    VertexOut out;
    out.position = mvp * float4(in.position, 1.0);
    out.texCoord = in.texCoord;
    return out;
}

fragment float4 fragmentShader(VertexOut in [[stage_in]],
                               texture2d<float> tex [[texture(0)]],
                               sampler samp [[sampler(0)]]) {
    return tex.sample(samp, in.texCoord);
}
```

着色器编译方式：

| 方式 | 说明 | 适用场景 |
|------|------|---------|
| 离线编译 | Xcode 编译 `.metal` → `.metallib` | 发布版本 |
| 运行时编译 | `newLibraryWithSource:` | 调试/动态生成 |
| 二进制归档 | `MTLBinaryArchive` | 缓存编译结果避免首帧卡顿 |

```objc
// 从默认 metallib 加载
id<MTLLibrary> library = [device newDefaultLibrary];
id<MTLFunction> vertexFunc = [library newFunctionWithName:@"vertexShader"];
id<MTLFunction> fragFunc = [library newFunctionWithName:@"fragmentShader"];

// 运行时编译
NSString *source = @"...MSL code...";
id<MTLLibrary> lib = [device newLibraryWithSource:source options:nil error:&error];
```

### 3.7 资源：Buffer 与 Texture

#### Buffer

```objc
// 从数据创建
id<MTLBuffer> buffer = [device newBufferWithBytes:data
                                           length:dataSize
                                          options:MTLResourceStorageModeShared];

// 空 Buffer
id<MTLBuffer> buffer = [device newBufferWithLength:size
                                           options:MTLResourceStorageModePrivate];

// CPU 写入（Shared 模式）
memcpy(buffer.contents, data, size);
```

#### Texture

```objc
MTLTextureDescriptor *desc = [MTLTextureDescriptor new];
desc.pixelFormat = MTLPixelFormatRGBA8Unorm;
desc.width = 512;
desc.height = 512;
desc.usage = MTLTextureUsageShaderRead;
desc.storageMode = MTLStorageModePrivate;
id<MTLTexture> texture = [device newTextureWithDescriptor:desc];

// 上传数据（通过 Blit Encoder 从 Shared Buffer 拷贝到 Private Texture）
```

#### Storage Mode（Apple GPU 关键概念）

| 模式 | CPU 访问 | GPU 访问 | 说明 |
|------|---------|---------|------|
| **Shared** | 可读写 | 可读写 | CPU/GPU 共享内存（Apple Silicon 统一内存） |
| **Private** | 不可访问 | 可读写 | 仅 GPU 可见，性能最优 |
| **Memoryless** | 不可访问 | Tile 内可用 | MSAA 临时缓冲，不占显存 |

Apple Silicon 的**统一内存架构（UMA）** 意味着 Shared 模式不需要 CPU→GPU 数据拷贝，只需确保同步即可。这是 Metal 相比 Vulkan 在 Apple 硬件上的独特优势。

### 3.8 Sampler

```objc
MTLSamplerDescriptor *samplerDesc = [MTLSamplerDescriptor new];
samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
samplerDesc.mipFilter = MTLSamplerMipFilterLinear;
samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:samplerDesc];
```

### 3.9 资源绑定模型（Argument Table）

Metal 的资源绑定比 Vulkan 的 Descriptor Set 简单直接得多。使用 **Argument Table** 模型，每种资源类型有独立的绑定槽位：

```
Buffer Table:   [0] vertexData   [1] uniforms   [2] ...
Texture Table:  [0] albedo       [1] normal     [2] ...
Sampler Table:  [0] linearSamp   [1] ...
```

```objc
[encoder setVertexBuffer:vertexBuffer offset:0 atIndex:0];
[encoder setVertexBuffer:uniformBuffer offset:0 atIndex:1];
[encoder setFragmentTexture:albedoTexture atIndex:0];
[encoder setFragmentSamplerState:sampler atIndex:0];
```

**对比 Vulkan：** 不需要 DescriptorSetLayout、DescriptorPool、DescriptorSet 等概念，直接按 index 绑定即可。

对于需要更高级绑定方式的场景，Metal 3 引入了 **Argument Buffer**（类似 Vulkan 的 Descriptor Set）：

```objc
// Argument Buffer 允许将多个资源打包到一个 Buffer 中
// 适用于 Bindless 渲染等高级场景
```

### 3.10 同步机制

Metal 的同步比 Vulkan 简化很多：

| 机制 | 用途 | 说明 |
|------|------|------|
| **CommandBuffer completion** | CPU 等 GPU | 回调或 waitUntilCompleted |
| **MTLFence** | Encoder 间同步 | 同一 CommandBuffer 内不同 Encoder 间 |
| **MTLEvent** | CommandBuffer 间同步 | 跨 CommandBuffer 的 GPU-GPU 同步 |
| **MTLSharedEvent** | CPU-GPU 双向同步 | 可从 CPU 端 signal/wait |

```objc
// 完成回调
[commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
    // GPU 执行完毕，可以安全读取结果
}];

// 阻塞等待
[commandBuffer waitUntilCompleted];

// MTLEvent：GPU-GPU 同步
id<MTLEvent> event = [device newEvent];
[commandBuffer1 encodeSignalEvent:event value:1];
[commandBuffer2 encodeWaitForEvent:event value:1];
```

**关键简化：** Metal 的 CommandBuffer 内部保证 Encoder 按顺序执行，不需要像 Vulkan 那样在每个资源状态转换处插入 Pipeline Barrier。跨 CommandBuffer 同步用 MTLEvent 即可。

### 3.11 Metal 与显示系统的集成

```objc
// CAMetalLayer 是连接 Metal 与屏幕的桥梁
CAMetalLayer *metalLayer = [CAMetalLayer layer];
metalLayer.device = device;
metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
metalLayer.framebufferOnly = YES;

// 每帧获取 drawable
id<CAMetalDrawable> drawable = [metalLayer nextDrawable];

// 渲染到 drawable.texture
renderPassDesc.colorAttachments[0].texture = drawable.texture;
// ... 编码渲染命令 ...

// 呈现
[commandBuffer presentDrawable:drawable];
[commandBuffer commit];
```

---

## 4. 一帧的完整流程

```
 1. drawable = [metalLayer nextDrawable]         // 获取可用帧缓冲
 2. commandBuffer = [queue commandBuffer]         // 创建命令缓冲
 3. renderPassDesc.colorAttachments[0].texture    // 设置渲染目标
      = drawable.texture
 4. encoder = [commandBuffer                      // 创建渲染编码器
      renderCommandEncoderWithDescriptor:desc]
 5. [encoder setRenderPipelineState:pipeline]     // 绑定管线
 6. [encoder setVertexBuffer:buf atIndex:0]       // 绑定资源
 7. [encoder setFragmentTexture:tex atIndex:0]
 8. [encoder drawPrimitives:... vertexCount:...]  // 绘制
 9. [encoder endEncoding]                         // 结束编码
10. [commandBuffer presentDrawable:drawable]      // 标记呈现
11. [commandBuffer commit]                        // 提交执行
```

---

## 5. Metal 独有特性

### 5.1 Tile Shading（iOS/Apple GPU）

Apple GPU 是 Tile-Based Deferred Rendering (TBDR) 架构。Metal 暴露了 **Tile Function**，可以在 Tile Memory 中直接进行自定义计算，无需回写主存：

```metal
kernel void tileShader(imageblock<ImgBlockLayout> imgBlock,
                       ushort2 tid [[thread_position_in_threadgroup]]) {
    // 直接操作 Tile Memory 中的像素数据
}
```

适用场景：延迟渲染的 G-Buffer 合并、后处理、MSAA Resolve 自定义等。

### 5.2 Indirect Command Buffer (ICB)

允许在 GPU 端生成绘制命令，实现 GPU-Driven Rendering：

```objc
// CPU 端创建 ICB 描述
MTLIndirectCommandBufferDescriptor *icbDesc = [MTLIndirectCommandBufferDescriptor new];
icbDesc.commandTypes = MTLIndirectCommandTypeDraw;
icbDesc.maxVertexBufferBindCount = 2;

id<MTLIndirectCommandBuffer> icb =
    [device newIndirectCommandBufferWithDescriptor:icbDesc
                                   maxCommandCount:1000
                                           options:0];

// GPU 端通过 Compute Shader 填充 ICB
// 然后 Render Encoder 执行
[renderEncoder executeCommandsInBuffer:icb withRange:NSMakeRange(0, drawCount)];
```

### 5.3 Metal Performance Shader (MPS)

Apple 提供的高性能计算内核库，覆盖图像处理、矩阵运算、光线追踪、神经网络等：

```objc
// 高斯模糊
MPSImageGaussianBlur *blur =
    [[MPSImageGaussianBlur alloc] initWithDevice:device sigma:5.0];
[blur encodeToCommandBuffer:commandBuffer
              sourceTexture:inputTexture
         destinationTexture:outputTexture];

// 矩阵乘法
MPSMatrixMultiplication *matMul =
    [[MPSMatrixMultiplication alloc] initWithDevice:device
                                     transposeLeft:NO
                                    transposeRight:NO
                                        resultRows:M
                                     resultColumns:N
                                   interiorColumns:K
                                             alpha:1.0
                                              beta:0.0];
```

### 5.4 Ray Tracing（Metal 3+）

Metal 3 引入硬件加速光线追踪：

```objc
// 构建加速结构
MTLAccelerationStructureDescriptor *asDesc = ...;
id<MTLAccelerationStructure> accelStruct =
    [device newAccelerationStructureWithDescriptor:asDesc];

// 在 Compute 或 Fragment Shader 中追踪光线
// intersector.intersect(ray, accelStruct)
```

### 5.5 Mesh Shader（Metal 3+）

替代传统的顶点着色器 + 输入装配流程：

```metal
[[mesh]] void meshShader(object_data const ObjectData *objectData,
                         mesh<VertexOut, PrimitiveOut, MAX_VERTS, MAX_PRIMS, topology::triangle> m,
                         uint tid [[thread_index_in_threadgroup]]) {
    // 直接输出顶点和图元
}
```

---

## 6. 与 OpenGL 的关键差异

| 维度 | OpenGL | Metal |
|------|--------|-------|
| 平台 | 跨平台 | Apple 独占 |
| API 风格 | C 函数 + 全局状态机 | Objective-C/Swift 对象式 |
| 管线 | 运行时可变状态 | 预编译不可变 Pipeline State |
| 命令模型 | 即时执行 | 录制-提交 |
| 多线程 | 困难（单上下文） | 天然支持（多 Encoder 并行编码） |
| 内存模型 | 驱动隐式管理 | Storage Mode 显式选择 |
| 着色器 | GLSL | MSL (Metal Shading Language) |
| Tile GPU 优化 | 无法控制 | Load/Store Action + Tile Function |

---

## 7. 与 TGFX RHI 的对应关系

| TGFX 抽象 | Metal 对应 |
|-----------|-----------|
| `GPU` | `MTLDevice` |
| `Texture` | `MTLTexture` |
| `GPUBuffer` | `MTLBuffer` |
| `Sampler` | `MTLSamplerState` |
| `ShaderModule` | `MTLLibrary` + `MTLFunction` |
| `RenderPipeline` | `MTLRenderPipelineState` |
| `RenderPass` | `MTLRenderPassDescriptor` + `MTLRenderCommandEncoder` |
| `CommandEncoder` | `MTLRenderCommandEncoder` / `MTLBlitCommandEncoder` |
| `CommandBuffer` | `MTLCommandBuffer` |
| `CommandQueue` | `MTLCommandQueue` |
| `Window` (MetalWindow) | `CAMetalLayer` + `CAMetalDrawable` |

---

## 8. 推荐学习资源

- [Metal Programming Guide](https://developer.apple.com/metal/) — Apple 官方文档
- [Metal Best Practices Guide](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/) — 性能优化指南
- [Metal by Example](https://metalbyexample.com/) — 经典入门教程
- [Metal Shading Language Specification](https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf) — MSL 语言规范
- [WWDC Metal Sessions](https://developer.apple.com/videos/graphics-games/) — 每年 WWDC 的 Metal 新特性讲解
