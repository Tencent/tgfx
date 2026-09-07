# WebGPU 技术概述

## 1. 定义与定位

WebGPU 是由 W3C GPU for the Web 工作组制定的图形与计算 API 规范，旨在为 Web 平台提供对现代 GPU 硬件的低开销、高性能访问能力。该规范于 2023 年 4 月进入 Candidate Recommendation 阶段，Chrome 113 成为首个默认启用 WebGPU 的浏览器。

WebGPU 并非对某一原生 API 的直接映射，而是在 Vulkan、Metal 和 Direct3D 12 三者的共同语义基础上抽象设计的。其设计目标包括：

- 提供与原生图形 API 对等的 GPU 能力（通用计算、多通道渲染、间接绘制等）
- 在浏览器安全沙箱约束下保证内存安全与着色器终止性
- 取代 WebGL，解决其状态机模型的性能瓶颈与功能局限

## 2. 架构模型

### 2.1 进程隔离

WebGPU 运行于浏览器的多进程架构之中：

```
渲染进程 (Renderer Process)
  │  JavaScript / WASM 调用 WebGPU API
  │  命令编码在此完成（轻量级，无系统调用）
  │
  ▼  IPC (序列化的命令流)
GPU 进程 (GPU Process)
  │  命令验证 → 翻译为原生 API 调用
  │  Vulkan / Metal / D3D12 驱动交互
  │
  ▼
GPU 硬件
```

所有 GPU 资源的实际创建与命令提交均发生在 GPU 进程中。渲染进程仅持有句柄（opaque handle），无法直接访问 GPU 内存地址。这一设计保证了即使着色器代码存在越界访问，也不会危及宿主系统的内存安全。

### 2.2 对象模型

WebGPU 的对象层次结构如下：

```
Navigator.gpu
  └─ GPUAdapter (物理设备抽象)
       └─ GPUDevice (逻辑设备，资源与命令的所有者)
            ├─ GPUBuffer
            ├─ GPUTexture → GPUTextureView
            ├─ GPUSampler
            ├─ GPUShaderModule
            ├─ GPUBindGroupLayout → GPUBindGroup
            ├─ GPUPipelineLayout → GPURenderPipeline / GPUComputePipeline
            ├─ GPUCommandEncoder → GPUCommandBuffer
            └─ GPUQueue (唯一，submit + writeBuffer + writeTexture)
```

每个 `GPUDevice` 仅拥有一个 `GPUQueue`（当前规范版本），所有命令提交通过该队列串行化执行。

## 3. 命令提交模型

WebGPU 采用显式的命令录制-提交模型（Command Buffer Pattern），与 Vulkan/Metal/D3D12 一致：

```javascript
const encoder = device.createCommandEncoder();

const pass = encoder.beginRenderPass(renderPassDescriptor);
pass.setPipeline(pipeline);
pass.setVertexBuffer(0, vertexBuffer);
pass.draw(vertexCount);
pass.end();

const commandBuffer = encoder.finish();
device.queue.submit([commandBuffer]);
```

**关键语义**：
- `CommandEncoder` 为一次性对象，`finish()` 后不可复用
- 命令录制阶段不发生任何 GPU 侧操作
- `queue.submit()` 是唯一触发 GPU 执行的入口
- 提交后命令的执行顺序由 GPU 保证（within a queue, submissions are ordered）

## 4. 渲染管线

### 4.1 管线状态对象

WebGPU 将所有渲染状态编译为不可变的管线对象（Pipeline State Object, PSO）。创建时必须指定：

| 组件 | 内容 |
|------|------|
| Vertex State | 顶点缓冲区布局、着色器入口点 |
| Primitive State | 图元拓扑（triangle-list/strip）、正面方向、裁剪模式 |
| Depth/Stencil State | 深度测试函数、模板操作、写入掩码 |
| Multisample State | 采样数、alpha-to-coverage |
| Fragment State | 颜色目标格式、混合方程 |

与 OpenGL 的全局状态机不同，WebGPU 管线对象在创建时即完成着色器编译与状态验证，运行时切换管线的开销极低。

### 4.2 管线拓扑的不可变性

`primitiveTopology` 是管线创建时的编译期常量。若应用需要同时使用 `triangle-list` 和 `triangle-strip`，须创建两个独立的管线对象。这与 Vulkan 的 `VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY`（Vulkan 1.3 引入）不同——WebGPU 选择了更保守的设计以确保跨平台一致性。

