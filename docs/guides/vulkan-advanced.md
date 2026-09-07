# Vulkan 进阶指南

## 1. 内存管理

### 1.1 内存架构

Vulkan 将显存分为多个 **Memory Type**，每种有不同的属性组合：

| 属性标志 | 含义 |
|---------|------|
| `DEVICE_LOCAL` | GPU 本地显存，GPU 访问最快 |
| `HOST_VISIBLE` | CPU 可映射，用于数据上传 |
| `HOST_COHERENT` | CPU 写入自动对 GPU 可见，无需手动 flush |
| `HOST_CACHED` | CPU 读取有缓存加速，用于回读 |
| `LAZILY_ALLOCATED` | 延迟分配，仅 Tile-Based GPU 支持 |

典型的内存配置（独显）：

```
Heap 0: DEVICE_LOCAL (8 GB VRAM)
  ├── Type 0: DEVICE_LOCAL                          → 纹理、渲染目标
  └── Type 1: DEVICE_LOCAL | HOST_VISIBLE           → 小型 Uniform Buffer（Bar 内存）

Heap 1: HOST (16 GB RAM)
  ├── Type 2: HOST_VISIBLE | HOST_COHERENT          → Staging Buffer
  └── Type 3: HOST_VISIBLE | HOST_CACHED | HOST_COHERENT → Readback Buffer
```

集成显卡 / 移动 GPU（统一内存）：

```
Heap 0: DEVICE_LOCAL | HOST_VISIBLE (共享内存)
  └── Type 0: DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT → 所有资源
```

### 1.2 内存分配策略

直接调用 `vkAllocateMemory` 有严重限制：

- 大多数驱动限制**最多 4096 次分配**
- 每次分配至少消耗一个页面（通常 4KB-64KB）
- 频繁分配/释放造成碎片

**正确做法：子分配（Sub-allocation）**

```
vkAllocateMemory → 256MB 大块
    ├── [0, 1MB)     → Texture A
    ├── [1MB, 1.5MB) → Buffer B (需对齐到 bufferImageGranularity)
    ├── [1.5MB, 3MB) → Texture C
    └── [3MB, 256MB) → 空闲
```

主流方案：

| 方案 | 说明 |
|------|------|
| **VMA (Vulkan Memory Allocator)** | AMD 开源库，业界标准，处理所有复杂性 |
| 自定义 Pool Allocator | 按资源类型分池，线性/伙伴系统分配 |

```cpp
// VMA 用法示例
VmaAllocatorCreateInfo allocatorInfo{};
allocatorInfo.physicalDevice = physicalDevice;
allocatorInfo.device = device;
allocatorInfo.instance = instance;
VmaAllocator allocator;
vmaCreateAllocator(&allocatorInfo, &allocator);

// 创建 Buffer + 自动分配内存
VkBufferCreateInfo bufInfo{};
bufInfo.size = 65536;
bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

VmaAllocationCreateInfo allocCreateInfo{};
allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

VkBuffer buffer;
VmaAllocation allocation;
vmaCreateBuffer(allocator, &bufInfo, &allocCreateInfo, &buffer, &allocation, nullptr);
```

### 1.3 Staging 模式与数据上传

GPU 本地内存（DEVICE_LOCAL）通常 CPU 不可见。上传数据需要经过 Staging Buffer：

```
CPU → Staging Buffer (HOST_VISIBLE) → Blit/Copy → Target (DEVICE_LOCAL)
```

```cpp
// 1. 创建 Staging Buffer
VkBuffer stagingBuffer;
VmaAllocation stagingAlloc;
// usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, HOST_VISIBLE

// 2. 写入数据
void* mapped;
vmaMapMemory(allocator, stagingAlloc, &mapped);
memcpy(mapped, pixelData, imageSize);
vmaUnmapMemory(allocator, stagingAlloc);

// 3. 录制拷贝命令
vkCmdCopyBufferToImage(cmd, stagingBuffer, image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

// 4. 提交后可释放 Staging Buffer
```

