# Android Vulkan 接入技术方案

## 1. 概述

本文档描述在 tgfx 引擎中接入 Android Vulkan 后端的技术方案。目标是使 tgfx 在 Android 平台上可选择使用 Vulkan API 进行 GPU 渲染，与现有 OpenGL ES/EGL 路径并存，并最终支持 AHardwareBuffer 互操作、视频纹理导入、设备旋转等 Android 特有场景。

### 1.1 背景

tgfx 当前的 Vulkan 后端支持 Windows（原生 GPU）和 macOS（通过 SwiftShader 软件渲染用于 CI）。Android 是 Vulkan 的最大移动端部署平台——自 Android 7.0 (API 24) 起提供 Vulkan 1.0 支持，Android 10 (API 29) 起要求设备支持 Vulkan 1.1。接入 Android Vulkan 可带来以下收益：

- 减少 Draw Call 开销（Vulkan 命令缓冲可预构建）
- 显式内存管理，减少驱动隐式分配和碎片
- AHardwareBuffer 零拷贝导入（视频解码/Camera 帧直接作为纹理）
- 为未来 Compute Shader 文本光栅化、粒子系统等高级管线奠定基础

### 1.2 当前就绪度

| 模块 | 状态 | 说明 |
|------|------|------|
| `VulkanAPI.h` 平台宏 | ✅ | 已定义 `VK_USE_PLATFORM_ANDROID_KHR` |
| `VulkanGPU::createInstance()` | ✅ | 已启用 `VK_KHR_android_surface` 扩展 |
| `CMakeLists.txt` VK 宏 | ✅ | Android 路径已添加 `VK_USE_PLATFORM_ANDROID_KHR` define |
| VulkanDevice 核心初始化 | ✅ | 平台无关，可直接复用 |
| VulkanWindow（Surface/Swapchain） | ❌ | 仅有 Win32 (HWND) 实现 |
| CMake 平台门控 | ❌ | 当前 FATAL_ERROR 阻止 Android 启用 Vulkan |
| AHardwareBuffer 导入 | ❌ | 未实现 |
| Android CI | ❌ | 无 Android Vulkan 测试环境 |

---

## 2. 架构设计

### 2.1 整体分层

```
┌──────────────────────────────────────────────────────────────┐
│                      Application Layer                        │
│   Kotlin/Java Activity → SurfaceView/TextureView             │
│   ANativeWindow* 传递到 JNI                                   │
└────────────────────────────────┬─────────────────────────────┘
                                 │ JNI
┌────────────────────────────────┴─────────────────────────────┐
│                      tgfx Public API                          │
│   VulkanDevice::Make() → VulkanWindow::MakeFrom(ANativeWindow)│
│   Canvas / Paint / TextBlob / Layers ...                      │
└────────────────────────────────┬─────────────────────────────┘
                                 │
┌────────────────────────────────┴─────────────────────────────┐
│                  Vulkan Backend (src/gpu/vulkan/)              │
│                                                               │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────────────┐ │
│  │ VulkanGPU   │  │VulkanWindow  │  │VulkanCommandEncoder │ │
│  │ (Instance/  │  │(Surface/     │  │(录制渲染命令)        │ │
│  │  Device/    │  │ Swapchain/   │  └─────────────────────┘ │
│  │  Queue)     │  │ Present)     │                           │
│  └─────────────┘  └──────────────┘                           │
│                                                               │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────────┐  │
│  │VulkanTexture │  │VulkanBuffer  │  │VulkanRenderPass   │  │
│  │(含 AHB 导入)│  │(Vertex/UBO)  │  │(+Pipeline 管理)   │  │
│  └──────────────┘  └──────────────┘  └───────────────────┘  │
└──────────────────────────────────────────────────────────────┘
                                 │
┌────────────────────────────────┴─────────────────────────────┐
│                   Android Platform Layer                       │
│   libvulkan.so (系统 Vulkan Loader)                           │
│   GPU Driver (Adreno / Mali / PowerVR / Xclipse)              │
└──────────────────────────────────────────────────────────────┘
```

