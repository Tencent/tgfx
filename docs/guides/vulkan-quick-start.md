# Vulkan 快速入门

## 1. Vulkan 是什么

Vulkan 是 Khronos Group 推出的跨平台低级别图形与计算 API，相比 OpenGL 提供更细粒度的 GPU 控制、更低的驱动开销和更好的多线程支持。

**核心设计理念：** 把驱动层做的事情交给开发者，换取更高的性能上限和更可预测的行为。

---

## 2. 核心架构概览

```
Application
    │
    ▼
VkInstance ──── VkPhysicalDevice ──── VkDevice
                                        │
                          ┌─────────────┼─────────────┐
                          ▼             ▼             ▼
                     VkQueue      VkSwapchain    VkPipeline
                       │              │              │
                  CommandBuffer   VkImage      ShaderModule
                       │          VkImageView   PipelineLayout
                  RenderPass      Framebuffer   DescriptorSet
```

---

## 3. 关键对象与概念

### 3.1 Instance 与 Physical Device

- **VkInstance**：Vulkan 运行时入口，加载验证层（Validation Layers）和扩展（Extensions）。
- **VkPhysicalDevice**：代表一块物理 GPU。通过它查询设备能力（显存大小、支持的格式、队列族等）。

```cpp
VkInstance instance;
VkInstanceCreateInfo createInfo{};
createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
vkCreateInstance(&createInfo, nullptr, &instance);

uint32_t deviceCount = 0;
vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
std::vector<VkPhysicalDevice> devices(deviceCount);
vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
```

### 3.2 Logical Device 与 Queue

- **VkDevice**：逻辑设备，是与 GPU 交互的主要接口，所有资源都从它创建。
- **VkQueue**：命令提交通道。GPU 有多个**队列族（Queue Family）**，各自支持不同操作类型：
  - **Graphics Queue**：绘制命令
  - **Compute Queue**：计算着色器
  - **Transfer Queue**：数据搬运（拷贝、blit）

```cpp
// 查询队列族
uint32_t queueFamilyCount = 0;
vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, families.data());

// 找到支持 Graphics 的队列族
for (uint32_t i = 0; i < queueFamilyCount; i++) {
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        graphicsFamily = i;
        break;
    }
}
```

### 3.3 Command Buffer 与 Command Pool

Vulkan 中所有 GPU 操作都通过 **Command Buffer** 录制，然后一次性提交给 Queue 执行。这是与 OpenGL 最大的区别之一：

- **VkCommandPool**：线程级的命令分配器，每个线程应有自己的 Pool。
- **VkCommandBuffer**：从 Pool 分配，录制一系列 GPU 命令。

```cpp
// 创建命令池
VkCommandPoolCreateInfo poolInfo{};
poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
poolInfo.queueFamilyIndex = graphicsFamily;
poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);

// 分配命令缓冲
VkCommandBufferAllocateInfo allocInfo{};
allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
allocInfo.commandPool = commandPool;
allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
allocInfo.commandBufferCount = 1;
vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

// 录制命令
vkBeginCommandBuffer(commandBuffer, &beginInfo);
// ... 录制绘制、拷贝等命令 ...
vkEndCommandBuffer(commandBuffer);

// 提交
VkSubmitInfo submitInfo{};
submitInfo.commandBufferCount = 1;
submitInfo.pCommandBuffers = &commandBuffer;
vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence);
```

### 3.4 Render Pass 与 Framebuffer

Render Pass 定义了一次渲染流程中的**附件（Attachment）** 和**子通道（Subpass）** 结构。这是 Vulkan 独有的概念，让驱动能提前知道渲染结构从而做出优化（尤其在 Tile-Based GPU 上）。

- **Attachment**：颜色、深度、模板等缓冲区的描述（格式、采样数、load/store 操作）。
- **Subpass**：Render Pass 内的一个阶段，可以引用不同的 Attachment。
- **VkFramebuffer**：将具体的 VkImageView 绑定到 Render Pass 的 Attachment 上。

```cpp
// 定义颜色附件
VkAttachmentDescription colorAttachment{};
colorAttachment.format = swapChainFormat;
colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;     // 渲染前清空
colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;    // 渲染后保存
colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

// 子通道引用
VkAttachmentReference colorRef{};
colorRef.attachment = 0;
colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

VkSubpassDescription subpass{};
subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
subpass.colorAttachmentCount = 1;
subpass.pColorAttachments = &colorRef;

// 创建 Render Pass
VkRenderPassCreateInfo renderPassInfo{};
renderPassInfo.attachmentCount = 1;
renderPassInfo.pAttachments = &colorAttachment;
renderPassInfo.subpassCount = 1;
renderPassInfo.pSubpasses = &subpass;
vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);
```

### 3.5 Graphics Pipeline