**优化技巧：**
- 使用 **Transfer Queue** 异步上传，不阻塞 Graphics Queue
- 对于频繁更新的小数据（Uniform），优先使用 `HOST_VISIBLE | DEVICE_LOCAL`（BAR 内存，如果可用）
- 大批量上传时，使用一个大 Staging Buffer 分段复用

---

## 2. 同步深入

### 2.1 Pipeline Barrier

Pipeline Barrier 是 Vulkan 同步的核心，解决两个问题：

1. **执行依赖**：确保先写后读的执行顺序
2. **内存可见性**：确保写操作对后续读操作可见（缓存刷新）

```cpp
VkImageMemoryBarrier barrier{};
barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;  // 写端
barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;             // 读端
barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
barrier.image = image;
barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

vkCmdPipelineBarrier(cmd,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,          // dstStageMask
    0, 0, nullptr, 0, nullptr, 1, &barrier);
```

**理解 Barrier 的关键模型：**

```
srcStageMask + srcAccessMask → "等谁完成 + 刷新什么缓存"
dstStageMask + dstAccessMask → "谁在等 + 使什么缓存失效"
```

### 2.2 常见同步场景速查

| 场景 | srcStage | srcAccess | dstStage | dstAccess |
|------|----------|-----------|----------|-----------|
| 渲染结果 → 采样 | COLOR_ATTACHMENT_OUTPUT | COLOR_WRITE | FRAGMENT_SHADER | SHADER_READ |
| 计算写入 → 渲染读取 | COMPUTE_SHADER | SHADER_WRITE | VERTEX_INPUT | VERTEX_READ |
| 上传完成 → 采样 | TRANSFER | TRANSFER_WRITE | FRAGMENT_SHADER | SHADER_READ |
| 渲染结果 → 呈现 | COLOR_ATTACHMENT_OUTPUT | COLOR_WRITE | BOTTOM_OF_PIPE | 0 |
| Mipmap 生成（逐级） | TRANSFER | TRANSFER_WRITE | TRANSFER | TRANSFER_READ |
| CPU 回读 | TRANSFER | TRANSFER_WRITE | HOST | HOST_READ |

### 2.3 Synchronization2（Vulkan 1.3）

Vulkan 1.3 引入了简化的同步 API `VK_KHR_synchronization2`，用更清晰的结构体替代旧的位掩码参数：

```cpp
VkImageMemoryBarrier2 barrier{};
barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
barrier.image = image;
barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

VkDependencyInfo depInfo{};
depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
depInfo.imageMemoryBarrierCount = 1;
depInfo.pImageMemoryBarriers = &barrier;
vkCmdPipelineBarrier2(cmd, &depInfo);
```

### 2.4 Timeline Semaphore

传统 Semaphore 是二值的（signal/wait），Timeline Semaphore 引入单调递增的计数器，大幅简化多帧同步：

```cpp
// 创建
VkSemaphoreTypeCreateInfo timelineInfo{};
timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
timelineInfo.initialValue = 0;

VkSemaphoreCreateInfo semInfo{};
semInfo.pNext = &timelineInfo;
vkCreateSemaphore(device, &semInfo, nullptr, &timelineSem);

// GPU signal: 帧 N 完成后 signal(N)
VkTimelineSemaphoreSubmitInfo tsInfo{};
uint64_t signalValue = frameIndex;
tsInfo.signalSemaphoreValueCount = 1;
tsInfo.pSignalSemaphoreValues = &signalValue;

// CPU wait: 等帧 N-2 完成（triple buffering）
VkSemaphoreWaitInfo waitInfo{};
waitInfo.semaphoreCount = 1;
waitInfo.pSemaphores = &timelineSem;
uint64_t waitValue = frameIndex - 2;
waitInfo.pValues = &waitValue;
vkWaitSemaphores(device, &waitInfo, UINT64_MAX);
```

