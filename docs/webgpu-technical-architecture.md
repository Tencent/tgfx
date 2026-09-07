# tgfx WebGPU 后端技术体系文档

## 1. 概述

tgfx WebGPU 后端将 tgfx 2D 渲染引擎编译为 WebAssembly (WASM)，通过浏览器的 WebGPU API 实现 GPU 加速渲染。上层 Canvas API 与原生端（Metal/Vulkan/OpenGL）完全一致，底层通过 WebGPU 提交绘制命令到浏览器的 GPU 进程。

### 技术栈全景

```
┌─────────────────────────────────────────────────────────────────────┐
│                          用户浏览器                                    │
├─────────────────────────────────────────────────────────────────────┤
│  HTML/JS 层                                                          │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │  index.html                                                     │ │
│  │    • <canvas id="tgfx-canvas"> — 渲染目标                       │ │
│  │    • navigator.gpu.requestAdapter() → requestDevice()           │ │
│  │    • TGFXModule({ preinitializedWebGPUDevice: device })         │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                              │                                       │
│                              ▼ WGPUDevice (预初始化)                  │
│  Emscripten 桥接层                                                   │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │  • emscripten_webgpu_get_device() — 获取 JS 传入的 Device       │ │
│  │  • html5_webgpu.h — Canvas → WGPUSurface 绑定                  │ │
│  │  • Asyncify / EM_ASYNC_JS — 异步 Promise 同步化                 │ │
│  │  • Virtual FS — 文件系统模拟 (资源加载/结果输出)                  │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                              │                                       │
│                              ▼                                       │
│  tgfx WebGPU 后端 (WASM)                                            │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │  WebGPUDevice / WebGPUGPU         — 设备管理                    │ │
│  │  WebGPUWindow / WebGPUDrawableProxy — Surface/帧管理            │ │
│  │  WebGPUShaderModule               — Shader 编译管线             │ │
│  │  WebGPURenderPipeline             — 渲染管线状态                 │ │
│  │  WebGPURenderPass                 — 绘制命令编码                 │ │
│  │  WebGPUBuffer / WebGPUTexture     — GPU 资源管理                │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                              │                                       │
│                              ▼ WGPUCommandBuffer                     │
│  浏览器 WebGPU 实现 (Dawn / wgpu-native)                             │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │  验证层 → 命令序列化 → IPC → GPU 进程 → 驱动层                   │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                              │                                       │
│                              ▼                                       │
│                           [ GPU 硬件 ]                               │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 2. WebAssembly (WASM) 编译体系

### 2.1 Emscripten 工具链

tgfx 使用 Emscripten 将 C++ 代码交叉编译为 WASM：

```bash
emcmake cmake -B build -DTGFX_USE_WEBGPU=ON
cmake --build build
```

产物：
| 文件 | 作用 |
|------|------|
| `app.wasm` | 编译后的 tgfx + 应用逻辑（二进制） |
| `app.js` | Emscripten 生成的胶水代码（初始化 WASM、WebGPU 绑定） |
| `app.data` | 预打包的资源文件（通过 `--preload-file` 嵌入） |

### 2.2 关键编译标志

| 标志 | 含义 |
|------|------|
| `-sWASM=1` | 输出 WebAssembly 格式 |
| `-sUSE_WEBGPU=1` | 启用 Emscripten 的 WebGPU C 绑定层（映射到浏览器 JS API） |
| `-sASYNCIFY=1` | 启用 Asyncify 变换（详见 §3） |
| `-sALLOW_MEMORY_GROWTH=1` | 允许 WASM 线性内存动态扩展 |
| `-sINITIAL_MEMORY=256MB` | 初始堆大小 |
| `-sMAXIMUM_MEMORY=2GB` | 最大堆大小（WASM 32-bit 上限约 4GB） |
| `-sSTACK_SIZE=1MB` | 栈大小 |
| `-sMODULARIZE=1 -sEXPORT_ES6=1` | 输出为 ES6 模块工厂函数 |
| `-sENVIRONMENT=web,worker` | 目标运行环境 |
| `-lembind` | 启用 C++/JS 双向绑定 |

### 2.3 内存模型

```
WASM 线性内存 (256MB → 2GB)
┌─────────────────────────────────────────────────┐
│  Stack (1MB)  │  Heap (动态增长)                  │
│               │  ┌────────────────────────────┐  │
│               │  │ tgfx 对象 / Shader 编译缓存 │  │
│               │  │ Staging Buffer 数据         │  │
│               │  │ 纹理像素数据 (上传前)        │  │
│               │  └────────────────────────────┘  │
└─────────────────────────────────────────────────┘
```

WASM 线性内存与 GPU 内存完全隔离。数据需通过 `wgpuQueueWriteBuffer` / `wgpuQueueWriteTexture` 从 WASM 堆拷贝到 GPU。

### 2.4 函数导出

```cmake
EXPORTED_FUNCTIONS=['_RunAllTests', '_RunTest']
EXPORTED_RUNTIME_METHODS=['FS', 'ccall', 'cwrap', 'GL']
```

JavaScript 通过 `module.ccall('RunAllTests', 'number', [], [], {async: true})` 调用导出的 C 函数。`{async: true}` 参数允许该调用在遇到 Asyncify 挂起点时返回 Promise。

---

## 3. Asyncify — 异步同步化机制

### 3.1 为什么需要 Asyncify

WebGPU 的多个 API 是异步的（返回 Promise）：
- `buffer.mapAsync()` — 将 GPU buffer 映射到 CPU 可读
- `requestAdapter()` / `requestDevice()` — 设备初始化

但 tgfx 的 API 设计是同步的（如 `readPixels()` 期望立即返回像素数据）。Asyncify 通过编译器变换解决这个矛盾。

### 3.2 工作原理

```
C++ 代码调用 webgpu_buffer_map_sync()
         │
         ▼ Asyncify 检测到 async 调用