### 2.2 后端选择策略

Android 应用启动时按以下优先级选择渲染后端：

```
1. 应用显式指定 TGFX_USE_VULKAN → 使用 Vulkan
2. 运行时检测 Vulkan 可用性：
   a. dlopen("libvulkan.so") 成功
   b. vkEnumerateInstanceVersion() ≥ 1.1
   c. 设备不在黑名单中
   d. 物理设备支持所需特性（graphics queue, swapchain）
   → 满足则使用 Vulkan
3. 回退到 OpenGL ES 3.x / EGL
```

---

## 3. 详细实现方案

### 3.1 CMake 平台限制解除

**文件**：`CMakeLists.txt` line 94-97

**当前代码**：
```cmake
if (TGFX_USE_VULKAN)
    if (NOT WIN32 AND NOT APPLE)
        message(FATAL_ERROR "TGFX_USE_VULKAN is currently supported on Windows and macOS only.")
    endif ()
```

**修改为**：
```cmake
if (TGFX_USE_VULKAN)
    if (NOT WIN32 AND NOT APPLE AND NOT ANDROID)
        message(FATAL_ERROR "TGFX_USE_VULKAN is currently supported on Windows, macOS, and Android only.")
    endif ()
```

**附加 CMake 改动**：确认 Android 路径下 volk 正常编译（volk 已支持 Android `dlopen`）。

### 3.2 VulkanWindow Android 实现

#### 3.2.1 头文件接口扩展

**文件**：`include/tgfx/gpu/vulkan/VulkanWindow.h`

```cpp
#ifdef __ANDROID__
#include <android/native_window.h>

  /// Creates a VulkanWindow from an Android ANativeWindow.
  /// The ANativeWindow must remain valid for the lifetime of the returned VulkanWindow.
  static std::shared_ptr<VulkanWindow> MakeFrom(ANativeWindow* window,
                                                std::shared_ptr<VulkanDevice> device,
                                                std::shared_ptr<ColorSpace> colorSpace = nullptr);
#endif
```

#### 3.2.2 Surface 创建

**文件**：`src/gpu/vulkan/VulkanWindow.cpp`

```cpp
#ifdef __ANDROID__
std::shared_ptr<VulkanWindow> VulkanWindow::MakeFrom(ANativeWindow* window,
                                                     std::shared_ptr<VulkanDevice> device,
                                                     std::shared_ptr<ColorSpace> colorSpace) {
  if (!window || !device) {
    return nullptr;
  }

  auto vulkanGPU = static_cast<VulkanGPU*>(device->lockContext()->gpu());

  if (!vulkanGPU->extensions().swapchain) {
    LOGE("VulkanWindow: swapchain extension not available.");
    device->unlock();
    return nullptr;
  }

  auto vkInstance = vulkanGPU->instance();

  VkAndroidSurfaceCreateInfoKHR createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
  createInfo.window = window;

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  auto result = vkCreateAndroidSurfaceKHR(vkInstance, &createInfo, nullptr, &surface);
  if (result != VK_SUCCESS) {
    LOGE("VulkanWindow: vkCreateAndroidSurfaceKHR failed: %d", result);
    device->unlock();
    return nullptr;
  }

  // ... 后续 swapchain 创建逻辑（复用现有 createSwapchain 流程）
}
#endif
```

#### 3.2.3 Swapchain 参数适配

Android 上 swapchain 创建需要关注以下特殊参数：

| 参数 | Android 推荐值 | 说明 |
|------|---------------|------|
| `imageCount` | `minImageCount + 1`（通常 3） | 三缓冲降低卡顿 |
| `imageFormat` | `VK_FORMAT_R8G8B8A8_UNORM` | Android 最广泛支持 |
| `presentMode` | `VK_PRESENT_MODE_FIFO_KHR` | 强制 VSync，省电 |
| `compositeAlpha` | `VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR` | Android Compositor 处理透明度 |
| `preTransform` | `capabilities.currentTransform` | 跟随系统旋转，避免 GPU 额外旋转 |
| `clipped` | `VK_TRUE` | 允许裁剪不可见区域 |