**优势：** 一个 Timeline Semaphore 替代多个 Fence + 二值 Semaphore，逻辑更清晰。

---

## 3. Render Pass 进阶

### 3.1 Subpass 与 Subpass Dependency

多个 Subpass 允许在一个 Render Pass 内完成多阶段渲染（如延迟渲染），Tile-Based GPU 上可在 Tile Memory 内完成数据传递，避免回写主存：

```cpp
// Subpass 0: G-Buffer 填充
VkSubpassDescription gbufferPass{};
gbufferPass.colorAttachmentCount = 3;  // albedo, normal, position
gbufferPass.pColorAttachments = gbufferRefs;
gbufferPass.pDepthStencilAttachment = &depthRef;

// Subpass 1: 光照计算（读取 G-Buffer 作为 Input Attachment）
VkAttachmentReference inputRefs[] = {
    {0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},  // albedo
    {1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},  // normal
    {2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},  // position
};

VkSubpassDescription lightingPass{};
lightingPass.inputAttachmentCount = 3;
lightingPass.pInputAttachments = inputRefs;
lightingPass.colorAttachmentCount = 1;
lightingPass.pColorAttachments = &finalColorRef;

// Subpass 依赖
VkSubpassDependency dep{};
dep.srcSubpass = 0;
dep.dstSubpass = 1;
dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
dep.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;  // Tile 级依赖
```

### 3.2 Dynamic Rendering（Vulkan 1.3）

`VK_KHR_dynamic_rendering` 彻底消除了传统 Render Pass + Framebuffer 的预创建需求：

```cpp
VkRenderingAttachmentInfo colorAttachment{};
colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
colorAttachment.imageView = colorImageView;
colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
colorAttachment.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};

VkRenderingInfo renderInfo{};
renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
renderInfo.renderArea = {{0, 0}, extent};
renderInfo.layerCount = 1;
renderInfo.colorAttachmentCount = 1;
renderInfo.pColorAttachments = &colorAttachment;
renderInfo.pDepthAttachment = &depthAttachment;

vkCmdBeginRendering(cmd, &renderInfo);
// ... 绘制命令 ...
vkCmdEndRendering(cmd);
```

**优势：**
- 不再需要预创建 VkRenderPass 和 VkFramebuffer 对象
- 管线创建时通过 `VkPipelineRenderingCreateInfo` 指定附件格式
- 代码更简洁，更接近 Metal 和 DX12 的体验
- **推荐新项目一律使用 Dynamic Rendering**

### 3.3 MSAA Resolve

```cpp
// 渲染到 MSAA 纹理，Resolve 到单采样纹理
VkRenderingAttachmentInfo colorAttachment{};
colorAttachment.imageView = msaaImageView;         // 4x MSAA 渲染目标
colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
colorAttachment.resolveImageView = resolveImageView; // 单采样输出
colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
```

---

## 4. Descriptor 管理

### 4.1 Descriptor 管理策略

| 策略 | 适用场景 | 优劣 |
|------|---------|------|
| 每帧重建 | 简单原型 | 简单但开销大 |
| 预分配固定池 | 资源数量已知 | 高效但不灵活 |
| 动态 Descriptor Pool | 资源数量不确定 | 均衡方案 |
| Bindless (Descriptor Indexing) | 大量纹理/缓冲 | 现代首选 |

### 4.2 Bindless 资源绑定（Descriptor Indexing）

Vulkan 1.2 的核心特性，允许在着色器中通过索引动态访问任意资源：

```cpp
// 创建一个超大 Descriptor Set，包含所有纹理
VkDescriptorSetLayoutBinding binding{};
binding.binding = 0;
binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
binding.descriptorCount = 100000;  // 最多 10 万个纹理
binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

// 启用 Descriptor Indexing 特性
VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
VkDescriptorBindingFlags flags =
    VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
    VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
    VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
flagsInfo.bindingCount = 1;
flagsInfo.pBindingFlags = &flags;
```