Vulkan 的管线是**完全预编译**的不可变对象，所有状态在创建时就确定。这消除了 OpenGL 中运行时状态切换的开销。

管线包含以下阶段：

| 阶段 | 对象 | 说明 |
|------|------|------|
| 着色器阶段 | VkShaderModule | 顶点、片元等着色器（SPIR-V 字节码） |
| 顶点输入 | VertexInputState | 顶点属性布局 |
| 输入装配 | InputAssemblyState | 图元类型（三角形、线等） |
| 视口/裁剪 | ViewportState | 视口和裁剪矩形 |
| 光栅化 | RasterizationState | 面剔除、多边形模式、线宽 |
| 多重采样 | MultisampleState | MSAA 配置 |
| 深度/模板 | DepthStencilState | 深度测试、模板测试 |
| 颜色混合 | ColorBlendState | 混合方程 |
| 动态状态 | DynamicState | 可运行时修改的状态（视口、裁剪等） |
| 管线布局 | PipelineLayout | Uniform 和 Push Constant 的布局 |

```cpp
VkGraphicsPipelineCreateInfo pipelineInfo{};
pipelineInfo.stageCount = 2;
pipelineInfo.pStages = shaderStages;          // 顶点 + 片元着色器
pipelineInfo.pVertexInputState = &vertexInput;
pipelineInfo.pInputAssemblyState = &inputAssembly;
pipelineInfo.pViewportState = &viewportState;
pipelineInfo.pRasterizationState = &rasterizer;
pipelineInfo.pMultisampleState = &multisampling;
pipelineInfo.pColorBlendState = &colorBlending;
pipelineInfo.pDynamicState = &dynamicState;
pipelineInfo.layout = pipelineLayout;
pipelineInfo.renderPass = renderPass;
pipelineInfo.subpass = 0;
vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
```

### 3.6 Descriptor Set 与资源绑定

Descriptor 是 Vulkan 中着色器访问资源（Uniform Buffer、纹理、存储缓冲等）的机制。

- **VkDescriptorSetLayout**：描述着色器需要哪些资源绑定。
- **VkDescriptorPool**：分配 Descriptor Set 的池。
- **VkDescriptorSet**：一组绑定好的资源引用，渲染时绑定到管线。

```
PipelineLayout
    └── DescriptorSetLayout[]
            └── Binding 0: Uniform Buffer (Vertex Stage)
            └── Binding 1: Combined Image Sampler (Fragment Stage)
```

```cpp
// 定义布局
VkDescriptorSetLayoutBinding uboBinding{};
uboBinding.binding = 0;
uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
uboBinding.descriptorCount = 1;

VkDescriptorSetLayoutBinding samplerBinding{};
samplerBinding.binding = 1;
samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
samplerBinding.descriptorCount = 1;

// 更新描述符集
VkWriteDescriptorSet writes[2];
writes[0].dstBinding = 0;
writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
writes[0].pBufferInfo = &bufferInfo;
writes[1].dstBinding = 1;
writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
writes[1].pImageInfo = &imageInfo;
vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
```

### 3.7 Synchronization（同步机制）

Vulkan 把同步完全交给开发者管理，这是最复杂也最关键的部分：

| 同步原语 | 用途 | 作用域 |
|----------|------|--------|
| **VkFence** | CPU 等待 GPU 完成 | CPU ↔ GPU |
| **VkSemaphore** | Queue 之间的执行顺序 | GPU ↔ GPU |
| **VkEvent** | 更细粒度的命令间同步 | Command Buffer 内 |
| **Pipeline Barrier** | 资源状态转换和内存可见性 | Command Buffer 内 |

```cpp
// Fence：CPU 等 GPU 完成
VkFence fence;
vkCreateFence(device, &fenceInfo, nullptr, &fence);
vkQueueSubmit(queue, 1, &submitInfo, fence);
vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

// Semaphore：帧间同步
// imageAvailable: swapchain 图像就绪后通知渲染开始
// renderFinished: 渲染完成后通知 present
VkSemaphore imageAvailable, renderFinished;
submitInfo.waitSemaphoreCount = 1;
submitInfo.pWaitSemaphores = &imageAvailable;
submitInfo.signalSemaphoreCount = 1;
submitInfo.pSignalSemaphores = &renderFinished;
```

### 3.8 Image Layout 与 Memory Management

**Image Layout** 是 Vulkan 独有的概念。同一个 VkImage 在不同用途下需要不同的内存布局，切换布局需要通过 Pipeline Barrier：

| Layout | 用途 |
|--------|------|
| `UNDEFINED` | 初始状态，内容不保留 |
| `COLOR_ATTACHMENT_OPTIMAL` | 作为渲染目标 |
| `SHADER_READ_ONLY_OPTIMAL` | 作为着色器采样纹理 |
| `TRANSFER_SRC_OPTIMAL` | 作为拷贝源 |
| `TRANSFER_DST_OPTIMAL` | 作为拷贝目标 |
| `PRESENT_SRC_KHR` | 用于屏幕呈现 |