#### 3.2.4 Surface 尺寸变化处理

Android 上 Activity 旋转、多窗口模式切换、折叠屏展开都会导致 Surface 尺寸变化：

```cpp
// 在 present() 中检测
VkResult result = vkQueuePresentKHR(queue, &presentInfo);
if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    recreateSwapchain();
}

// 在 acquireNextImage() 中检测
result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, semaphore, VK_NULL_HANDLE, &imageIndex);
if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapchain();
    return; // 本帧跳过
}
```

**preTransform 处理策略**：

```cpp
// 方案 A（推荐）：应用层补偿旋转，GPU 不做额外工作
// 在顶点着色器或投影矩阵中乘以 preTransform 对应的旋转矩阵
Matrix preRotation;
switch (preTransform) {
    case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
        preRotation = Matrix::MakeRotate(90); break;
    case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
        preRotation = Matrix::MakeRotate(180); break;
    case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
        preRotation = Matrix::MakeRotate(270); break;
    default:
        preRotation = Matrix::I(); break;
}

// 方案 B（简单但有性能代价）：设置 preTransform = IDENTITY
// Compositor 会在 display 时做旋转，增加一次额外的 blit
```

### 3.3 AHardwareBuffer 纹理导入

#### 3.3.1 扩展需求

```cpp
// 实例扩展
VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME     // 查询外部内存能力
VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME // 已在 1.1 中推升为 core

// 设备扩展
VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME
VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME
VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME
VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME  // YUV 格式必需
```

#### 3.3.2 导入流程

```cpp
// 1. 查询 AHardwareBuffer 属性
VkAndroidHardwareBufferFormatPropertiesANDROID formatProps = {};
formatProps.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;

VkAndroidHardwareBufferPropertiesANDROID bufferProps = {};
bufferProps.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
bufferProps.pNext = &formatProps;

vkGetAndroidHardwareBufferPropertiesANDROID(device, hardwareBuffer, &bufferProps);

// 2. 分配外部内存
VkImportAndroidHardwareBufferInfoANDROID importInfo = {};
importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
importInfo.buffer = hardwareBuffer;

VkMemoryDedicatedAllocateInfo dedicatedInfo = {};
dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
dedicatedInfo.pNext = &importInfo;
dedicatedInfo.image = vkImage;

VkMemoryAllocateInfo allocInfo = {};
allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
allocInfo.pNext = &dedicatedInfo;
allocInfo.allocationSize = bufferProps.allocationSize;
allocInfo.memoryTypeIndex = findMemoryType(bufferProps.memoryTypeBits);

vkAllocateMemory(device, &allocInfo, nullptr, &memory);
vkBindImageMemory(device, vkImage, memory, 0);

// 3. 如果是 YUV 格式，创建 YCbCr Conversion
if (formatProps.format == VK_FORMAT_UNDEFINED) {
    // 需要 VkSamplerYcbcrConversion + immutable sampler
    VkSamplerYcbcrConversionCreateInfo ycbcrInfo = {};
    ycbcrInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
    ycbcrInfo.pNext = &externalFormat;
    ycbcrInfo.format = VK_FORMAT_UNDEFINED;
    ycbcrInfo.ycbcrModel = formatProps.suggestedYcbcrModel;
    ycbcrInfo.ycbcrRange = formatProps.suggestedYcbcrRange;
    ycbcrInfo.components = formatProps.samplerYcbcrConversionComponents;
    ycbcrInfo.xChromaOffset = formatProps.suggestedXChromaOffset;
    ycbcrInfo.yChromaOffset = formatProps.suggestedYChromaOffset;
    ycbcrInfo.chromaFilter = VK_FILTER_LINEAR;
    vkCreateSamplerYcbcrConversion(device, &ycbcrInfo, nullptr, &conversion);
}
```