着色器中通过 Push Constant 传入索引：

```glsl
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 0) uniform sampler2D textures[];
layout(push_constant) uniform PushConstants {
    uint textureIndex;
};

void main() {
    fragColor = texture(textures[nonuniformEXT(textureIndex)], uv);
}
```

### 4.3 Push Constant

小量数据（128-256 字节，取决于硬件）可通过 Push Constant 直接嵌入命令流，无需 Descriptor：

```cpp
struct PushData {
    float4x4 mvp;     // 64 bytes
    uint textureIdx;   // 4 bytes
    float opacity;     // 4 bytes
};  // 总计 72 bytes

VkPushConstantRange pushRange{};
pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
pushRange.offset = 0;
pushRange.size = sizeof(PushData);

// 录制命令时
PushData data{mvp, texIdx, 1.0f};
vkCmdPushConstants(cmd, pipelineLayout,
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    0, sizeof(data), &data);
```

**最佳实践：** 每个 Draw Call 变化的小数据（MVP 矩阵、材质索引）用 Push Constant；共享数据用 Uniform Buffer；大量资源用 Bindless Descriptor。

---

## 5. 多线程渲染

### 5.1 多线程命令录制

Vulkan 天然支持多线程，关键规则：

- 每个线程一个 **VkCommandPool**（CommandPool 不是线程安全的）
- 各线程录制 **Secondary CommandBuffer**
- 主线程的 Primary CommandBuffer 通过 `vkCmdExecuteCommands` 执行所有 Secondary

```
Thread 0 (Main):     Primary CommandBuffer
                       ├── BeginRenderPass
                       ├── ExecuteCommands(secondary[0])  ← Thread 1 录制
                       ├── ExecuteCommands(secondary[1])  ← Thread 2 录制
                       ├── ExecuteCommands(secondary[2])  ← Thread 3 录制
                       └── EndRenderPass

Thread 1:  Pool1 → Secondary[0] → 录制场景前 1/3 的绘制命令
Thread 2:  Pool2 → Secondary[1] → 录制场景中 1/3 的绘制命令
Thread 3:  Pool3 → Secondary[2] → 录制场景后 1/3 的绘制命令
```

```cpp
// Secondary CommandBuffer 录制
VkCommandBufferInheritanceInfo inheritance{};
inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
inheritance.renderPass = renderPass;
inheritance.subpass = 0;
inheritance.framebuffer = framebuffer;

VkCommandBufferBeginInfo beginInfo{};
beginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
beginInfo.pInheritanceInfo = &inheritance;

vkBeginCommandBuffer(secondary, &beginInfo);
vkCmdBindPipeline(secondary, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
// ... 绘制命令 ...
vkEndCommandBuffer(secondary);
```

### 5.2 异步 Compute

利用独立的 Compute Queue 与 Graphics Queue **并行执行**：

```
Frame N:
  Graphics Queue: ████ 渲染 ████████████████████
  Compute Queue:  ██ 粒子模拟 ██  ██ 剔除 ██

Frame N+1:
  Graphics Queue: ████ 渲染 ████████████████████
  Compute Queue:  ██ 粒子模拟 ██  ██ 剔除 ██
```

跨队列同步使用 Semaphore：

```cpp
// Compute Queue 完成后 signal
VkSubmitInfo computeSubmit{};
computeSubmit.signalSemaphoreCount = 1;
computeSubmit.pSignalSemaphores = &computeDoneSem;
vkQueueSubmit(computeQueue, 1, &computeSubmit, VK_NULL_HANDLE);

// Graphics Queue 等待 Compute 完成后再使用结果
VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
VkSubmitInfo graphicsSubmit{};
graphicsSubmit.waitSemaphoreCount = 1;
graphicsSubmit.pWaitSemaphores = &computeDoneSem;
graphicsSubmit.pWaitDstStageMask = &waitStage;
vkQueueSubmit(graphicsQueue, 1, &graphicsSubmit, VK_NULL_HANDLE);
```