┌─────────────────────────────────────┐
│  1. 保存当前 WASM 调用栈到堆上       │  ← Asyncify unwind
│  2. 返回到 JS event loop            │
│  3. 浏览器处理 GPU 操作              │
│  4. Promise resolve                  │
│  5. 恢复 WASM 栈，从暂停点继续执行   │  ← Asyncify rewind
└─────────────────────────────────────┘
         │
         ▼
C++ 代码继续执行（看起来像同步返回）
```

### 3.3 实现代码

```cpp
// WebGPUBuffer.cpp
EM_ASYNC_JS(int, webgpu_buffer_map_sync, (WGPUBuffer bufHandle, size_t size), {
  var bufferWrapper = WebGPU.mgrBuffer.objects[bufHandle];
  try {
    await bufferWrapper.object.mapAsync(1 /* GPUMapMode.READ */, 0, size);
    return 0;  // 成功
  } catch (e) {
    return 1;  // 失败
  }
});
```

`EM_ASYNC_JS` 宏生成一个 C 函数，其内部实现是异步 JavaScript。Asyncify 在这个调用点自动执行栈保存/恢复。

### 3.4 性能优化：排除不参与异步的代码

```cmake
ASYNCIFY_REMOVE=['shaderc*', 'glslang*', 'spv::*', 'spvtools*', 'tint::*', ...]
```

Asyncify 会对所有函数插桩（检查是否需要 unwind/rewind），这会增大 WASM 体积约 50% 并降低性能。通过 `ASYNCIFY_REMOVE` 排除纯 CPU 计算的库（shader 编译器），可以：
1. **减小 WASM 体积**
2. **避免破坏 shader 编译器的执行状态**（这些库内部使用深层递归，Asyncify 插桩可能导致栈损坏）

---

## 4. HTML Canvas 集成

### 4.1 Canvas 绑定流程

```
HTML: <canvas id="tgfx-canvas" width="1024" height="1024"></canvas>
         │
         │ CSS selector "#tgfx-canvas"
         ▼
C++: WebGPUWindow::MakeFrom("#tgfx-canvas", device)
         │
         │ 1. wgpuCreateInstance()
         │ 2. WGPUSurfaceDescriptorFromCanvasHTMLSelector { selector = "#tgfx-canvas" }
         │ 3. wgpuInstanceCreateSurface(instance, &desc)
         ▼
WGPUSurface ← 绑定到 HTML Canvas 元素
         │
         │ wgpuSurfaceConfigure() 配置：
         │   • format: BGRA8Unorm
         │   • usage: RenderAttachment
         │   • presentMode: Fifo
         │   • alphaMode: Premultiplied
         ▼