#### 3.3.3 tgfx API 设计

```cpp
// 公开接口：include/tgfx/gpu/vulkan/VulkanTexture.h
class VulkanTexture {
 public:
#ifdef __ANDROID__
  /// Imports an AHardwareBuffer as a Vulkan texture. The hardware buffer must remain valid
  /// for the lifetime of the returned texture. Supports RGBA and YUV formats.
  static std::shared_ptr<Texture> MakeFromHardwareBuffer(
      Context* context, AHardwareBuffer* hardwareBuffer,
      std::shared_ptr<ColorSpace> colorSpace = nullptr);
#endif
};
```

### 3.4 Demo App 适配

#### 3.4.1 JNI 层修改

**文件**：`android/app/src/main/cpp/JTGFXView.cpp`

```cpp
#include "tgfx/gpu/vulkan/VulkanDevice.h"
#include "tgfx/gpu/vulkan/VulkanWindow.h"

static std::shared_ptr<tgfx::VulkanDevice> vulkanDevice;
static std::shared_ptr<tgfx::VulkanWindow> vulkanWindow;

JNIEXPORT void JNICALL Java_org_tgfx_hello2d_TGFXView_nativeSurfaceCreated(
    JNIEnv* env, jobject, jobject surface) {
  ANativeWindow* nativeWindow = ANativeWindow_fromSurface(env, surface);

  // 创建（或复用）VulkanDevice
  if (!vulkanDevice) {
    vulkanDevice = tgfx::VulkanDevice::Make();
    if (!vulkanDevice) {
      // Vulkan 不可用，回退到 EGL
      fallbackToEGL(nativeWindow);
      return;
    }
  }

  vulkanWindow = tgfx::VulkanWindow::MakeFrom(nativeWindow, vulkanDevice);
}

JNIEXPORT void JNICALL Java_org_tgfx_hello2d_TGFXView_nativeDrawFrame(JNIEnv*, jobject) {
  if (!vulkanWindow) return;

  auto surface = vulkanWindow->createSurface(vulkanDevice->lockContext());
  if (!surface) return;

  auto canvas = surface->getCanvas();
  // ... 绘制逻辑
  surface->flush();
  vulkanDevice->unlock();
  vulkanWindow->present(vulkanDevice->lockContext());
  vulkanDevice->unlock();
}
```

#### 3.4.2 Gradle/CMake 配置

**文件**：`android/app/CMakeLists.txt`

```cmake
# 启用 Vulkan 后端
set(TGFX_USE_VULKAN ON)
add_subdirectory(${TGFX_DIR} tgfx)
```

**文件**：`android/app/build.gradle`

```groovy
android {
    defaultConfig {
        minSdkVersion 24  // Vulkan 1.0 最低要求
        // 推荐 minSdkVersion 29 以获得 Vulkan 1.1 保证
    }
}
```

### 3.5 Vulkan 加载与可用性检测

tgfx 使用 volk 动态加载 Vulkan。在 Android 上流程为：

```cpp
// VulkanGPU::initVulkan() 中已有：
if (volkInitialize() != VK_SUCCESS) {
    // libvulkan.so 不存在或无法加载 vkGetInstanceProcAddr
    // → 不支持 Vulkan
    return false;
}
```

volk 在 Android 上的加载路径：
```c
// volk.c 内部
#if defined(__ANDROID__)
    library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#endif
```

### 3.6 设备兼容性与黑名单

#### 3.6.1 已知问题设备

| GPU 系列 | 驱动版本 | 问题 |
|----------|----------|------|
| Adreno 5xx | < 512.415 | vkAllocateMemory 泄漏 |
| Mali-G71 | r16p0 以下 | Multisampled render target 崩溃 |
| PowerVR GE8xxx | 部分固件 | Pipeline cache 损坏 |
| Samsung Xclipse 920 | 初始驱动 | Timeline semaphore 死锁 |