**内存管理**：Vulkan 中 Buffer 和 Image 创建后不自带显存，需要手动分配和绑定：

```cpp
// 1. 创建 Buffer
vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);

// 2. 查询内存需求
VkMemoryRequirements memReq;
vkGetBufferMemoryRequirements(device, buffer, &memReq);

// 3. 分配显存
VkMemoryAllocateInfo allocInfo{};
allocInfo.allocationSize = memReq.size;
allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);
vkAllocateMemory(device, &allocInfo, nullptr, &memory);

// 4. 绑定
vkBindBufferMemory(device, buffer, memory, 0);
```

### 3.9 Swapchain（交换链）

Swapchain 管理与窗口系统交互的一组可呈现图像：

```
Swapchain
    ├── Image 0 ──→ ImageView 0 ──→ Framebuffer 0
    ├── Image 1 ──→ ImageView 1 ──→ Framebuffer 1
    └── Image 2 ──→ ImageView 2 ──→ Framebuffer 2

帧循环：
  AcquireNextImage → 录制命令 → Submit → Present
       ↑                                    │
       └────────────────────────────────────┘
```

### 3.10 SPIR-V 着色器

Vulkan 不接受 GLSL 文本，使用 **SPIR-V** 中间字节码格式。通常用 `glslc` 或 `glslangValidator` 离线编译：

```bash
glslc shader.vert -o vert.spv
glslc shader.frag -o frag.spv
```

---

## 4. 一帧的完整流程

```
1. vkAcquireNextImageKHR()        // 获取 swapchain 中下一张可用图像
2. vkResetCommandBuffer()          // 重置命令缓冲
3. vkBeginCommandBuffer()          // 开始录制
4.   vkCmdBeginRenderPass()        // 开始渲染通道
5.     vkCmdBindPipeline()         // 绑定管线
6.     vkCmdBindDescriptorSets()   // 绑定资源
7.     vkCmdBindVertexBuffers()    // 绑定顶点数据
8.     vkCmdDraw()                 // 绘制
9.   vkCmdEndRenderPass()          // 结束渲染通道
10. vkEndCommandBuffer()           // 结束录制
11. vkQueueSubmit()                // 提交到 GPU 执行
12. vkQueuePresentKHR()            // 呈现到屏幕
```

---

## 5. 与 OpenGL 的关键差异

| 维度 | OpenGL | Vulkan |
|------|--------|--------|
| 驱动开销 | 驱动做大量隐式工作 | 开发者显式控制一切 |
| 多线程 | 单上下文，难以多线程 | 多 CommandPool 天然多线程 |
| 管线状态 | 运行时可变全局状态机 | 预编译不可变 Pipeline 对象 |
| 内存管理 | 驱动自动管理 | 开发者手动分配绑定 |
| 同步 | 驱动隐式处理 | Fence/Semaphore/Barrier 显式管理 |
| 着色器 | GLSL 文本运行时编译 | SPIR-V 字节码离线编译 |
| 错误检查 | 默认开启 `glGetError` | 通过 Validation Layer 可选开启 |
| Render Pass | 无显式概念 | 必须定义，驱动据此优化 |

---

## 6. 与 TGFX RHI 的对应关系

TGFX 的 GPU 抽象层参照了现代 GPU API 设计，如果要实现 Vulkan 后端，对应关系如下：

| TGFX 抽象 | Vulkan 对应 |
|-----------|-------------|
| `GPU` | `VkDevice` + `VkPhysicalDevice` |
| `Texture` | `VkImage` + `VkImageView` + `VkDeviceMemory` |
| `GPUBuffer` | `VkBuffer` + `VkDeviceMemory` |
| `Sampler` | `VkSampler` |
| `ShaderModule` | `VkShaderModule` (SPIR-V) |
| `RenderPipeline` | `VkPipeline` + `VkPipelineLayout` |
| `RenderPass` | `VkRenderPass` + `VkFramebuffer` |
| `CommandEncoder` | `VkCommandBuffer` (recording) |
| `CommandBuffer` | `VkCommandBuffer` (recorded) |
| `CommandQueue` | `VkQueue` |

---

## 7. 推荐学习资源

- [Vulkan Tutorial](https://vulkan-tutorial.com/) — 最经典的入门教程
- [Vulkan Specification](https://registry.khronos.org/vulkan/specs/1.3/html/) — 官方规范
- [Vulkan Samples (Khronos)](https://github.com/KhronosGroup/Vulkan-Samples) — 官方示例集
- [vkguide.dev](https://vkguide.dev/) — 现代 Vulkan 实践指南
- [Sascha Willems Samples](https://github.com/SaschaWillems/Vulkan) — 丰富的功能示例