### 5.3 异步 Transfer

专用 Transfer Queue 可在不阻塞渲染的情况下上传纹理：

```
Graphics Queue: ████ 渲染帧 1 ████  ████ 渲染帧 2 ████
Transfer Queue: ████ 上传纹理 A ████████ 上传纹理 B ████
                                    │
                          Queue Ownership Transfer
                          (通过 Barrier 的 srcQueueFamily/dstQueueFamily)
```

跨队列资源传递需要 **Queue Family Ownership Transfer**：

```cpp
// Transfer Queue 端：释放所有权
VkImageMemoryBarrier releaseBarrier{};
releaseBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
releaseBarrier.dstAccessMask = 0;
releaseBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
releaseBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
releaseBarrier.srcQueueFamilyIndex = transferFamily;
releaseBarrier.dstQueueFamilyIndex = graphicsFamily;

// Graphics Queue 端：获取所有权
VkImageMemoryBarrier acquireBarrier{};
acquireBarrier.srcAccessMask = 0;
acquireBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
acquireBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
acquireBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
acquireBarrier.srcQueueFamilyIndex = transferFamily;
acquireBarrier.dstQueueFamilyIndex = graphicsFamily;
```

---

## 6. Pipeline 优化

### 6.1 Pipeline Cache

管线编译（着色器编译 + 链接）是 Vulkan 最昂贵的 CPU 操作之一。Pipeline Cache 可跨运行持久化编译结果：

```cpp
// 从磁盘加载缓存
std::vector<uint8_t> cacheData = loadFromDisk("pipeline_cache.bin");
VkPipelineCacheCreateInfo cacheInfo{};
cacheInfo.initialDataSize = cacheData.size();
cacheInfo.pInitialData = cacheData.data();
VkPipelineCache cache;
vkCreatePipelineCache(device, &cacheInfo, nullptr, &cache);

// 创建管线时传入缓存
vkCreateGraphicsPipelines(device, cache, 1, &pipelineInfo, nullptr, &pipeline);

// 程序退出前保存缓存到磁盘
size_t cacheSize;
vkGetPipelineCacheData(device, cache, &cacheSize, nullptr);
std::vector<uint8_t> data(cacheSize);
vkGetPipelineCacheData(device, cache, &cacheSize, data.data());
saveToDisk("pipeline_cache.bin", data);
```

### 6.2 Pipeline Derivative

从已有管线派生新管线，驱动可以复用部分编译结果：

```cpp
VkGraphicsPipelineCreateInfo baseInfo{};
baseInfo.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
vkCreateGraphicsPipelines(device, cache, 1, &baseInfo, nullptr, &basePipeline);

VkGraphicsPipelineCreateInfo derivedInfo = baseInfo;
derivedInfo.flags = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
derivedInfo.basePipelineHandle = basePipeline;
// 修改少量状态（如不同的混合模式）
vkCreateGraphicsPipelines(device, cache, 1, &derivedInfo, nullptr, &derivedPipeline);
```

### 6.3 Dynamic State 扩展

减少管线变体数量，将更多状态设为动态：

```cpp
// Vulkan 1.3 Extended Dynamic State
VkDynamicState dynamicStates[] = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
    VK_DYNAMIC_STATE_CULL_MODE,           // 1.3
    VK_DYNAMIC_STATE_FRONT_FACE,          // 1.3
    VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,  // 1.3
    VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,   // 1.3
    VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,  // 1.3
    VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,    // 1.3
    VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, // 1.3
};

// 录制时动态设置
vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);
vkCmdSetDepthTestEnable(cmd, VK_TRUE);
vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_LESS);
```

**效果：** 一条管线覆盖多种绘制场景，大幅减少管线对象数量和切换开销。

### 6.4 着色器编译优化