#### 3.6.2 检测逻辑

```cpp
struct VulkanBlacklist {
    uint32_t vendorID;
    uint32_t deviceID;        // 0 = 匹配所有该厂商设备
    uint32_t maxDriverVersion; // 低于此版本的驱动被黑名单
};

static const VulkanBlacklist BLACKLIST[] = {
    {0x5143, 0, VK_MAKE_VERSION(512, 415, 0)},  // Qualcomm Adreno < 512.415
    {0x13B5, 0x70901000, VK_MAKE_VERSION(16, 0, 0)},  // ARM Mali-G71 < r16
};

bool IsDeviceBlacklisted(VkPhysicalDeviceProperties props) {
    for (auto& entry : BLACKLIST) {
        if (props.vendorID == entry.vendorID &&
            (entry.deviceID == 0 || props.deviceID == entry.deviceID) &&
            props.driverVersion < entry.maxDriverVersion) {
            return true;
        }
    }
    return false;
}
```

---

## 4. 同步与生命周期管理

### 4.1 帧同步模型

Android Vulkan 推荐使用三缓冲 + 信号量同步：

```
Frame N:
  ┌─ acquireNextImage (imageAvailableSemaphore[N%3]) ─┐
  │                                                    │
  │  记录命令 → submit (等待 imageAvailable,           │
  │                     信号 renderFinished)            │
  │                                                    │
  └─ present (等待 renderFinished) ────────────────────┘

Frame N+1: 使用下一组信号量，不等待 Frame N 完成
```

```cpp
// 帧内同步对象（每个 in-flight frame 一组）
struct FrameSync {
    VkSemaphore imageAvailable;
    VkSemaphore renderFinished;
    VkFence inFlightFence;
};
static constexpr int MAX_FRAMES_IN_FLIGHT = 3;
FrameSync frameSyncs[MAX_FRAMES_IN_FLIGHT];
```

### 4.2 Surface 生命周期

Android Activity 生命周期与 Surface 绑定：

```
Activity.onCreate()
  └─ SurfaceView.surfaceCreated() → 创建 VulkanWindow + Swapchain

Activity.onPause() / 多窗口离开焦点
  └─ 可选：降低帧率 / 暂停渲染

SurfaceView.surfaceChanged() → 重建 Swapchain（尺寸/旋转变化）

SurfaceView.surfaceDestroyed()
  └─ 销毁 Swapchain + Surface
     ⚠️ 必须等待 GPU idle：vkDeviceWaitIdle()
     ⚠️ 然后释放所有引用该 surface 的资源

Activity.onDestroy()
  └─ 销毁 VulkanDevice
```

**关键约束**：`surfaceDestroyed` 回调返回后，`ANativeWindow` 立即失效。必须在回调内完成所有 GPU 等待和资源释放。

### 4.3 外部信号量（与 MediaCodec/Camera 同步）

```cpp
// 导入 Android sync fd 为 Vulkan 信号量
VkImportSemaphoreFdInfoKHR importInfo = {};
importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR;
importInfo.semaphore = semaphore;
importInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
importInfo.fd = syncFd;  // 从 MediaCodec/Camera 获得
importInfo.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;  // 一次性使用

vkImportSemaphoreFdKHR(device, &importInfo);
// 之后在 vkQueueSubmit 中等待此信号量，确保 HardwareBuffer 写入完成
```

---

## 5. 内存管理策略

### 5.1 VMA 配置（Android 特化）

tgfx 使用 Vulkan Memory Allocator (VMA)。Android 上需注意：

```cpp
VmaAllocatorCreateInfo allocatorInfo = {};
allocatorInfo.physicalDevice = physicalDevice;
allocatorInfo.device = device;
allocatorInfo.instance = instance;
allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_1;

// Android 特化：启用外部内存扩展支持
allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;

// 移动端内存有限，设置预算回调
allocatorInfo.pDeviceMemoryCallbacks = &memoryCallbacks;
```