每帧: wgpuSurfaceGetCurrentTexture() → 获取当前帧的 backbuffer
```

### 4.2 帧循环

```
┌──── 每帧渲染 ────────────────────────────────────────────┐
│                                                            │
│  1. wgpuSurfaceGetCurrentTexture()  → 获取当前 Surface 纹理 │
│  2. 创建 TextureView → 包装为 RenderTarget                  │
│  3. tgfx Canvas 绘制命令 → 编码为 WebGPU 命令              │
│  4. wgpuQueueSubmit() → 提交命令到 GPU                     │
│  5. [Emscripten 环境下自动 present]                        │
│     [原生环境下调用 wgpuSurfacePresent()]                   │
│  6. 释放 TextureView                                      │
│                                                            │
└────────────────────────────────────────────────────────────┘
```

### 4.3 Canvas 尺寸感知

```cpp
int canvasWidth, canvasHeight;
emscripten_get_canvas_element_size(canvasSelector.c_str(), &canvasWidth, &canvasHeight);
```

通过 Emscripten API 获取 Canvas 的像素尺寸，用于配置 Surface extent。

---

## 5. Shader 编译管线

### 5.1 三级转换流程

```
┌──────────────────────────────────────────────────────────────────┐
│  输入: GLSL (ES 3.0 风格, tgfx 内部 shader 格式)                 │
│  #version 300 es                                                  │
│  uniform sampler2D uTexture;                                      │
│  in vec2 vTexCoord;                                               │
│  out vec4 fragColor;                                              │
│  void main() { fragColor = texture(uTexture, vTexCoord); }        │
└──────────────────────────────────────────┬───────────────────────┘
                                           │
                  ┌────────────────────────▼─────────────────────────┐
                  │  Step 1: GLSL 预处理 (正则变换)                    │
                  │                                                    │
                  │  • #version 300 es → #version 450                  │
                  │  • uniform sampler2D X →                           │
                  │      uniform texture2D X;                          │
                  │      uniform sampler X_Sampler;                    │
                  │  • texture(X, coord) →                             │
                  │      texture(sampler2D(X, X_Sampler), coord)       │
                  │  • 添加 layout(binding=N) 给 UBO                   │
                  │  • 添加 layout(location=N) 给 in/out               │
                  │  • 移除 precision 声明                              │
                  └────────────────────────┬─────────────────────────┘
                                           │ Vulkan-compatible GLSL 4.50
                                           ▼
                  ┌──────────────────────────────────────────────────┐
                  │  Step 2: GLSL → SPIR-V (shaderc)                 │
                  │                                                    │
                  │  target: Vulkan 1.1                                │
                  │  optimization: performance                         │
                  │  auto_bind_uniforms: true                          │
                  │  auto_map_locations: true                          │
                  └────────────────────────┬─────────────────────────┘
                                           │ SPIR-V 二进制 (std::vector<uint32_t>)
                                           ▼
                  ┌──────────────────────────────────────────────────┐
                  │  Step 3: SPIR-V → WGSL (Tint)                    │
                  │                                                    │
                  │  tint::spirv::reader::Read(spirv, options)        │
                  │    options.allow_non_uniform_derivatives = true    │
                  │  tint::wgsl::writer::Generate(program, options)   │
                  │                                                    │
                  │  输出: WGSL 源代码字符串                            │
                  └────────────────────────┬─────────────────────────┘
                                           │ WGSL string
                                           ▼
                  ┌──────────────────────────────────────────────────┐
                  │  wgpuDeviceCreateShaderModule(device, &desc)      │
                  │  → WGPUShaderModule (GPU 侧编译后的 shader)        │
                  └──────────────────────────────────────────────────┘
```

### 5.2 为什么需要 Texture/Sampler 分离

GLSL 中 `sampler2D` 是 **combined image sampler**（纹理 + 采样器绑定在一起）。但 WGSL 规范要求纹理和采样器是**独立资源**，各自有独立的 binding：

```wgsl
// WGSL
@group(0) @binding(0) var uTexture: texture_2d<f32>;
@group(0) @binding(1) var uTexture_Sampler: sampler;
```

tgfx 在 GLSL 预处理阶段通过正则匹配自动完成这个拆分。

### 5.3 为什么使用 Tint 而非直接编译 WGSL

1. tgfx 所有后端共享同一套 GLSL shader 源码
2. GLSL → WGSL 的直接翻译非常复杂（语法、语义差异大）
3. GLSL → SPIR-V（shaderc，成熟工具）+ SPIR-V → WGSL（Tint，Google 官方）是最可靠的路径
4. SPIR-V 作为中间表示还能做跨后端优化

---

## 6. GPU 设备获取策略

### 6.1 JS 侧预初始化

WebGPU 设备获取是异步的（`navigator.gpu.requestAdapter()` 返回 Promise），在 C++ 同步模型中无法直接调用。解决方案：

```javascript
// index.html (JS 侧)
const adapter = await navigator.gpu.requestAdapter();
const device = await adapter.requestDevice({
  requiredFeatures: [...],
  requiredLimits: {...}
});