```
                GLSL / HLSL
                    │
            glslc / DXC 离线编译
                    │
                 SPIR-V
                    │
    ┌───────────────┼───────────────┐
    ▼               ▼               ▼
Pipeline Cache   Pipeline Library   Shader Object
(运行时缓存)     (预编译库)          (Vulkan 1.3 ext)
```

**SPIR-V 优化建议：**
- 使用 `spirv-opt` 对 SPIR-V 做优化（死代码消除、常量折叠等）
- Specialization Constant 替代 `#ifdef`，减少着色器变体数量
- 运行时通过 `VkSpecializationInfo` 注入不同常量值

```cpp
// Specialization Constant
uint32_t maxLights = 8;
VkSpecializationMapEntry entry{};
entry.constantID = 0;
entry.offset = 0;
entry.size = sizeof(uint32_t);

VkSpecializationInfo specInfo{};
specInfo.mapEntryCount = 1;
specInfo.pMapEntries = &entry;
specInfo.dataSize = sizeof(uint32_t);
specInfo.pData = &maxLights;

shaderStage.pSpecializationInfo = &specInfo;
```

---

## 7. 高级渲染技术

### 7.1 Indirect Draw

CPU 不决定绘制参数，由 GPU 填充 Indirect Buffer（实现 GPU-Driven Rendering）：

```cpp
// Indirect Buffer 结构
struct VkDrawIndexedIndirectCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;
};

// Compute Shader 填充 Indirect Buffer（视锥剔除、LOD 选择等）
// ...

// 一次调用绘制所有可见物体
vkCmdDrawIndexedIndirect(cmd, indirectBuffer, 0,
                         maxDrawCount, sizeof(VkDrawIndexedIndirectCommand));

// 带计数版本（GPU 决定实际绘制数量）
vkCmdDrawIndexedIndirectCount(cmd, indirectBuffer, 0,
                              countBuffer, 0, maxDrawCount,
                              sizeof(VkDrawIndexedIndirectCommand));
```

### 7.2 多重 Draw Call 合并（Multi-Draw Indirect）

将数百个 Draw Call 合并为一次 `vkCmdDrawIndexedIndirect`：

```
传统方式：
  for each object:
    vkCmdBindDescriptorSets(...)     // CPU 开销
    vkCmdDrawIndexed(...)            // CPU 开销

GPU-Driven 方式：
  Compute: 视锥剔除 → 填充 Indirect Buffer + Count Buffer
  Render:  vkCmdDrawIndexedIndirectCount(...)  // 一次调用
```

### 7.3 实例化渲染（Instancing）

```cpp
// 实例数据通过 Vertex Buffer 传入
VkVertexInputBindingDescription instanceBinding{};
instanceBinding.binding = 1;
instanceBinding.stride = sizeof(InstanceData);
instanceBinding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

// 每个实例的变换矩阵、材质索引等
struct InstanceData {
    float4x4 modelMatrix;
    uint32_t materialIndex;
};

vkCmdBindVertexBuffers(cmd, 1, 1, &instanceBuffer, &offset);
vkCmdDrawIndexed(cmd, indexCount, instanceCount, 0, 0, 0);
```

### 7.4 遮挡查询（Occlusion Query）

```cpp
VkQueryPool queryPool;
VkQueryPoolCreateInfo queryInfo{};
queryInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
queryInfo.queryCount = objectCount;
vkCreateQueryPool(device, &queryInfo, nullptr, &queryPool);

// 渲染时
vkCmdBeginQuery(cmd, queryPool, objectIndex, 0);
vkCmdDrawIndexed(cmd, ...);  // 绘制包围盒
vkCmdEndQuery(cmd, queryPool, objectIndex);

// 读取结果
uint64_t results[objectCount];
vkGetQueryPoolResults(device, queryPool, 0, objectCount,
                      sizeof(results), results, sizeof(uint64_t),
                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
```

---

## 8. 移动端优化（Tile-Based GPU）

### 8.1 Tile-Based 架构特点