### 5.2 内存预算感知

Android 设备内存有限（通常 GPU 与 CPU 共享物理内存），需要：

```cpp
// 查询内存预算（VK_EXT_memory_budget）
VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {};
budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

VkPhysicalDeviceMemoryProperties2 memProps = {};
memProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
memProps.pNext = &budget;
vkGetPhysicalDeviceMemoryProperties2(physicalDevice, &memProps);

// budget.heapBudget[i] — 可用预算
// budget.heapUsage[i] — 当前使用量
// 当 usage > budget * 0.8 时，触发 Atlas LRU 淘汰
```

### 5.3 Atlas 策略调整

| 参数 | Desktop | Android（建议） | 原因 |
|------|---------|----------------|------|
| Atlas 尺寸 | 2048×2048 | 1024×1024 或 2048×2048 | 取决于 maxImageDimension2D |
| 缓存上限 | 4MB | 2MB | 移动端内存压力 |
| 条目上限 | 2048 | 1024 | 减少 GPU 内存占用 |
| 淘汰阈值 | 70% | 60% | 更早触发回收 |

---

## 6. 性能优化

### 6.1 Pipeline Cache 持久化

```cpp
// 保存 pipeline cache 到应用私有目录
std::string cachePath = getApplicationCacheDir() + "/vulkan_pipeline_cache.bin";

// 创建时加载
VkPipelineCacheCreateInfo cacheInfo = {};
cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
if (loadFromFile(cachePath, &cacheData)) {
    cacheInfo.initialDataSize = cacheData.size();
    cacheInfo.pInitialData = cacheData.data();
}
vkCreatePipelineCache(device, &cacheInfo, nullptr, &pipelineCache);

// 退出时保存
size_t dataSize = 0;
vkGetPipelineCacheData(device, pipelineCache, &dataSize, nullptr);
std::vector<uint8_t> data(dataSize);
vkGetPipelineCacheData(device, pipelineCache, &dataSize, data.data());
saveToFile(cachePath, data);
```

### 6.2 命令缓冲复用

对于静态 UI，可预录制命令缓冲并跨帧复用：

```cpp
// 一级命令缓冲：每帧动态内容
// 二级命令缓冲：静态 UI 预录制，通过 vkCmdExecuteCommands 嵌入
if (!uiChanged) {
    vkCmdExecuteCommands(primaryCB, 1, &cachedSecondaryCB);
} else {
    recordUI(primaryCB);
    // 同时异步重录 secondary CB 供下帧使用
}
```

### 6.3 渲染通道优化

Android 上 tile-based GPU（Adreno/Mali）对 renderpass 的 load/store 操作敏感：

```cpp
// 推荐配置
VkAttachmentDescription colorAttachment = {};
colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;      // 不需要读回上一帧
colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;    // 需要 present
// 深度/模板（如果不需要跨帧保留）
depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // 节省带宽

// 使用 subpass 减少 resolve/load 开销（MSAA → 单采样 resolve 在 subpass 内完成）
```

---

## 7. 测试策略

### 7.1 本地开发测试

```bash
# Android Studio 调试配置
# 在 app/build.gradle 中启用 Validation Layer
android {
    buildTypes {
        debug {
            ndk {
                // 打包 validation layer 到 APK
            }
        }
    }
}

# 或通过 adb 启用
adb shell setprop debug.vulkan.layers "VK_LAYER_KHRONOS_validation"
adb shell setprop debug.vulkan.force_enable "1"
```

### 7.2 CI 自动化测试

**推荐方案**：GitHub Actions + Android Emulator（API 30+，内置 SwiftShader Vulkan）