## 5. 着色器语言：WGSL

WebGPU 的唯一着色器语言为 WGSL（WebGPU Shading Language）。其设计原则包括：

- **可分析性**：语法无歧义，可在有限时间内完成词法/语法分析
- **终止性保证**：禁止无界循环，所有循环必须有编译器可推断的终止条件（或由运行时超时机制兜底）
- **内存安全**：所有数组访问在运行时进行越界检查（clamping 或 zeroing）
- **确定性**：浮点运算遵循 IEEE 754，未定义行为被最小化

WGSL 示例：

```wgsl
@group(0) @binding(0) var<uniform> mvp: mat4x4<f32>;
@group(0) @binding(1) var diffuseTexture: texture_2d<f32>;
@group(0) @binding(2) var diffuseSampler: sampler;

struct VertexOutput {
  @builtin(position) position: vec4<f32>,
  @location(0) texCoord: vec2<f32>,
}

@vertex
fn vs_main(@location(0) pos: vec3<f32>, @location(1) uv: vec2<f32>) -> VertexOutput {
  var out: VertexOutput;
  out.position = mvp * vec4<f32>(pos, 1.0);
  out.texCoord = uv;
  return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
  return textureSample(diffuseTexture, diffuseSampler, in.texCoord);
}
```

### 5.1 资源绑定模型

WGSL 使用 `@group(N) @binding(M)` 显式声明资源绑定位置。WebGPU 支持最多 4 个 Bind Group（`@group(0)` 至 `@group(3)`），每个 Group 可绑定任意数量的 buffer、texture、sampler。

与 GLSL 的 combined image sampler 不同，WGSL 要求 texture 和 sampler 作为**独立资源**声明。这与 Metal Shading Language 和 HLSL 的模型一致。

## 6. 资源管理

### 6.1 Buffer

```javascript
const buffer = device.createBuffer({
  size: 1024,
  usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
});
```

Buffer 的 `usage` 标志在创建时不可变。常用标志：

| 标志 | 用途 |
|------|------|
| `VERTEX` | 顶点缓冲区 |
| `INDEX` | 索引缓冲区 |
| `UNIFORM` | Uniform Block |
| `STORAGE` | 可读写存储缓冲区 |
| `COPY_SRC` / `COPY_DST` | 拷贝操作的源/目标 |
| `MAP_READ` / `MAP_WRITE` | CPU 可映射 |

**重要约束**：`MAP_READ` 和 `MAP_WRITE` 不能与 `VERTEX`、`UNIFORM`、`STORAGE` 等 GPU 使用标志组合。这意味着 CPU 可读的缓冲区只能作为 staging buffer 使用，数据传输需要 `copyBufferToBuffer` 中转。

### 6.2 Texture

WebGPU 纹理格式分为三类：

| 类别 | 示例 | 用途 |
|------|------|------|
| 普通格式 | `rgba8unorm`, `bgra8unorm`, `rg16float` | 颜色附件、采样 |
| 深度模板格式 | `depth24plus`, `depth24plus-stencil8`, `depth32float` | 深度测试 |
| 压缩格式 | `bc1-rgba-unorm`, `etc2-rgb8unorm`, `astc-4x4-unorm` | 纹理压缩（需要 feature 开启） |

纹理的 `TextureView` 是对纹理子资源（mip level、array layer、aspect）的引用，用于绑定到管线或作为 render attachment。

### 6.3 数据上传策略

WebGPU 提供两种 CPU→GPU 数据传输方式：

1. **`queue.writeBuffer(buffer, offset, data)`** — 立即将 ArrayBuffer 内容排入队列，由实现选择最优传输路径。适用于频繁更新的小数据（uniform、动态顶点等）。

2. **Staging Buffer + Copy** — 创建 `MAP_WRITE` 缓冲区 → `mapAsync(WRITE)` → 写入 → `unmap()` → `copyBufferToBuffer(staging, dst)`。适用于大批量数据的初始化上传。

推荐使用方式 1（`queue.writeBuffer`），因其由浏览器实现内部优化（如 ring buffer、page mapping），开发者无需管理 staging buffer 生命周期。

## 7. 同步模型

### 7.1 隐式同步

在单队列模型下，WebGPU 提供以下隐式保证：

- 同一队列中先 `submit` 的命令先执行（submission order）
- `writeBuffer`/`writeTexture` 的写入在后续 `submit` 的命令中可见
- Render pass 内部的 draw call 按录制顺序执行