Qualcomm Adreno、ARM Mali、Apple GPU 都使用 Tile-Based (Deferred) Rendering：

```
传统 Immediate Mode GPU:
  每个三角形 → 光栅化 → 写入 Framebuffer (显存)

Tile-Based GPU:
  所有三角形 → Binning (分配到 Tile) → 每个 Tile 在片上 Tile Memory 中完成渲染 → 写回显存
```

### 8.2 Tile-Based 优化原则

| 原则 | 做法 | 原因 |
|------|------|------|
| 正确设置 Load/Store Op | 尽可能用 `DONT_CARE` | 避免不必要的 Tile ↔ 显存搬运 |
| 使用 `LAZILY_ALLOCATED` 内存 | 深度/MSAA 等临时附件 | 数据始终在 Tile Memory，不分配显存 |
| 使用 Subpass 的 Input Attachment | 延迟渲染的 G-Buffer 读取 | 在 Tile Memory 内直接读取，零带宽开销 |
| 避免中途读回 Framebuffer | 不要在 Render Pass 内插入不必要的 Barrier | 会强制 Tile 回写 |
| 合并 Render Pass | 能放一个 Pass 的不拆成多个 | 每个 Pass 都有一次完整的 Tile 加载/存储 |

```cpp
// 深度附件使用 LAZILY_ALLOCATED（Tile-Based GPU 不需要实际显存）
VkImageCreateInfo depthInfo{};
depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                  VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

VmaAllocationCreateInfo allocInfo{};
allocInfo.usage = VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED;  // 0 显存占用
```

### 8.3 Render Pass 设置示例（移动端最佳实践）

```cpp
// 颜色附件：Clear + Store（需要呈现）
colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

// 深度附件：Clear + DontCare（渲染后不需要）
depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

// MSAA 附件：DontCare + DontCare（Resolve 后不需要）
msaaAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
msaaAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
```

---

## 9. 调试与验证

### 9.1 Validation Layer

**开发阶段必须开启**，能捕获 99% 的 API 误用：

```cpp
const char* validationLayers[] = {"VK_LAYER_KHRONOS_validation"};

VkInstanceCreateInfo createInfo{};
createInfo.enabledLayerCount = 1;
createInfo.ppEnabledLayerNames = validationLayers;

// 配置 Debug Messenger 接收验证消息
VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
debugInfo.messageSeverity =
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
debugInfo.messageType =
    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
debugInfo.pfnUserCallback = debugCallback;
```

### 9.2 GPU-Assisted Validation

检测着色器中的越界访问等运行时错误：

```cpp
VkValidationFeaturesEXT features{};
VkValidationFeatureEnableEXT enables[] = {
    VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
    VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
    VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
};
features.enabledValidationFeatureCount = 3;
features.pEnabledValidationFeatures = enables;
```

### 9.3 Debug Marker

为 GPU 对象和命令区域添加名称，方便在 RenderDoc/Nsight 等工具中定位：

```cpp
// 命名对象
VkDebugUtilsObjectNameInfoEXT nameInfo{};
nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
nameInfo.objectHandle = (uint64_t)image;
nameInfo.pObjectName = "ShadowMap";
vkSetDebugUtilsObjectNameEXT(device, &nameInfo);

// 标记命令区域
VkDebugUtilsLabelEXT label{};
label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
label.pLabelName = "Shadow Pass";
label.color[0] = 1.0f;  // 红色标记
vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
// ... 渲染阴影 ...
vkCmdEndDebugUtilsLabelEXT(cmd);
```

### 9.4 常用调试工具

| 工具 | 平台 | 功能 |
|------|------|------|
| **RenderDoc** | Windows/Linux/Android | 帧捕获、Draw Call 分析、资源查看 |
| **Nsight Graphics** | Windows/Linux | NVIDIA GPU 深度分析 |
| **AGI (Android GPU Inspector)** | Android | Qualcomm/Mali GPU 分析 |
| **Vulkan Configurator** | 全平台 | Validation Layer 配置 |