```yaml
# .github/workflows/autotest.yml — 新增 android-vulkan job
android-vulkan:
  name: android (vulkan-swiftshader)
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
      with:
        lfs: true

    - name: Setup Android SDK
      uses: android-actions/setup-android@v3

    - name: AVD Cache
      uses: actions/cache@v4
      with:
        path: ~/.android/avd
        key: avd-api30-x86_64

    - name: Create AVD
      run: |
        echo "no" | avdmanager create avd -n test_avd -k "system-images;android-30;google_apis;x86_64" --force

    - name: Run Tests
      uses: reactivecircus/android-emulator-runner@v2
      with:
        api-level: 30
        arch: x86_64
        script: |
          ./gradlew :app:connectedAndroidTest -Ptgfx.backend=vulkan
```

### 7.3 截图基线

Android Vulkan 使用独立的基线缓存目录：

```
test/baseline/.cache/vulkan-android/     # Android 真机/模拟器 Vulkan
test/baseline/.cache/vulkan-swiftshader/ # macOS/Windows SwiftShader
```

由于 Android SwiftShader（模拟器内置）与 macOS SwiftShader 可能版本不同导致像素差异，Android 应使用独立基线。

---

## 8. 实施路线图

### Phase 1：MVP（可渲染） — 预计 2-3 天

| 任务 | 产出 |
|------|------|
| CMake 解除 Android 限制 | Android 可编译 Vulkan 后端 |
| VulkanWindow Android 实现 | ANativeWindow → Surface → Swapchain |
| Demo App 切换 | hello2d 可在真机上 Vulkan 渲染 |
| 基本烟雾测试 | 绘制彩色矩形 + 文本，确认管线通路 |

### Phase 2：生产可用 — 预计 1-2 周

| 任务 | 产出 |
|------|------|
| AHardwareBuffer 导入 | 视频帧/Camera 帧作为纹理 |
| YCbCr Sampler 支持 | YUV 视频无需 shader 转换 |
| 设备旋转处理 | preTransform 补偿，无撕裂 |
| 外部信号量同步 | MediaCodec/Camera GPU-GPU 同步 |
| Pipeline Cache 持久化 | 二次启动无编译延迟 |
| 设备黑名单 + 优雅降级 | 问题设备回退 GLES |

### Phase 3：质量保证 — 预计 1 周

| 任务 | 产出 |
|------|------|
| Android CI 环境搭建 | 自动化截图测试 |
| Validation Layer 集成 | Debug 构建自动报告违规 |
| 内存预算监控 | 低内存预警 + Atlas 主动收缩 |
| 性能对比报告 | GL vs VK 帧时间/内存/功耗对比 |

---

## 9. 风险评估

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|----------|
| 驱动碎片化导致渲染异常 | 高 | 中 | 设备黑名单 + GL 降级 |
| AHardwareBuffer YUV 格式多样 | 中 | 高 | 通过 formatProperties 运行时适配 |
| 内存不足导致 OOM | 中 | 高 | 内存预算监控 + 激进 LRU |
| CI 模拟器 Vulkan 与真机差异 | 中 | 低 | 独立基线 + 真机验证环节 |
| preTransform 在个别设备行为异常 | 低 | 中 | 可配置 fallback 到 IDENTITY |
| VMA 在特定驱动上分配失败 | 低 | 高 | 增加分配重试 + 降低 Atlas 尺寸 |

---

## 10. 参考资料

1. Vulkan Programming Guide — Android Chapter. https://developer.android.com/ndk/guides/graphics/getting-started
2. Android Vulkan Design Guidelines. https://developer.android.com/games/optimize/vulkan-best-practices
3. Vulkan Mobile Best Practices (ARM). https://arm-software.github.io/vulkan_best_practice_for_mobile_developers/
4. AHardwareBuffer Vulkan Interop. https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_ANDROID_external_memory_android_hardware_buffer.html
5. SwiftShader Vulkan on Android Emulator. https://swiftshader.googlesource.com/SwiftShader/
6. volk — Meta-loader for Vulkan. https://github.com/zeux/volk
