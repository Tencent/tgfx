# TGFX Windows Vulkan 后端——实施计划

> **版本**：1.0
> **日期**：2026-04-28
> **分支**：`feature/edwardxfshen_win_autotest`
> **前置文档**：[vulkan-backend-design.md](vulkan-backend-design.md)、[vulkan-dependency-survey.md](vulkan-dependency-survey.md)、[vulkan-memory-design.md](vulkan-memory-design.md)

---

## 目录

1. [项目目标](#1-项目目标)
2. [现状分析](#2-现状分析)
3. [技术选型摘要](#3-技术选型摘要)
4. [分阶段实施计划](#4-分阶段实施计划)
   - Phase 0：依赖引入与构建系统
   - Phase 1：Shader 编译工具提取
   - Phase 2：公开 API 扩展
   - Phase 3：Vulkan 核心实现
   - Phase 4：窗口与上屏
   - Phase 5：Hello2D Win 端适配
   - Phase 5B：测试框架与 Backend 切换
   - Phase 6：预留（后续）
5. [文件变更清单](#5-文件变更清单)
6. [工作量估算](#6-工作量估算)
7. [关键依赖关系](#7-关键依赖关系)
8. [风险与对策](#8-风险与对策)

---

## 1. 项目目标

为 TGFX 图形引擎在 **Windows 平台**接入 **Vulkan GPU 后端**（Phase 1），使 Hello2D 示例程序能够通过 Vulkan 在 Windows 上正确渲染并呈现画面。

**成功标准**：

- `cmake -DTGFX_USE_VULKAN=ON` 可正常配置和编译
- Hello2D 在 Windows 上通过 Vulkan 渲染上屏，画面与 OpenGL 后端一致
- 现有 OpenGL/Metal 构建路径不受影响（`TGFX_USE_VULKAN` 默认 OFF）

---

## 2. 现状分析

### 2.1 已完成（设计阶段）

| 产出 | 文件 | 内容 |
|------|------|------|
| 技术设计文档 | `docs/vulkan-backend-design.md` | 完整的架构设计、Shader 编译链、Descriptor 策略、命令模型、帧同步、目录结构、关键代码结构 |
| 依赖调研文档 | `docs/vulkan-dependency-survey.md` | 7 个业内项目（Skia/Dawn/ANGLE/Filament/Godot/Vulkan-Samples/SDL）的 Vulkan 依赖方案对比 |
| 内存管理文档 | `docs/vulkan-memory-design.md` | Vulkan 内存管理 6 层复杂性分析、VMA 解决方案、TGFX 集成设计 |

### 2.2 代码现状

| 项目 | 状态 | 详情 |
|------|------|------|
| `Backend::Vulkan` 枚举 | ✅ 已声明 | `include/tgfx/gpu/Backend.h` 第 29 行 |
| `BackendTexture/RenderTarget/Semaphore` | ❌ 无 Vulkan 变体 | 仅有 OpenGL 和 Metal 的构造器、union 成员、getter |
| `TGFX_USE_VULKAN` CMake 选项 | ❌ 不存在 | 当前构建系统仅支持 `TGFX_USE_OPENGL` 和 `TGFX_USE_METAL` |
| Android Vulkan 占位符 | ⚠️ 部分存在 | `CMakeLists.txt` 第 338-340 行有 `src/gpu/vulkan/*.*` 的 glob，但无 define/include/vendor 配置 |
| `src/gpu/vulkan/` 目录 | ❌ 不存在 | 无任何 Vulkan 实现代码 |
| `include/tgfx/gpu/vulkan/` 目录 | ❌ 不存在 | 无 Vulkan 公开头文件 |
| DEPS（第三方依赖） | ❌ 缺少 3 个库 | Vulkan-Headers、volk、VulkanMemoryAllocator 尚未引入 |
| `vendor.json`（编译构建） | ❌ 需扩展平台 | shaderc 和 SPIRV-Cross 仅配置了 mac/ios 平台，缺少 win 平台 |
| Metal 后端参考 | ✅ 37 个文件 | `src/gpu/metal/` 下完整的 Metal 实现，作为 Vulkan 实现的主要蓝本 |

### 2.3 Metal 后端文件结构（参考蓝本）

```
src/gpu/metal/                          include/tgfx/gpu/metal/
├── MetalGPU.h / .mm          (14 KB)  ├── MetalDevice.h
├── MetalDevice.h / .mm        (3 KB)  ├── MetalTypes.h
├── MetalCaps.h / .mm          (9 KB)  └── MetalWindow.h
├── MetalCommandQueue.h / .mm (11 KB)
├── MetalCommandEncoder.h / .mm (9 KB)
├── MetalCommandBuffer.h / .mm  (3 KB)
├── MetalRenderPass.h / .mm   (19 KB)
├── MetalRenderPipeline.h / .mm (17 KB)
├── MetalShaderModule.h / .mm  (20 KB)
├── MetalTexture.h / .mm       (8 KB)
├── MetalBuffer.h / .mm        (5 KB)
├── MetalSampler.h / .mm       (4 KB)
├── MetalSemaphore.h / .mm     (4 KB)
├── MetalResource.h / .mm      (3 KB)
├── MetalDrawableProxy.h / .mm  (5 KB)
├── MetalHardwareTexture.h / .mm (7 KB)
├── MetalExternalTexture.h      (1 KB)
├── MetalUtil.h / .mm          (13 KB)
├── MetalDefines.h              (2 KB)
└── MetalWindow.mm              (4 KB)
```

---

## 3. 技术选型摘要

详见 [vulkan-backend-design.md](vulkan-backend-design.md) 和 [vulkan-dependency-survey.md](vulkan-dependency-survey.md)，核心决策如下：

| 决策点 | 选择 | 理由 |
|--------|------|------|
| 图形 API 版本 | Vulkan 1.0 + 必需扩展 | 最低要求，覆盖面最广 |
| Vulkan 头文件 | DEPS 引入 KhronosGroup/Vulkan-Headers | 版本可控、与 SwiftShader 解耦、CI 无需预装 SDK |
| 函数加载 | volk（`VK_NO_PROTOTYPES` + 运行时动态加载） | 7/7 调研项目均不链接 vulkan-1.lib；volk 维护成本最低、天然跨平台 |
| 内存管理 | VMA（Vulkan Memory Allocator） | 业界标准 header-only 库，省去 ~3000 行手写代码 |
| Shader 编译 | 复用现有 shaderc + SPIRV-Cross | Metal 后端已集成，Vulkan 直接使用 SPIR-V 产物 |
| Descriptor 策略 | 单 Set 固定布局 | TGFX 引擎层的 binding 常量为连续平坦命名空间，无需多 Set |
| 帧同步 | VK_KHR_timeline_semaphore | 对标 Metal 的 MTLEvent，桌面 GPU 广泛支持 |
| Loader 链接 | **不链接** `vulkan-1.lib` | volk 运行时 `LoadLibrary("vulkan-1.dll")` 加载，优雅降级 |

### 必需 Vulkan 扩展

| 扩展名 | 级别 | 用途 |
|--------|------|------|
| `VK_KHR_surface` | Instance | Swapchain 基础 |
| `VK_KHR_win32_surface` | Instance | Win32 窗口呈现 |
| `VK_KHR_get_physical_device_properties2` | Instance | timeline semaphore 依赖 |
| `VK_KHR_swapchain` | Device | 交换链 |
| `VK_KHR_timeline_semaphore` | Device | 帧同步 |

---

## 4. 分阶段实施计划

### Phase 0：依赖引入与构建系统

> **目标**：让 `cmake -DTGFX_USE_VULKAN=ON` 能通过配置阶段，所有第三方依赖就位。

#### 0.1 DEPS 新增 3 条依赖

**文件**：`DEPS`

在 `repos.common` 数组中新增：

```json
{
  "url": "https://github.com/KhronosGroup/Vulkan-Headers.git",
  "commit": "<锁定到与 volk 匹配的 SDK 版本 tag>",
  "dir": "third_party/Vulkan-Headers"
},
{
  "url": "https://github.com/zeux/volk.git",
  "commit": "<锁定到最新稳定 tag>",
  "dir": "third_party/volk"
},
{
  "url": "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git",
  "commit": "<锁定到最新稳定 tag>",
  "dir": "third_party/VulkanMemoryAllocator"
}
```

**说明**：三个库的 commit 版本需对齐同一 Vulkan SDK 版本（如 1.3.x），确保头文件与函数加载器兼容。

#### 0.2 vendor.json 扩展 win 平台

**文件**：`vendor.json`

**新增 shaderc win 平台条目**（复制 mac 条目，修改 platforms）：

```json
{
  "name": "shaderc",
  "cmake": {
    "targets": ["shaderc_combined"],
    "arguments": [
      "-DSHADERC_SKIP_TESTS=ON",
      "-DSHADERC_SKIP_EXAMPLES=ON",
      "-DSHADERC_SKIP_INSTALL=ON",
      "-DCMAKE_CXX_FLAGS=\"-w\""
    ],
    "platforms": ["win"]
  }
}
```

**新增 SPIRV-Cross win 平台条目**（不含 spirv-cross-msl，Vulkan 后端不需要 MSL 转换）：

```json
{
  "name": "SPIRV-Cross",
  "cmake": {
    "targets": [
      "spirv-cross-core",
      "spirv-cross-glsl"
    ],
    "arguments": [
      "-DSPIRV_CROSS_ENABLE_TESTS=OFF",
      "-DCMAKE_CXX_FLAGS=\"-w\""
    ],
    "platforms": ["win"]
  }
}
```

#### 0.3 CMakeLists.txt 新增 `TGFX_USE_VULKAN` 选项

**文件**：`CMakeLists.txt`

**变更点 1**：在 option 区域新增选项（约第 28 行后）

```cmake
option(TGFX_USE_VULKAN "Use Vulkan as the GPU backend" OFF)
```

**变更点 2**：互斥逻辑（修改现有的后端检查区域，约第 105-131 行）

```cmake
if (TGFX_USE_VULKAN)
    if (NOT WIN32 AND NOT ANDROID)
        message(FATAL_ERROR "TGFX_USE_VULKAN is currently only supported on Windows and Android.")
    endif ()
    set(TGFX_USE_OPENGL OFF)
    set(TGFX_USE_METAL OFF)
    set(TGFX_USE_QT OFF)
    set(TGFX_USE_SWIFTSHADER OFF)
    set(TGFX_USE_ANGLE OFF)
endif ()

if (NOT TGFX_USE_METAL AND NOT TGFX_USE_OPENGL AND NOT TGFX_USE_VULKAN)
    message(FATAL_ERROR "At least one GPU backend must be enabled.")
endif ()
```

**变更点 3**：Vulkan 源文件收集和依赖配置（在 GPU 后端选择区域）

```cmake
if (TGFX_USE_VULKAN)
    file(GLOB_RECURSE GFX_PLATFORM_FILES src/gpu/vulkan/*.*)
    list(APPEND TGFX_FILES ${GFX_PLATFORM_FILES})
    list(APPEND TGFX_DEFINES TGFX_USE_VULKAN)

    # Vulkan-Headers (header-only)
    list(APPEND TGFX_INCLUDES third_party/Vulkan-Headers/include)

    # volk (compile volk.c directly, same pattern as lz4)
    list(APPEND TGFX_INCLUDES third_party/volk)
    file(GLOB VOLK_FILES third_party/volk/volk.c)
    list(APPEND TGFX_FILES ${VOLK_FILES})

    # VMA (header-only, same pattern as json/concurrentqueue)
    list(APPEND TGFX_INCLUDES third_party/VulkanMemoryAllocator/include)

    # shaderc + SPIRV-Cross (shared with Metal backend)
    list(APPEND TGFX_STATIC_VENDORS shaderc SPIRV-Cross)
    list(APPEND TGFX_INCLUDES third_party/shaderc/libshaderc/include)
    list(APPEND TGFX_INCLUDES third_party/SPIRV-Cross)
    list(APPEND TGFX_DEFINES SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS)
endif ()
```

**变更点 4**：shaderc 子依赖同步（复用 Metal 后端的 `git-sync-deps` 逻辑）

```cmake
if (TGFX_USE_VULKAN OR TGFX_USE_METAL)
    if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/shaderc/third_party/glslang)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        message(STATUS "Syncing shaderc sub-dependencies...")
        execute_process(
                COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/third_party/shaderc/utils/git-sync-deps
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/shaderc
                RESULT_VARIABLE SYNC_RESULT)
        if (NOT SYNC_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to sync shaderc sub-dependencies.")
        endif ()
    endif ()
endif ()
```

**变更点 5**：Windows 平台 Vulkan 模式不链接 opengl32（修改 WIN32 区域）

```cmake
elseif (WIN32)
    # ... existing Win32 libs ...
    if (TGFX_USE_NATIVE_GL)
        file(GLOB_RECURSE GPU_PLATFORM_FILES src/gpu/opengl/wgl/*.*)
        find_library(OPENGL_LIB opengl32)
        list(APPEND TGFX_STATIC_LIBS ${OPENGL_LIB})
        list(APPEND TGFX_FILES ${GPU_PLATFORM_FILES})
    endif ()
endif ()
```

> 注：`TGFX_USE_NATIVE_GL` 仅在 `TGFX_USE_OPENGL=ON` 且非 Qt/SwiftShader/ANGLE 时为 ON，Vulkan 模式下自然不会设置，无需额外修改。

#### 0.4 同步依赖并验证

```bash
# 同步所有 DEPS 依赖
./sync_deps.sh

# 验证新目录存在
ls third_party/Vulkan-Headers/include/vulkan/vulkan.h
ls third_party/volk/volk.h
ls third_party/VulkanMemoryAllocator/include/vk_mem_alloc.h

# 验证 CMake 配置通过（此时 src/gpu/vulkan/ 为空，不会报错）
cmake -G Ninja -DTGFX_USE_VULKAN=ON -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -B cmake-build-debug
```

---

### Phase 1：Shader 编译工具提取

> **目标**：将 Metal 独有的 GLSL→SPIR-V 预处理代码提取为 Metal/Vulkan 共享的公共模块。

#### 1.1 创建 `ShaderCompiler.h`

**新建文件**：`src/gpu/ShaderCompiler.h`

```cpp
#pragma once
#include <string>
#include <vector>
#include "tgfx/gpu/ShaderStage.h"

namespace shaderc {
class Compiler;
}

namespace tgfx {

/// Preprocesses GLSL source code for Vulkan/Metal compatibility: upgrades to #version 450,
/// assigns explicit UBO bindings, sampler bindings, and in/out location qualifiers.
std::string PreprocessGLSL(const std::string& glslCode);

/// Compiles preprocessed GLSL to SPIR-V binary using shaderc. Returns empty vector on failure.
std::vector<uint32_t> CompileGLSLToSPIRV(const shaderc::Compiler* compiler,
                                          const std::string& vulkanGLSL,
                                          ShaderStage stage);

}  // namespace tgfx
```

#### 1.2 创建 `ShaderCompiler.cpp`

**新建文件**：`src/gpu/ShaderCompiler.cpp`

从 `MetalShaderModule.mm` 提取以下静态函数（约 180 行代码）：

- `replaceAllMatches()` —— 正则替换辅助
- `preprocessGLSL()` —— 6 步正则变换（升级 #version、分配 UBO binding、分配 sampler binding、分配 location、移除 precision）
- `compileGLSLToSPIRV()` —— shaderc 编译调用

提取为平台无关的 `.cpp` 文件（非 `.mm`），确保 Windows 可编译。

#### 1.3 修改 `MetalShaderModule.mm`

**修改文件**：`src/gpu/metal/MetalShaderModule.mm`

- 移除已提取的 static 函数
- 新增 `#include "gpu/ShaderCompiler.h"`
- 改为调用 `PreprocessGLSL()` 和 `CompileGLSLToSPIRV()` 公共接口
- Metal 后端继续在公共接口之后调用 SPIRV-Cross 转 MSL

#### 1.4 验证

```bash
# Mac 上验证 Metal 后端不受影响
cmake -G Ninja -DTGFX_USE_METAL=ON -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -B cmake-build-debug
cmake --build cmake-build-debug --target TGFXFullTest
```

---

### Phase 2：公开 API 扩展

> **目标**：完成 Vulkan 相关的公开头文件定义，为内部实现提供接口契约。

#### 2.1 创建 `VulkanTypes.h`

**新建文件**：`include/tgfx/gpu/vulkan/VulkanTypes.h`

定义 Vulkan 原生类型桥接结构，**使用前向声明避免公开头文件暴露 vulkan.h**：

```cpp
#pragma once
#include <cstdint>

// Forward declarations matching Vulkan spec typedefs.
typedef struct VkImage_T* VkImage;
typedef struct VkSemaphore_T* VkSemaphore;
typedef uint32_t VkFormat;
typedef uint32_t VkImageLayout;
typedef uint32_t VkImageUsageFlags;

namespace tgfx {

/// Describes a Vulkan texture for external import/export.
struct VulkanTextureInfo {
  VkImage image = nullptr;
  VkFormat format = 0;            // VK_FORMAT_UNDEFINED
  VkImageLayout layout = 0;       // VK_IMAGE_LAYOUT_UNDEFINED
  VkImageUsageFlags usage = 0;
};

/// Describes a Vulkan image used as a render target.
struct VulkanImageInfo {
  VkImage image = nullptr;
  VkFormat format = 0;
  VkImageLayout layout = 0;
};

/// Describes a Vulkan timeline semaphore for synchronization.
struct VulkanSyncInfo {
  VkSemaphore semaphore = nullptr;
  uint64_t value = 0;
};

}  // namespace tgfx
```

#### 2.2 创建 `VulkanDevice.h`

**新建文件**：`include/tgfx/gpu/vulkan/VulkanDevice.h`

```cpp
#pragma once
#include "tgfx/gpu/Device.h"

namespace tgfx {

/// VulkanDevice manages a VkInstance, VkPhysicalDevice, VkDevice, and VkQueue. It serves as the
/// primary entry point for creating a Vulkan rendering context.
class VulkanDevice : public Device {
 public:
  /// Creates a new VulkanDevice. Returns nullptr if Vulkan is not available on the system (e.g.,
  /// no Vulkan driver installed), allowing the caller to gracefully fall back to OpenGL.
  static std::shared_ptr<VulkanDevice> Make();

 protected:
  VulkanDevice();
};

}  // namespace tgfx
```

#### 2.3 创建 `VulkanWindow.h`

**新建文件**：`include/tgfx/gpu/vulkan/VulkanWindow.h`

```cpp
#pragma once
#include "tgfx/gpu/Window.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace tgfx {

class Device;

/// VulkanWindow manages a VkSurfaceKHR and VkSwapchainKHR for presenting rendered content to a
/// platform window.
class VulkanWindow : public Window {
 public:
#ifdef _WIN32
  /// Creates a VulkanWindow from a Win32 window handle.
  static std::shared_ptr<VulkanWindow> MakeFrom(HWND hwnd, std::shared_ptr<Device> device);
#elif defined(__ANDROID__)
  struct ANativeWindow;
  /// Creates a VulkanWindow from an Android native window.
  static std::shared_ptr<VulkanWindow> MakeFrom(ANativeWindow* window,
                                                 std::shared_ptr<Device> device);
#endif
};

}  // namespace tgfx
```

#### 2.4 扩展 `Backend.h`

**修改文件**：`include/tgfx/gpu/Backend.h`

- 新增 `#include "tgfx/gpu/vulkan/VulkanTypes.h"`
- `BackendTexture`：新增 `BackendTexture(const VulkanTextureInfo&, int, int)` 构造器 + `VulkanTextureInfo vulkanInfo` union 成员 + `getVulkanTextureInfo()` getter
- `BackendRenderTarget`：新增 `BackendRenderTarget(const VulkanImageInfo&, int, int)` 构造器 + `VulkanImageInfo vulkanInfo` union 成员 + `getVulkanImageInfo()` getter
- `BackendSemaphore`：新增 `BackendSemaphore(const VulkanSyncInfo&)` 构造器 + `VulkanSyncInfo vulkanSyncInfo` union 成员 + `getVulkanSync()` getter

---

### Phase 3：Vulkan 核心实现

> **目标**：实现完整的 Vulkan GPU 后端，全部位于 `src/gpu/vulkan/`。以 Metal 后端为蓝本，逐类对标实现。

#### 3A. 基础层

| # | 文件 | 说明 | 对标 Metal |
|---|------|------|------------|
| 3.1 | `VulkanDefines.h` | `VK_NO_PROTOTYPES` + `#include "volk.h"`，统一宏定义入口 | `MetalDefines.h` |
| 3.2 | `VulkanUtil.h/.cpp` | PixelFormat↔VkFormat 映射、VkResult→string、错误处理宏 `VK_CHECK` | `MetalUtil.h/.mm` |
| 3.3 | `VulkanCaps.h/.cpp` | 能力查询：VkPhysicalDeviceProperties/Features、格式支持、MSAA 采样数、硬件限制、扩展检测 | `MetalCaps.h/.mm` |

#### 3B. 设备与资源基类

| # | 文件 | 说明 | 对标 Metal |
|---|------|------|------------|
| 3.4 | `VulkanDevice.h/.cpp`（内部） | VkInstance/VkPhysicalDevice/VkDevice/VkQueue 创建 + VMA VmaAllocator 初始化 + volk 三步初始化（`volkInitialize` → `volkLoadInstance` → `volkLoadDevice`） | `MetalDevice.h/.mm` |
| 3.5 | `VulkanResource.h/.cpp` | `VulkanResource : ReturnNode` 基类，复用 ReturnQueue 延迟释放机制 | `MetalResource.h/.mm` |

#### 3C. GPU 资源

| # | 文件 | 说明 | 对标 Metal |
|---|------|------|------------|
| 3.6 | `VulkanTexture.h/.cpp` | `vmaCreateImage` 创建 VkImage + `onRelease()` 调用 `vmaDestroyImage` | `MetalTexture.h/.mm` |
| 3.7 | `VulkanBuffer.h/.cpp` | `vmaCreateBuffer` 创建 VkBuffer + persistent mapping + `onRelease()` 调用 `vmaDestroyBuffer` | `MetalBuffer.h/.mm` |
| 3.8 | `VulkanSampler.h/.cpp` | VkSampler 创建与缓存（FilterMode/MipmapMode → VkFilter/VkSamplerMipmapMode） | `MetalSampler.h/.mm` |

#### 3D. Shader 与管线

| # | 文件 | 说明 | 对标 Metal |
|---|------|------|------------|
| 3.9 | `VulkanShaderModule.h/.cpp` | 调用 `ShaderCompiler::PreprocessGLSL` + `CompileGLSLToSPIRV` → SPIR-V binary → `vkCreateShaderModule` | `MetalShaderModule.h/.mm` |
| 3.10 | `VulkanRenderPipeline.h/.cpp` | VkPipeline 创建 + 单 Set 固定 VkDescriptorSetLayout + VkPipelineLayout + VkPipelineCache | `MetalRenderPipeline.h/.mm` |

**Descriptor Set Layout 设计**：

```
Set 0:
  Binding 0: Vertex Uniform Block (UBO)        ← VERTEX_UBO_BINDING_POINT
  Binding 1: Fragment Uniform Block (UBO)       ← FRAGMENT_UBO_BINDING_POINT
  Binding 2..N: Combined Image Sampler          ← TEXTURE_BINDING_POINT_START + i
```

#### 3E. 命令模型

| # | 文件 | 说明 | 对标 Metal |
|---|------|------|------------|
| 3.11 | `VulkanCommandBuffer.h/.cpp` | VkCommandBuffer 封装 + per-frame CommandPool 重置 | `MetalCommandBuffer.h/.mm` |
| 3.12 | `VulkanCommandEncoder.h/.cpp` | 命令录制抽象层（对应 `CommandEncoder` 接口） | `MetalCommandEncoder.h/.mm` |
| 3.13 | `VulkanCommandQueue.h/.cpp` | VkQueue + `vkQueueSubmit` + staging buffer 管理（writeTexture/readTexture） | `MetalCommandQueue.h/.mm` |
| 3.14 | `VulkanRenderPass.h/.cpp` | `vkCmdBeginRenderPass` + draw calls + `vkCmdEndRenderPass` + 状态缓存（scissor/pipeline/vertex buffer/uniform/texture） | `MetalRenderPass.h/.mm` |

**CommandPool 管理**：per-frame 轮换，帧开始时 `vkResetCommandPool` 批量回收。

**Descriptor Pool 管理**：per-frame 重置，`vkResetDescriptorPool()` 一次性回收所有 descriptor set。

#### 3F. 同步与核心工厂

| # | 文件 | 说明 | 对标 Metal |
|---|------|------|------------|
| 3.15 | `VulkanSemaphore.h/.cpp` | `VK_KHR_timeline_semaphore` 封装 + completedFrameTime 追踪 | `MetalSemaphore.h/.mm` |
| 3.16 | `VulkanGPU.h/.cpp` | 核心工厂类，实现 GPU 全部纯虚方法，串联以上所有组件 | `MetalGPU.h/.mm` |

**VulkanGPU 职责**：

- `makeTexture()` → `VulkanTexture::Make()`
- `makeBuffer()` → `VulkanBuffer::Make()`
- `makeSampler()` → `VulkanSampler::Make()`
- `makeRenderPipeline()` → `VulkanRenderPipeline::Make()`
- `makeShaderModule()` → `VulkanShaderModule::Make()`
- `makeCommandEncoder()` → `VulkanCommandEncoder::Make()`
- `makeCommandQueue()` → `VulkanCommandQueue::Make()`
- `processUnreferencedResources()` → ReturnQueue 延迟释放
- VmaAllocator 生命周期管理

---

### Phase 4：窗口与上屏

> **目标**：完成 Swapchain 管理和 Win32 窗口渲染。

| # | 文件 | 说明 | 对标 Metal |
|---|------|------|------------|
| 4.1 | `VulkanSwapchainProxy.h/.cpp` | Swapchain image 代理，包装 VkImage 为 RenderTargetProxy | `MetalDrawableProxy.h/.mm` |
| 4.2 | `VulkanWindow.cpp` | `MakeFrom(HWND)` → `vkCreateWin32SurfaceKHR` → `vkCreateSwapchainKHR` → `onPresent` 调用 `vkQueuePresentKHR` | `MetalWindow.mm` |

**Swapchain 管理要点**：

- 窗口 resize 时检测尺寸变化，重建 swapchain
- `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` 时自动重建
- 使用 `VK_PRESENT_MODE_FIFO_KHR`（等价于 VSync，保证可用）

---

### Phase 5：Hello2D Win 端适配

> **目标**：Hello2D 通过 Vulkan 在 Windows 上正确渲染。

| # | 文件 | 说明 |
|---|------|------|
| 5.1 | `win/CMakeLists.txt` | Vulkan 模式下不链接 opengl32，条件编译 |
| 5.2 | `win/src/TGFXWindow.h` | `#ifdef TGFX_USE_VULKAN` → 使用 `VulkanWindow` 替代 `WGLWindow` |
| 5.3 | `win/src/TGFXWindow.cpp` | Vulkan 模式下用 `VulkanDevice::Make()` + `VulkanWindow::MakeFrom(HWND)` 创建渲染上下文 |
| 5.4 | 端到端验证 | 运行 Hello2D，确认 Vulkan 渲染上屏正常 |

**条件编译示例**：

```cpp
// win/src/TGFXWindow.h
#ifdef TGFX_USE_VULKAN
#include "tgfx/gpu/vulkan/VulkanDevice.h"
#include "tgfx/gpu/vulkan/VulkanWindow.h"
#else
#include "tgfx/gpu/opengl/wgl/WGLDevice.h"
#include "tgfx/gpu/opengl/wgl/WGLWindow.h"
#endif
```

---

### Phase 5B：测试框架与 Backend 切换

> **目标**：让现有测试框架能够在 Vulkan 后端下运行，通用测试用例自动适配，同时支持 Vulkan 专属测试。

#### 现有测试架构分析

当前测试框架使用**编译期多态**来切换 GPU 后端：

```
test/src/
├── utils/DevicePool.h          # 统一接口：DevicePool::Make() → Device
├── opengl/DevicePool.cpp       # GLDevice::Make()
├── metal/DevicePool.mm         # MetalDevice::Make()
├── webgl/DevicePool.cpp        # WebGLDevice::MakeFrom()
├── opengl/GLRenderTest.cpp     # OpenGL 专属测试
└── metal/MetalRenderPassTest.mm# Metal 专属测试
```

CMake 通过 `TGFX_USE_METAL` 决定排除 `test/src/opengl/` 还是 `test/src/metal/` 目录：

```cmake
if (TGFX_USE_METAL)
    list(REMOVE_ITEM TGFX_TEST_FILES ${TGFX_TEST_OPENGL_FILES})
else ()
    list(REMOVE_ITEM TGFX_TEST_FILES ${TGFX_TEST_METAL_FILES})
endif ()
```

**关键设计原则**：所有通用测试（CanvasTest、FilterTest、LayerTest 等 ~30 个文件）通过 `DevicePool::Make()` 和 `ContextScope` 获取设备，**完全不感知**底层是哪个 GPU 后端。因此只需新增 Vulkan 的 `DevicePool` 实现和扩展 CMake 筛选逻辑即可。

#### 5B.1 新建 `test/src/vulkan/DevicePool.cpp`

```cpp
#include "utils/DevicePool.h"
#include "tgfx/gpu/vulkan/VulkanDevice.h"

namespace tgfx {
thread_local std::shared_ptr<Device> cachedDevice = nullptr;

std::shared_ptr<Device> DevicePool::Make() {
  if (cachedDevice == nullptr) {
    cachedDevice = VulkanDevice::Make();
  }
  return cachedDevice;
}
}  // namespace tgfx
```

#### 5B.2 新建 `test/src/vulkan/VulkanRenderTest.cpp`（Vulkan 专属测试）

类比 `GLRenderTest.cpp` 和 `MetalRenderPassTest.mm`，编写 Vulkan 后端特有的测试：

```cpp
#include "utils/TestUtils.h"
// Vulkan-specific includes ...

namespace tgfx {

TGFX_TEST(VulkanRenderTest, DeviceCreation) {
  // Verify VulkanDevice::Make() returns a valid device
  auto device = DevicePool::Make();
  if (device == nullptr) {
    GTEST_SKIP() << "Vulkan backend not available";
  }
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  device->unlock();
}

TGFX_TEST(VulkanRenderTest, BasicClear) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Vulkan backend not available";
  }
  // Create surface, clear to a color, verify readback
  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::Red());
  EXPECT_TRUE(Baseline::Compare(surface, "VulkanRenderTest/BasicClear"));
}

// Additional Vulkan-specific tests: pipeline creation, buffer upload, etc.

}  // namespace tgfx
```

#### 5B.3 修改 `CMakeLists.txt` — 扩展测试后端筛选逻辑

**修改文件**：`CMakeLists.txt`（约第 684-695 行的测试文件筛选区域）

将现有的二选一逻辑扩展为三选一：

```cmake
file(GLOB_RECURSE TGFX_TEST_FILES test/src/*.*)

# Exclude backend-specific test files for backends not in use.
if (TGFX_USE_METAL)
    file(GLOB_RECURSE TGFX_TEST_OPENGL_FILES test/src/opengl/*.*)
    list(REMOVE_ITEM TGFX_TEST_FILES ${TGFX_TEST_OPENGL_FILES})
    file(GLOB_RECURSE TGFX_TEST_VULKAN_FILES test/src/vulkan/*.*)
    if (TGFX_TEST_VULKAN_FILES)
        list(REMOVE_ITEM TGFX_TEST_FILES ${TGFX_TEST_VULKAN_FILES})
    endif ()
elseif (TGFX_USE_VULKAN)
    file(GLOB_RECURSE TGFX_TEST_OPENGL_FILES test/src/opengl/*.*)
    if (TGFX_TEST_OPENGL_FILES)
        list(REMOVE_ITEM TGFX_TEST_FILES ${TGFX_TEST_OPENGL_FILES})
    endif ()
    file(GLOB_RECURSE TGFX_TEST_METAL_FILES test/src/metal/*.*)
    if (TGFX_TEST_METAL_FILES)
        list(REMOVE_ITEM TGFX_TEST_FILES ${TGFX_TEST_METAL_FILES})
    endif ()
else ()
    file(GLOB_RECURSE TGFX_TEST_METAL_FILES test/src/metal/*.*)
    list(REMOVE_ITEM TGFX_TEST_FILES ${TGFX_TEST_METAL_FILES})
    file(GLOB_RECURSE TGFX_TEST_VULKAN_FILES test/src/vulkan/*.*)
    if (TGFX_TEST_VULKAN_FILES)
        list(REMOVE_ITEM TGFX_TEST_FILES ${TGFX_TEST_VULKAN_FILES})
    endif ()
endif ()

file(GLOB_RECURSE TGFX_TEST_WEBGL_FILES test/src/webgl/*.*)
if (TGFX_TEST_WEBGL_FILES)
    list(REMOVE_ITEM TGFX_TEST_FILES ${TGFX_TEST_WEBGL_FILES})
endif ()
```

#### 5B.4 修改 `autotest.bat` — 支持 `USE_VULKAN` 参数

**修改文件**：`autotest.bat`

```bat
:: Configure CMake with Ninja
set "USE_SWIFTSHADER_FLAG="
set "USE_VULKAN_FLAG="
if "%1"=="USE_SWIFTSHADER" set "USE_SWIFTSHADER_FLAG=-DTGFX_USE_SWIFTSHADER=ON"
if "%1"=="USE_VULKAN" set "USE_VULKAN_FLAG=-DTGFX_USE_VULKAN=ON"
cmake -G Ninja %USE_SWIFTSHADER_FLAG% %USE_VULKAN_FLAG% -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release ..
```

#### 5B.5 修改 `autotest.sh` — 支持 `USE_VULKAN` 参数

**修改文件**：`autotest.sh`

```bash
if [[ "$1" == "USE_SWIFTSHADER" ]]; then
  cmake ... -DTGFX_USE_SWIFTSHADER=ON ...
elif [[ "$1" == "USE_METAL" ]]; then
  cmake ... -DTGFX_USE_METAL=ON ...
elif [[ "$1" == "USE_VULKAN" ]]; then
  cmake -DTGFX_USE_VULKAN=ON -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug ../
else
  cmake ... -DTGFX_BUILD_TESTS=ON ...
fi
```

#### 5B.6 截图基线（Baseline）策略

**现有机制回顾**：Baseline 系统通过 `test/baseline/version.json`（仓库）和 `test/baseline/.cache/version.json`（本地缓存）的版本号对比来判断是否跳过截图比较。这意味着：

- **首次运行 Vulkan 后端**：所有 baseline key（如 `CanvasTest/Clip`）没有对应的 Vulkan 渲染结果缓存，MD5 比较会失败
- **跨后端差异**：不同 GPU 后端对同一渲染内容的像素级输出可能存在微小差异（浮点精度、采样插值等）

**处理策略**：

1. **初期阶段**：Vulkan 后端首次运行测试时，通过 `UpdateBaseline` target 生成 Vulkan 专属的 baseline 缓存
2. **Baseline key 复用**：Vulkan 后端与 OpenGL/Metal 共享相同的 baseline key（如 `CanvasTest/Clip`），因为 TGFX 的软件渲染管线目标是跨后端一致性
3. **差异处理**：如果 Vulkan 和 OpenGL 存在不可避免的像素差异，通过 `/accept-baseline` 为 Vulkan 后端接受新的 baseline 版本号

**验证流程**：

```bash
# 1. 先用 UpdateBaseline 生成 Vulkan 后端的 baseline 缓存
cmake -G Ninja -DTGFX_USE_VULKAN=ON -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -B build
cmake --build build --target UpdateBaseline
./build/UpdateBaseline

# 2. 运行完整测试
cmake --build build --target TGFXFullTest
./build/TGFXFullTest --gtest_output=json:TGFXFullTest.json

# 3. 检查 test/out/ 中的差异图像，确认渲染正确性
```

---

### Phase 6：预留（后续，本次不实施）

| # | 任务 | 说明 | 优先级 |
|---|------|------|--------|
| 6.1 | Android Vulkan 接入 | CMake 补充 Android Vulkan 依赖、JNI 层条件编译 | 高 |
| 6.2 | SwiftShader Vulkan 测试 | 预编译 ICD 引入、离屏渲染测试模式 | 中 |
| 6.3 | VulkanExternalTexture | Android AHardwareBuffer 互操作 | 中 |
| 6.4 | VulkanHardwareTexture | 外部 VkImage 导入（对标 MetalHardwareTexture） | 低 |
| 6.5 | Pipeline Cache 持久化 | VkPipelineCache 序列化到磁盘加速启动 | 低 |
| 6.6 | VMA 碎片整理 | 长时间运行场景下的内存碎片整理 | 低 |

---

## 5. 文件变更清单

### 5.1 新建文件（~40 个）

```
include/tgfx/gpu/vulkan/
├── VulkanTypes.h                     # 公开类型定义
├── VulkanDevice.h                    # 公开 VulkanDevice : Device
└── VulkanWindow.h                    # 公开 VulkanWindow : Window

src/gpu/
├── ShaderCompiler.h                  # 公共 shader 编译工具声明
└── ShaderCompiler.cpp                # 平台无关实现

src/gpu/vulkan/
├── VulkanDefines.h                   # VK_NO_PROTOTYPES + volk 引入
├── VulkanUtil.h / .cpp               # 工具函数（格式映射、错误处理）
├── VulkanCaps.h / .cpp               # 能力查询
├── VulkanDevice.h / .cpp             # 内部设备管理
├── VulkanResource.h / .cpp           # 资源基类（ReturnNode）
├── VulkanTexture.h / .cpp            # 纹理资源（VMA）
├── VulkanBuffer.h / .cpp             # 缓冲区资源（VMA）
├── VulkanSampler.h / .cpp            # 采样器
├── VulkanShaderModule.h / .cpp       # Shader 模块
├── VulkanRenderPipeline.h / .cpp     # 渲染管线
├── VulkanCommandBuffer.h / .cpp      # 命令缓冲区
├── VulkanCommandEncoder.h / .cpp     # 命令编码器
├── VulkanCommandQueue.h / .cpp       # 命令队列
├── VulkanRenderPass.h / .cpp         # 渲染通道
├── VulkanSemaphore.h / .cpp          # 信号量（timeline semaphore）
├── VulkanGPU.h / .cpp                # 核心工厂类
├── VulkanSwapchainProxy.h / .cpp     # Swapchain image 代理
└── VulkanWindow.cpp                  # 窗口/Surface/Swapchain 管理

test/src/vulkan/
├── DevicePool.cpp                    # VulkanDevice::Make() 的 DevicePool 实现
└── VulkanRenderTest.cpp              # Vulkan 专属测试用例
```

### 5.2 修改文件（~10 个）

| 文件 | 变更内容 |
|------|----------|
| `DEPS` | 新增 Vulkan-Headers + volk + VMA 三条依赖 |
| `vendor.json` | 新增 shaderc(win) + SPIRV-Cross(win) 两个平台条目 |
| `CMakeLists.txt` | 新增 `TGFX_USE_VULKAN` 选项、互斥逻辑、Vulkan 源文件收集、include 路径、volk.c 编译、shaderc 子依赖同步；扩展测试后端筛选逻辑支持三选一 |
| `include/tgfx/gpu/Backend.h` | 新增 Vulkan 变体（VulkanTypes.h include + 3 个类各加构造器/union 成员/getter） |
| `src/gpu/metal/MetalShaderModule.h` | 调用 ShaderCompiler 公共函数 |
| `src/gpu/metal/MetalShaderModule.mm` | 提取公共代码后改为调用公共接口 |
| `win/src/TGFXWindow.h` | 条件编译 Vulkan 支持 |
| `win/src/TGFXWindow.cpp` | 条件编译 Vulkan 支持 |
| `autotest.bat` | 新增 `USE_VULKAN` 参数支持 |
| `autotest.sh` | 新增 `USE_VULKAN` 参数支持 |

---

## 6. 工作量估算

| Phase | 新建文件 | 修改文件 | 预估代码量 | 预估工时 |
|-------|----------|----------|------------|----------|
| Phase 0：依赖 + 构建 | 0 | 3 | ~200 行（配置） | 0.5 天 |
| Phase 1：Shader 提取 | 2 | 1 | ~200 行 | 0.5 天 |
| Phase 2：公开 API | 3 | 1 | ~300 行 | 0.5 天 |
| Phase 3A：基础层 | 6 | 0 | ~500 行 | 1 天 |
| Phase 3B：设备 + 资源基类 | 4 | 0 | ~600 行 | 1 天 |
| Phase 3C：GPU 资源 | 6 | 0 | ~800 行 | 1 天 |
| Phase 3D：Shader + 管线 | 4 | 0 | ~800 行 | 1.5 天 |
| Phase 3E：命令模型 | 8 | 0 | ~1500 行 | 2 天 |
| Phase 3F：同步 + 工厂 | 4 | 0 | ~800 行 | 1 天 |
| Phase 4：窗口 + 上屏 | 3 | 0 | ~500 行 | 1 天 |
| Phase 5：Hello2D 适配 | 0 | 3 | ~100 行 | 0.5 天 |
| Phase 5B：测试框架 | 2 | 3 | ~300 行 | 1 天 |
| **合计** | **~40** | **~10** | **~6600 行** | **~11.5 天** |

---

## 7. 关键依赖关系

```
Phase 0 (依赖+构建)
    ↓
Phase 1 (Shader提取) ──→ Phase 2 (API扩展)
                              ↓
Phase 3A (基础层) → 3B (设备) → 3C (资源) → 3D (管线) → 3E (命令) → 3F (工厂)
                                                                        ↓
                                                  Phase 4 (窗口) → Phase 5 (Hello2D)
                                                        ↓
                                                  Phase 5B (测试框架)
```

**可并行的工作**：

- Phase 3A（VulkanDefines/Util/Caps）与 Phase 2（公开 API）可并行
- Phase 3C（VulkanTexture/Buffer/Sampler）三者之间可并行
- Phase 3D 和 3E 有较强顺序依赖（管线先于命令模型）
- Phase 5B（测试框架）可在 Phase 3F 完成后与 Phase 4/5 并行推进（DevicePool + CMake 部分仅需 VulkanDevice 可用即可）

---

## 8. 风险与对策

| 风险 | 概率 | 影响 | 对策 |
|------|------|------|------|
| shaderc 在 Windows 上编译失败 | 中 | 阻塞 Phase 0 | shaderc 子依赖同步已有 Mac 上的成功先例；若 MSVC 编译问题，参考 shaderc 官方 Windows 构建指南 |
| volk 版本与 Vulkan-Headers 版本不匹配 | 低 | 编译错误 | 统一锁定到同一 Vulkan SDK 版本 tag |
| Timeline semaphore 在某些旧 GPU 上不支持 | 低 | 设备创建失败 | `VulkanDevice::Make()` 检查扩展支持，不支持时返回 nullptr，上层回退到 OpenGL |
| Descriptor pool 大小预估不准确 | 中 | 运行时分配失败 | 保守预分配（MaxDrawCalls × MaxDescriptors），失败时动态扩容 |
| Swapchain resize 导致竞争条件 | 低 | 崩溃或画面撕裂 | 在 resize 时 `vkDeviceWaitIdle` 确保安全；后续可优化为 fence-based |
| MetalShaderModule 提取破坏 Metal 后端 | 低 | Metal 回归失败 | Phase 1 完成后在 Mac 上运行完整测试验证 |
| MSVC 对 `volk.c` 的 C99 兼容性问题 | 低 | 编译警告/错误 | volk 官方支持 MSVC，必要时添加特定编译选项 |
| Windows 开发机无 Vulkan 驱动 | 中 | 无法验证 | 使用 NVIDIA/AMD/Intel 独立 GPU 的开发机；后续 Phase 6.2 引入 SwiftShader Vulkan 作为软件后备 |

---

## 附录 A：VMA 内存策略映射

| TGFX 资源 | VMA Usage | VMA Flags | 说明 |
|-----------|-----------|-----------|------|
| Render target texture | `VMA_MEMORY_USAGE_GPU_ONLY` | — | GPU 独占读写 |
| Sampled texture (atlas) | `VMA_MEMORY_USAGE_GPU_ONLY` | — | 初始上传后 GPU 只读 |
| Staging buffer (upload) | `VMA_MEMORY_USAGE_CPU_TO_GPU` | `MAPPED_BIT` | CPU 写 → GPU 读 |
| Staging buffer (readback) | `VMA_MEMORY_USAGE_GPU_TO_CPU` | `MAPPED_BIT` | GPU 写 → CPU 读 |
| Uniform buffer | `VMA_MEMORY_USAGE_CPU_TO_GPU` | `MAPPED_BIT` | 每帧 CPU 填充 |
| Vertex/Index buffer (dynamic) | `VMA_MEMORY_USAGE_CPU_TO_GPU` | `MAPPED_BIT` | 每帧 CPU 生成 |

## 附录 B：PixelFormat↔VkFormat 映射

| tgfx::PixelFormat | VkFormat |
|-------------------|----------|
| ALPHA_8 | `VK_FORMAT_R8_UNORM` |
| GRAY_8 | `VK_FORMAT_R8_UNORM` |
| RG_88 | `VK_FORMAT_R8G8_UNORM` |
| RGBA_8888 | `VK_FORMAT_R8G8B8A8_UNORM` |
| BGRA_8888 | `VK_FORMAT_B8G8R8A8_UNORM` |
| DEPTH24_STENCIL8 | `VK_FORMAT_D24_UNORM_S8_UINT`（fallback: `VK_FORMAT_D32_SFLOAT_S8_UINT`） |

## 附录 C：与 Metal 后端的完整对标关系

| Metal 文件 | Vulkan 对标文件 | 说明 |
|------------|-----------------|------|
| `MetalGPU.h/.mm` | `VulkanGPU.h/.cpp` | 核心工厂 |
| `MetalDevice.h/.mm` | `VulkanDevice.h/.cpp` | 设备管理 |
| `MetalCaps.h/.mm` | `VulkanCaps.h/.cpp` | 能力查询 |
| `MetalCommandQueue.h/.mm` | `VulkanCommandQueue.h/.cpp` | 命令队列 |
| `MetalCommandEncoder.h/.mm` | `VulkanCommandEncoder.h/.cpp` | 命令编码 |
| `MetalCommandBuffer.h/.mm` | `VulkanCommandBuffer.h/.cpp` | 命令缓冲区 |
| `MetalRenderPass.h/.mm` | `VulkanRenderPass.h/.cpp` | 渲染通道 |
| `MetalRenderPipeline.h/.mm` | `VulkanRenderPipeline.h/.cpp` | 渲染管线 |
| `MetalShaderModule.h/.mm` | `VulkanShaderModule.h/.cpp` | Shader 模块 |
| `MetalTexture.h/.mm` | `VulkanTexture.h/.cpp` | 纹理 |
| `MetalBuffer.h/.mm` | `VulkanBuffer.h/.cpp` | 缓冲区 |
| `MetalSampler.h/.mm` | `VulkanSampler.h/.cpp` | 采样器 |
| `MetalSemaphore.h/.mm` | `VulkanSemaphore.h/.cpp` | 信号量 |
| `MetalResource.h/.mm` | `VulkanResource.h/.cpp` | 资源基类 |
| `MetalDrawableProxy.h/.mm` | `VulkanSwapchainProxy.h/.cpp` | 上屏代理 |
| `MetalWindow.mm` | `VulkanWindow.cpp` | 窗口管理 |
| `MetalHardwareTexture.h/.mm` | （Phase 6.4 预留） | 外部纹理导入 |
| `MetalExternalTexture.h` | （Phase 6.3 预留） | 外部纹理声明 |
| `MetalDefines.h` | `VulkanDefines.h` | 宏定义 |
| `MetalUtil.h/.mm` | `VulkanUtil.h/.cpp` | 工具函数 |