// 将 device 传入 WASM 模块
const module = await TGFXModule({
  preinitializedWebGPUDevice: device
});
```

```cpp
// WebGPUDevice.cpp (C++ 侧)
std::shared_ptr<WebGPUDevice> WebGPUDevice::Make() {
  auto wgpuDevice = emscripten_webgpu_get_device();  // 获取 JS 传入的 Device
  // ...
}
```

### 6.2 Emscripten WebGPU 绑定内部

```
JavaScript Device 对象
         │
         │ Emscripten 内部: WebGPU.mgrDevice.create(device)
         │ → 分配整数句柄
         ▼
WGPUDevice (C 侧 uint32_t 句柄)
         │
         │ 每次 wgpuDevice*() C API 调用
         │ → Emscripten glue code 查表还原 JS 对象
         │ → 调用对应的 JS WebGPU API
         ▼
浏览器 WebGPU 实现
```

---

## 7. Buffer 管理与数据传输

### 7.1 写入路径（CPU → GPU）

```
应用写入数据
    │
    ▼
WebGPUBuffer (Vertex/Index/Uniform)
    │ 分配 CPU staging buffer (malloc)
    │ memcpy 数据到 staging
    ▼
CommandQueue::submit() 时：
    │ wgpuQueueWriteBuffer(queue, gpuBuffer, offset, data, size)
    │ → 数据从 WASM 堆复制到浏览器 GPU 进程
    ▼
GPU Buffer 就绪
```

**设计选择**：不使用 `buffer.mapAsync(WRITE)`（需要异步），而是用 `wgpuQueueWriteBuffer` 直接写入（同步 API，内部由浏览器实现高效传输）。

### 7.2 读回路径（GPU → CPU）

```
readPixels() 请求
    │
    ▼
1. 创建 Staging Buffer (MAP_READ usage)
2. wgpuCommandEncoderCopyTextureToBuffer() — GPU 侧拷贝
3. wgpuQueueSubmit() — 提交命令
    │
    ▼ [异步等待 GPU 完成]
4. webgpu_buffer_map_sync() — Asyncify 包装的 buffer.mapAsync()
    │  ← WASM 栈暂停
    │  ← JS event loop 运行
    │  ← GPU 完成 → Promise resolve
    │  ← WASM 栈恢复
    ▼
5. wgpuBufferGetConstMappedRange() — 获取映射指针
6. memcpy 到应用缓冲区
7. wgpuBufferUnmap()
```

### 7.3 行对齐处理

WebGPU 规范要求 `copyTextureToBuffer` 的每行字节数必须是 **256 字节对齐**：

```cpp
uint32_t bytesPerRow = (width * bytesPerPixel + 255) & ~255u;  // 向上对齐到 256
```

读回后需要按行去除 padding。

---

## 8. 渲染管线架构

### 8.1 Pipeline 状态对象

WebGPU 要求在创建 Pipeline 时指定所有状态（不像 OpenGL 可以动态修改）：

```
WGPURenderPipeline = f(
    ShaderModule (vertex + fragment),
    VertexState (buffer layouts, attributes),
    PrimitiveState (topology, cull, frontFace),
    DepthStencilState,
    MultisampleState,
    FragmentState (color targets, blend),
    PipelineLayout (bind group layouts)
)
```

### 8.2 双 Topology 变体

WebGPU 将 primitive topology 纳入 Pipeline 状态（不可动态切换）。tgfx 为每个 Pipeline 创建两个变体：

```
Pipeline (TriangleList)  ← 大多数绘制使用
Pipeline (TriangleStrip) ← 部分优化路径使用
```

运行时根据 draw call 的 topology 选择对应变体。

### 8.3 Bind Group 管理

WebGPU 使用 Bind Group（类似 Vulkan Descriptor Set）管理资源绑定：

```
BindGroupLayout:
  binding 0: Uniform Buffer (VertexUBO)
  binding 1: Uniform Buffer (FragmentUBO)
  binding 2: Texture View
  binding 3: Sampler
  binding 4: Texture View (如有第二个纹理)
  binding 5: Sampler
  ...