开发者无需手动插入 barrier 或 fence。资源的 usage transition（layout transition in Vulkan）由实现自动管理。

### 7.2 显式同步：mapAsync

`buffer.mapAsync()` 是 WebGPU 中唯一的显式 CPU-GPU 同步点：

```javascript
await buffer.mapAsync(GPUMapMode.READ);
const data = new Float32Array(buffer.getMappedRange());
// 读取 data
buffer.unmap();
```

`mapAsync` 返回 Promise，resolve 时保证：
- 所有之前提交的使用该 buffer 的命令已执行完毕
- buffer 内容已从 GPU 回传到 CPU 可读内存

### 7.3 queue.onSubmittedWorkDone()

```javascript
await device.queue.onSubmittedWorkDone();
```

等待队列中所有已提交工作完成。这是一个全局同步点，通常用于性能测量或资源回收时机判断。

## 8. 错误处理

WebGPU 采用分层错误处理机制：

| 层级 | 时机 | 示例 |
|------|------|------|
| 验证错误 (Validation) | API 调用时（同步） | 非法 usage 组合、超出 limits |
| 内存不足 (Out-of-memory) | 资源创建时 | GPU 显存耗尽 |
| 设备丢失 (Device Lost) | 异步 | 驱动崩溃、GPU 挂起、后台标签页被回收 |

错误捕获通过 Error Scope 机制：

```javascript
device.pushErrorScope('validation');
// ... 执行可能出错的操作
const error = await device.popErrorScope();
if (error) console.error(error.message);
```

设备丢失通过 `device.lost` Promise 监控：

```javascript
device.lost.then(info => {
  console.error(`Device lost: ${info.reason} — ${info.message}`);
});
```

## 9. 与 WebGL 的对比

| 维度 | WebGL 2.0 | WebGPU |
|------|-----------|--------|
| API 模型 | 全局状态机（OpenGL ES 3.0） | 命令缓冲区（Vulkan/Metal 风格） |
| 着色器 | GLSL ES 3.00 | WGSL |
| 通用计算 | 不支持 | Compute Shader ✓ |
| 多线程命令录制 | 不支持 | 规范预留（当前单 encoder） |
| 管线状态 | 运行时动态修改 | 预编译不可变 PSO |
| 资源绑定 | Texture Unit（全局槽位） | Bind Group（显式声明） |
| 内存管理 | 驱动全权管理 | 应用显式指定 usage 和生命周期 |
| 渲染通道 | 隐式（每次 draw 即一次提交） | 显式 Render Pass（定义 load/store 行为） |
| 错误处理 | `gl.getError()` 轮询 | Error Scope + Device Lost Promise |
| 多后端适配 | ANGLE (OpenGL→D3D/Vulkan/Metal) | 原生多后端（Dawn/wgpu） |

## 10. 实现现状

### 10.1 浏览器支持

| 浏览器 | 状态 | 底层实现 |
|--------|------|---------|
| Chrome / Edge | 默认启用 (113+) | Dawn (C++) |
| Firefox | Nightly 可用 | wgpu (Rust) |
| Safari | 预览版可用 | WebKit 自研 |

### 10.2 原生实现

WebGPU 的 C API (`webgpu.h`) 允许非浏览器环境使用相同接口：

- **Dawn** (Google): C++ 实现，Chrome 的 WebGPU 后端，也可独立使用
- **wgpu-native** (Mozilla): Rust 实现，Firefox 的 WebGPU 后端，导出 C API

### 10.3 已知限制（截至规范 v1）

- 单队列：不支持异步计算/传输队列
- 无 ray tracing：光线追踪需等待扩展提案
- 无 bindless：所有资源必须通过 Bind Group 绑定
- 无 mesh shader：仅支持传统顶点/片段管线
- 纹理格式受限：不支持所有原生格式（如 RGB 不含 A 的格式）

## 11. 参考文献

1. W3C. *WebGPU Specification*. https://www.w3.org/TR/webgpu/
2. W3C. *WebGPU Shading Language (WGSL)*. https://www.w3.org/TR/WGSL/
3. Beaufort, F. *WebGPU Fundamentals*. https://webgpufundamentals.org/
4. Google. *Dawn — WebGPU implementation*. https://dawn.googlesource.com/dawn
5. Mozilla. *wgpu — Safe and portable GPU abstraction*. https://github.com/gfx-rs/wgpu