---

## 10. Vulkan 1.3 核心特性汇总

Vulkan 1.3（2022）将多个重要扩展提升为核心，简化了现代 Vulkan 开发：

| 特性 | 原扩展 | 作用 |
|------|--------|------|
| Dynamic Rendering | `VK_KHR_dynamic_rendering` | 消除 RenderPass/Framebuffer 预创建 |
| Synchronization2 | `VK_KHR_synchronization2` | 简化同步 API |
| Extended Dynamic State | `VK_EXT_extended_dynamic_state` | 更多管线状态可运行时设置 |
| Maintenance4 | `VK_KHR_maintenance4` | 允许用 pNext 查询特性/属性 |
| Inline Uniform Block | `VK_EXT_inline_uniform_block` | Descriptor 中内联小型 Uniform |
| Subgroup Size Control | `VK_EXT_subgroup_size_control` | 控制 Compute 子组大小 |
| Shader Integer Dot Product | `VK_KHR_shader_integer_dot_product` | 机器学习推理优化 |
| Zero-Initialize Workgroup Memory | `VK_KHR_zero_initialize_workgroup_memory` | 自动清零共享内存 |

---

## 11. 推荐架构模式

### 11.1 帧资源管理（Triple Buffering）

```
Frame 0: [Recording]  ← CPU 正在录制
Frame 1: [Submitted]  ← GPU 正在执行
Frame 2: [Presented]  ← 正在显示

每帧独立的资源集：
  - CommandPool + CommandBuffer
  - Uniform Buffer（或 Dynamic Uniform Buffer 的偏移）
  - Descriptor Set
  - Fence（CPU 等待此帧的 GPU 完成）
  - Semaphore（imageAvailable / renderFinished）
```

```cpp
struct FrameData {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkFence inFlightFence;
    VkSemaphore imageAvailableSem;
    VkSemaphore renderFinishedSem;
    VkBuffer uniformBuffer;
    VkDescriptorSet descriptorSet;
};

FrameData frames[MAX_FRAMES_IN_FLIGHT];  // 通常 2-3

void drawFrame() {
    auto& frame = frames[currentFrame];

    // 等待此帧的 GPU 工作完成
    vkWaitForFences(device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &frame.inFlightFence);

    // 获取 swapchain 图像
    uint32_t imageIndex;
    vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                          frame.imageAvailableSem, VK_NULL_HANDLE, &imageIndex);

    // 更新 Uniform Buffer
    updateUniformBuffer(frame.uniformBuffer);

    // 录制命令
    vkResetCommandPool(device, frame.commandPool, 0);
    recordCommands(frame.commandBuffer, imageIndex);

    // 提交
    VkSubmitInfo submit{};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &frame.imageAvailableSem;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.commandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &frame.renderFinishedSem;
    vkQueueSubmit(graphicsQueue, 1, &submit, frame.inFlightFence);

    // 呈现
    VkPresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frame.renderFinishedSem;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    vkQueuePresentKHR(presentQueue, &presentInfo);

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}
```

### 11.2 资源生命周期

```
创建资源 → 使用中（可能多帧引用）→ 标记删除 → 等待 GPU 不再引用 → 真正销毁

关键原则：
  - 资源被命令引用期间不能销毁
  - 使用"删除队列"：标记删除时放入队列，N 帧后（GPU 确认完成后）真正释放
  - 或者用 Fence 精确判断
```

```cpp
struct DeletionQueue {
    std::deque<std::pair<uint64_t, std::function<void()>>> deletors;

    void push(uint64_t frameNumber, std::function<void()>&& deletor) {
        deletors.push_back({frameNumber, std::move(deletor)});
    }

    void flush(uint64_t completedFrame) {
        while (!deletors.empty() && deletors.front().first <= completedFrame) {
            deletors.front().second();
            deletors.pop_front();
        }
    }
};
```