BindGroup: 实际资源 → Layout 的绑定实例
```

tgfx 使用 **dirty flag** 机制延迟创建 Bind Group——仅在资源绑定发生变化时重新创建。

---

## 9. 与其他后端的关键差异

| 特性 | Metal | Vulkan | OpenGL | **WebGPU** |
|------|-------|--------|--------|------------|
| 运行环境 | 操作系统进程 | 操作系统进程 | 操作系统进程 | **浏览器沙箱** |
| 编译产物 | 原生二进制 | 原生二进制 | 原生二进制 | **WASM + JS** |
| GPU 访问 | 直接驱动调用 | 直接驱动调用 | 直接驱动调用 | **IPC 到 GPU 进程** |
| Shader 语言 | MSL | SPIR-V | GLSL | **WGSL** |
| Shader 转换 | GLSL→SPIRV→MSL | GLSL→SPIRV | 直接 | **GLSL→SPIRV→WGSL** |
| Buffer readback | 同步 | 同步 | 同步 | **异步 (Asyncify)** |
| 窗口系统 | CAMetalLayer | VkSurface | EGL/WGL | **HTML Canvas** |
| 多线程 | 支持 | 支持 | 有限 | **单线程** (WASM 限制) |
| 内存模型 | 统一/分离 | 设备/主机 | 驱动管理 | **完全隔离** |

### 9.1 核心架构差异

**安全模型**：WebGPU 运行在浏览器沙箱内，所有 GPU 操作经过严格验证（防止越界访问、确保着色器终止等），这带来了额外开销但保证了安全性。

**异步模型**：WebGPU 强制异步（fence/readback 通过 Promise），这是浏览器 GPU 进程隔离的必然结果。tgfx 通过 Asyncify 将其伪装为同步，但有性能代价。

**单线程**：WASM 默认单线程执行（SharedArrayBuffer + Atomics 可以多线程，但当前未使用），所有 GPU 命令在同一线程提交。

---

## 10. 测试与自动化

### 10.1 测试执行流程

```
开发者运行 node run_test.js
    │
    ▼
1. 启动 http-server 托管编译产物
2. Puppeteer 启动 Chrome (headless)
   --enable-unsafe-webgpu
   --enable-features=Vulkan (backend)
   --experimental-wasm-stack-switching
    │
    ▼
3. Chrome 加载 index.html
4. JS 初始化 WebGPU Device → 传入 WASM
5. WASM 执行 RunAllTests()
   (Google Test 框架, 397 个用例)
    │
    ▼
6. 测试输出到 console (Puppeteer 捕获)
7. 截图结果写入虚拟文件系统
8. JS 端打包为 zip 下载
    │
    ▼
9. Puppeteer 通过 CDP 截获下载的 zip
10. 解压到项目目录 (test/out/)
11. MD5 基线比对
```

### 10.2 当前测试状态

- **总测试数**: 397
- **通过**: 101 (25%)
- **失败**: 296 (均不崩溃)
- **主要失败原因**: readPixels 异步对接未完成（截图对比无法获取像素数据）

---

## 11. 未来演进方向

### 11.1 JSPI (JavaScript Promise Integration)

WebAssembly 提案 [JSPI](https://github.com/aspect-build/aspect-workflows/wiki/JSPI) 是 Asyncify 的原生替代方案：

- **Asyncify**: 编译器变换，增大 WASM 体积，有性能开销
- **JSPI**: 运行时原生支持 Promise 挂起/恢复，零额外体积

当 JSPI 在主流浏览器稳定后（Chrome 已实验性支持），可以移除 Asyncify 依赖。

### 11.2 多线程支持

通过 `SharedArrayBuffer` + Web Workers 可以实现多线程 WASM。未来可能用于：
- 后台 shader 编译（当前 Tint 转换在主线程阻塞）
- 资源上传并行化

### 11.3 WebGPU 规范演进

- **Subgroups**: GPU 波前级操作（性能优化）
- **Bindless**: 大量纹理不通过 Bind Group（简化资源管理）
- **Ray Tracing**: 光线追踪加速结构
