# PAG 游戏引擎插件技术方案深度对比评估报告

## 一、背景与目标

libpag 是一个 2D 动效渲染库，底层使用 tgfx 渲染引擎实现图形渲染。现需将 libpag 封装为 PAG UE
插件，提供给游戏项目使用。游戏项目对性能要求极为严苛，PAG UE 插件需要有极致的渲染性能。

**两个候选方案**：

- **方案 1（纹理共享）**：tgfx 切换到与 UE 相同的渲染后端，libpag 渲染到独立纹理，通过平台共享机制
  将纹理传递给 UE 侧 UMG Widget 显示。
- **方案 2（UE RHI 后端）**：将 UE RHI 作为独立渲染后端在 tgfx 中适配，tgfx 直接调用 UE RHI 接口
  实现渲染。

**技术前提**：tgfx 已支持（或即将支持）所有主流渲染后端（OpenGL、Metal、Vulkan、D3D12、WebGPU）。

---

## 二、架构现状

### tgfx 渲染后端架构

tgfx 采用三层抽象：

```
公共 API 层:   Surface, Canvas, Image, Context, Device, Window
GPU 抽象层:    GPU, Texture, RenderPass, CommandEncoder, CommandQueue, RenderPipeline
后端实现层:    OpenGL (GLGPU) | Metal (MetalGPU) | [Vulkan/D3D12/WebGPU]
```

核心接口 `GPU`（`include/tgfx/gpu/GPU.h`）是所有后端的入口，需实现约 15 个纯虚方法：
`createBuffer`、`createTexture`、`createSampler`、`createShaderModule`、`createRenderPipeline`、
`createCommandEncoder`、`importBackendTexture`、`importBackendRenderTarget` 等。

**Shader 策略**：统一用 GLSL（Vulkan 风格 `#version 450`）编写，Metal 后端通过 shaderc（GLSL ->
SPIR-V）+ SPIRV-Cross（SPIR-V -> MSL）实时编译。

**当前后端代码量**：OpenGL ~10,800 行（含 6 个平台适配层），Metal ~4,500 行。

### libpag 与 tgfx 集成方式

libpag 通过 `Drawable` 抽象层与 tgfx 对接：

```
PAGPlayer::flush()
  -> PAGSurface::draw()
    -> Drawable::getDevice()        // 获取 tgfx::Device
    -> Drawable::getSurface()       // 获取 tgfx::Surface
    -> Canvas 绑定并录制绘制命令
    -> Context::flushAndSubmit()    // 提交 GPU 命令
    -> Drawable::present()          // 呈现
```

libpag 已有完善的外部纹理集成能力：

- `PAGSurface::MakeFrom(BackendTexture)` -- 可渲染到外部纹理
- `PAGSurface::MakeFrom(BackendRenderTarget)` -- 可渲染到外部 RenderTarget
- 支持 `forAsyncThread` 参数创建独立 GPU 上下文做异步渲染
- 支持 `BackendSemaphore` 进行跨上下文同步

---

## 三、方案架构详解

### 方案 1：纹理共享方案

**原理**：tgfx 使用与 UE 相同的原生 GPU API，libpag 渲染到独立纹理，通过平台共享机制传递给 UE。

**工作流**：

```
+---------------------------------------------------------------------------+
|                          UE Game Thread                                    |
|   PAGPlayer::setProgress() -> 触发异步渲染                                 |
+-------------------------------------+-------------------------------------+
                                      |
+-------------------------------------v-------------------------------------+
|                     PAG 渲染线程 (独立线程)                                 |
|  tgfx::Device (D3D12/Vulkan/Metal) -> 绘制到共享纹理 -> Semaphore 信号     |
+--------------+--------------------------+----------------------------------+
               | 共享纹理                  | Semaphore
+--------------v--------------------------v----------------------------------+
|                         UE Render Thread                                   |
|  1. 等待 PAG 信号量 (GPU-GPU 同步)                                         |
|  2. ID3D12DynamicRHI::RHICreateTexture2DFromResource(ID3D12Resource*)     |
|     IVulkanDynamicRHI::RHICreateTexture2DFromResource(VkImage)            |
|  3. FSlateTexture2DRHIRef -> UMG Widget 显示                              |
+---------------------------------------------------------------------------+
```

**各平台纹理共享机制**：

| 平台 | UE 使用的 API | tgfx 需切换到 | 共享机制 |
|------|--------------|--------------|---------|
| Windows | D3D12 | D3D12 | `ID3D12Resource` SharedHandle / `IDXGIKeyedMutex` |
| Windows | D3D11 | D3D11 | `ID3D11Texture2D` SharedHandle |
| macOS/iOS | Metal | Metal | `MTLSharedEvent` + 同一 `MTLDevice` 纹理直接共享 |
| Android | Vulkan | Vulkan | `AHardwareBuffer` / `VkExternalMemory` |
| Android | OpenGL ES | OpenGL ES | `EGLImage` / `AHardwareBuffer` |
| 主机平台 | 平台专有 API | 不支持 | 不适用 |

**关键 UE RHI 接口**（基于源码分析）：

- **D3D12**: `ID3D12DynamicRHI::RHICreateTexture2DFromResource(Format, Flags, ClearValue, ID3D12Resource*)`
- **Vulkan**: `IVulkanDynamicRHI::RHICreateTexture2DFromResource(Format, SizeX, SizeY, NumMips, NumSamples, VkImage, Flags, ...)`
- **Metal**: `IMetalDynamicRHI::RHICreateTexture2DFromCVMetalTexture(Format, Flags, ClearValue, CVMetalTextureRef)`

### 方案 2：UE RHI 后端方案

**原理**：在 tgfx 中新增 UE RHI 作为独立渲染后端，tgfx 所有 GPU 操作直接翻译为 UE RHI 调用。

**工作流**：

```
+---------------------------------------------------------------------------+
|                          UE Game Thread                                    |
|   PAGPlayer::setProgress() -> ENQUEUE_RENDER_COMMAND                      |
+-------------------------------------+-------------------------------------+
                                      |
+-------------------------------------v-------------------------------------+
|                         UE Render Thread                                   |
|  tgfx UE RHI Backend:                                                     |
|    UERHIDevice -> wraps GDynamicRHI                                       |
|    UERHIGPU -> 调用 FRHICommandList 接口                                   |
|    UERHIRenderPass -> RHIBeginRenderPass / RHIEndRenderPass               |
|                                                                            |
|  所有 tgfx 资源 = UE RHI 资源                                              |
|    UERHITexture wraps FRHITexture2D                                       |
|    UERHIBuffer wraps FRHIBuffer                                           |
|    UERHIPipeline wraps FGraphicsPipelineState                             |
+---------------------------------------------------------------------------+
```

**接口映射关系**（基于 `RHIContext.h` 分析）：

| tgfx 接口 | UE RHI 接口 |
|-----------|-------------|
| `RenderPass::setViewport()` | `RHISetViewport(MinX, MinY, MinZ, MaxX, MaxY, MaxZ)` |
| `RenderPass::setScissorRect()` | `RHISetScissorRect(bEnable, MinX, MinY, MaxX, MaxY)` |
| `RenderPass::setPipeline()` | `RHISetGraphicsPipelineState(GraphicsState, StencilRef, ...)` |
| `RenderPass::draw()` | `RHIDrawPrimitive(BaseVertexIndex, NumPrimitives, NumInstances)` |
| `RenderPass::drawIndexed()` | `RHIDrawIndexedPrimitive(IndexBuffer, BaseVertexIndex, ...)` |
| `CommandEncoder::beginRenderPass()` | `RHIBeginRenderPass(FRHIRenderPassInfo, Name)` |
| `CommandEncoder::endRenderPass()` | `RHIEndRenderPass()` |

**需实现的核心类**（参照 Metal 后端）：

| 类 | 职责 | 预估代码量 |
|----|------|----------|
| `UERHIDevice` | 继承 Device，封装 UE RHI 上下文 | ~150 行 |
| `UERHIGPU` | 继承 GPU，所有资源创建通过 UE RHI | ~500 行 |
| `UERHICommandQueue` | 继承 CommandQueue，映射到 FRHICommandList | ~300 行 |
| `UERHICommandEncoder` | 继承 CommandEncoder，命令编码 | ~250 行 |
| `UERHIRenderPass` | 继承 RenderPass，映射绘制调用 | ~500 行 |
| `UERHIRenderPipeline` | 继承 RenderPipeline，PSO 创建 | ~400 行 |
| `UERHITexture` | 继承 Texture，封装 FRHITexture2D | ~200 行 |
| `UERHIBuffer` | 继承 GPUBuffer，封装 FRHIBuffer | ~150 行 |
| `UERHISampler` | 继承 Sampler，封装 FRHISamplerState | ~80 行 |
| `UERHISemaphore` | 继承 Semaphore，封装 FRHIGPUFence | ~80 行 |
| `UERHIShaderModule` | 继承 ShaderModule，GLSL->SPIR-V->HLSL | ~600 行 |
| `UERHICaps` | GPU 能力查询 | ~200 行 |
| UEDrawable + UMG Widget | libpag 集成层 | ~500 行 |
| **合计** | | **~3,800 行** |

---

## 四、多维度深度对比

### 4.1 渲染性能

| 指标 | 方案 1（纹理共享） | 方案 2（UE RHI 后端） | 差异分析 |
|------|-------------------|---------------------|---------|
| **GPU 上下文数量** | 2 个独立上下文 | 1 个共享上下文 | 方案 2 节省 GPU 资源 |
| **GPU Command Queue** | 独立 Queue 竞争时间片 | 融入 UE 统一 Queue | 方案 2 调度更优 |
| **纹理内存** | 共享纹理 1 份 | UE 纹理 1 份 | 相当 |
| **GPU-GPU 同步** | Semaphore 等待 0.5-2ms | 无额外同步 | 方案 2 优势明显 |
| **帧延迟** | 至少 1 帧 | 可同帧渲染 | 方案 2 延迟最低 |
| **上下文切换** | 每帧 GPU 上下文切换 | 无切换 | 方案 2 更优 |
| **CPU 开销** | PAG 线程 + 同步管理 | 仅 Render Thread | 方案 2 更简单 |

**性能量化**：

- 方案 1 跨上下文同步在移动端约 0.5-2ms
- 方案 1 独立 Queue 可能损失 5-15% GPU 利用率
- 高帧率场景（120fps，帧预算 8.3ms），方案 2 优势更显著

**结论**：方案 2 在性能上有决定性优势。纹理共享方案的跨上下文同步、GPU 资源冗余、Command Queue
竞争是不可消除的性能开销。

### 4.2 跨平台兼容性

| 平台 | 方案 1（纹理共享） | 方案 2（UE RHI 后端） |
|------|-------------------|---------------------|
| **Windows (D3D12)** | 需 D3D12 后端 + 共享纹理机制 | UE RHI 自动适配 |
| **Windows (D3D11)** | 需 D3D11 后端 + 共享纹理机制 | UE RHI 自动适配 |
| **macOS/iOS (Metal)** | 已支持，可行 | UE RHI 自动适配 |
| **Android (Vulkan)** | 需 Vulkan 后端 + VkExternalMemory | UE RHI 自动适配 |
| **Android (GLES)** | 已支持，但 UE 主推 Vulkan | UE RHI 自动适配 |
| **主机 (PS5/Xbox/Switch)** | 完全不可行，无平台 API 访问权限 | **通过 UE RHI 天然支持** |
| **纹理共享机制** | 每个平台需单独实现和维护 | **无需纹理共享** |

**结论**：方案 1 在每个平台上需要单独实现纹理共享和同步机制，且主机平台完全不可行。方案 2 通过 UE
RHI 一次适配即覆盖所有 UE 支持的平台，包括主机平台。

### 4.3 架构合理性

| 维度 | 方案 1 | 方案 2 |
|------|--------|--------|
| **职责分离** | tgfx 完全独立，松耦合 | tgfx 依赖 UE RHI，紧耦合 |
| **抽象层级** | PAG -> tgfx -> 原生 API -> 共享 -> UE | PAG -> tgfx -> UE RHI |
| **错误隔离** | PAG 崩溃不影响 UE | PAG 崩溃可能影响 UE |
| **调试便利性** | 可独立调试 PAG | 需在 UE 环境调试 |
| **线程模型** | PAG 独立线程，与 UE 并行 | 必须在 UE Render Thread |

**关键洞察**：UE RHI 作为后端在架构上是合理的 -- UE RHI 接口（`IRHICommandContext`）与 tgfx GPU
抽象层（`GPU`、`RenderPass`）高度同构，类似于 Metal/Vulkan 作为 tgfx 后端，UE RHI 作为后端符合设计
意图。方案 2 层级更少，数据路径更短。

### 4.4 可扩展性

| 维度 | 方案 1 | 方案 2 |
|------|--------|--------|
| **UE 版本升级** | 仅更新纹理导入接口，影响小 | 需跟随 RHI 变化，但接口相对稳定 |
| **新 GPU API 支持** | tgfx 独立实现 | 依赖 UE 支持 |
| **非 UE 平台复用** | 代码完全复用 | UE RHI 后端无法复用 |
| **多引擎支持** | 同架构可适配 Unity 等 | 仅适用于 UE |
| **主机平台** | 无法访问主机 API | UE RHI 已封装，天然支持 |

### 4.5 可维护性

| 维度 | 方案 1 | 方案 2 |
|------|--------|--------|
| **代码复杂度** | 纹理共享层 + 同步层 + 多平台适配 | 单一 RHI 后端适配层 |
| **依赖管理** | tgfx 独立构建 | 需链接 UE RHI 头文件 |
| **问题定位** | 可分别定位 PAG/UE 问题 | 问题可能跨边界 |
| **API 稳定性** | 原生 API 稳定 | UE RHI 可能变化（但实际接口演进缓慢） |

### 4.6 工程复杂度与代码量

**方案 1 实现清单**：

```
[tgfx 侧 - 假设 D3D12/Vulkan 后端已存在]
1. D3D12 共享纹理 (D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)    ~300 行
2. Vulkan 共享纹理 (VK_KHR_external_memory)                          ~400 行
3. D3D12/Vulkan Fence 同步                                           ~300 行

[UE 侧]
4. 纹理导入适配层                                                     ~500 行
5. 同步等待                                                           ~300 行
6. UPAGTexture (参考 FMediaTextureResource)                           ~800 行
7. UMG Widget                                                         ~400 行

总计：~3,000 行
```

**方案 2 实现清单**：

```
[tgfx 侧]
1. UERHIDevice                                                        ~150 行
2. UERHIGPU                                                           ~500 行
3. UERHICommandQueue                                                  ~300 行
4. UERHICommandEncoder                                                ~250 行
5. UERHIRenderPass                                                    ~500 行
6. UERHIRenderPipeline                                                ~400 行
7. UERHITexture / Buffer / Sampler                                    ~400 行
8. UERHIShaderModule (GLSL -> SPIR-V -> HLSL 或预编译)                 ~600 行
9. UERHICaps                                                          ~200 行

[UE 侧]
10. UEDrawable                                                        ~200 行
11. UMG Widget                                                        ~300 行

总计：~3,800 行
```

**对比**：方案 1 约 3,000 行 vs 方案 2 约 3,800 行，差距不大。但方案 1 的代码分散在多个平台适配层，
维护成本更高。

### 4.7 Shader 处理

| 维度 | 方案 1 | 方案 2 |
|------|--------|--------|
| **D3D12 平台** | 需 GLSL->SPIR-V->HLSL，或直接维护双份 HLSL shader | GLSL->SPIR-V->HLSL，通过 UE shader 编译管线处理 |
| **Metal 平台** | 已有 GLSL->SPIR-V->MSL | 同上，UE RHI 处理 |
| **Vulkan 平台** | GLSL->SPIR-V 直接使用 | 同上，UE RHI 处理 |

两个方案在 shader 处理上复杂度相当。tgfx 已有 shaderc + SPIRV-Cross 管线，UE RHI 后端可复用这套
管线生成各平台所需的 shader 字节码。

---

## 五、风险评估

### 方案 1 风险

| 风险 | 等级 | 说明 |
|------|------|------|
| D3D12 Resource Barrier 状态管理 | **高** | 跨上下文共享纹理时需精确管理资源状态转换，错误会导致渲染异常或崩溃 |
| Vulkan 外部内存类型匹配 | **高** | 不同 GPU 支持的外部内存类型不同，兼容性复杂 |
| 跨 GPU 厂商纹理共享兼容性 | 中 | NVIDIA/AMD/Intel/Adreno/Mali 行为可能有差异 |
| 跨上下文死锁 | 中 | 需严格的同步协议，实现不当易死锁 |
| 主机平台完全不可行 | **致命** | 无法访问主机平台 API |

### 方案 2 风险

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| UE RHI 版本变化 | 中 | 抽象适配层，跟踪 UE 更新；实际接口演进缓慢 |
| Render Thread 阻塞 | 中 | 优化 PAG 渲染性能，考虑分帧处理 |
| GLSL->HLSL 转换完整性 | 中 | SPIRV-Cross 成熟度高，预编译方案作为备选 |
| UE 插件更新延迟 | 低 | 自动化测试覆盖 |

---

## 六、场景适用性

| 场景 | 推荐方案 | 理由 |
|------|---------|------|
| 高帧率游戏 (120fps) | **方案 2** | 帧预算紧张（8.3ms），无跨上下文开销 |
| 纯 UI 层动效 | 两者均可 | 方案 2 性能更优 |
| 与 3D 场景深度融合 | **方案 2** | 可访问 UE 深度缓冲，直接共享 RenderTarget |
| 主机平台 (PS5/Xbox) | **方案 2** | 方案 1 无法访问主机 API |
| 多引擎项目 (UE + Unity) | 混合 | UE 用方案 2，Unity 用纹理共享（详见第十章） |

---

## 七、tgfx 与 UE RHI 接口同构性

方案 2 可行的核心依据在于 tgfx GPU 抽象层与 UE RHI 的接口高度同构：

| tgfx | UE RHI |
|------|--------|
| `GPU` | `FDynamicRHI / GDynamicRHI` |
| `Device` | `FDynamicRHI` (lockContext/unlock) |
| `CommandQueue` | `FRHICommandListImmediate` |
| `CommandEncoder` | `FRHICommandList` |
| `RenderPass` | `FRHIRenderPassInfo + IRHICommandContext` |
| `RenderPipeline` | `FGraphicsPipelineState` |
| `Texture` | `FRHITexture` |
| `GPUBuffer` | `FRHIBuffer` |
| `Sampler` | `FRHISamplerState` |
| `Semaphore` | `FRHIGPUFence` |
| `ShaderModule` | `FRHIVertexShader / FRHIPixelShader` |

这种同构性使得适配工作主要是接口映射，而非架构重构。

---

## 八、最终选型结论

### 推荐方案：方案 2（UE RHI 后端）

**决策理由**：

1. **性能优势决定性**：游戏场景对性能要求极高，方案 2 消除所有跨上下文开销（同步延迟 0.5-2ms、Queue
   竞争 5-15% GPU 利用率损失、上下文切换），在 120fps 高帧率场景下优势尤为显著。

2. **实现复杂度相当**：方案 1 约 3,000 行 vs 方案 2 约 3,800 行，差距不大。方案 2 代码集中在单一适配
   层，维护性更好。

3. **主机平台支持**：方案 1 完全无法支持 PS5/Xbox/Switch，方案 2 通过 UE RHI 天然覆盖所有 UE 支持
   的平台。

4. **接口高度同构**：tgfx GPU 抽象层与 UE RHI `IRHICommandContext` 接口几乎一一对应，映射自然，
   实现风险低。

5. **UE 生态融合**：可利用 UE 的资源管理、线程调度、调试工具，未来可扩展更深度的集成（如共享深度缓冲、
   参与 UE 渲染排序）。

### 方案 1 保留价值

方案 1（纹理共享）在以下场景仍有价值：

- PAG 需在 Unity 引擎中使用（Unity 无 RHI 抽象层，只能采用纹理共享，详见第十章）
- 团队对原生 GPU API 更熟悉
- 需要完全独立的 PAG 调试环境

---

## 九、综合评分

| 维度 | 权重 | 方案 1 得分 | 方案 2 得分 | 说明 |
|------|------|-----------|-----------|------|
| 渲染性能 | 30% | 6 | **10** | 方案 2 零拷贝零同步，性能优势决定性 |
| 跨平台兼容性 | 25% | 5 | **10** | 方案 2 一次适配覆盖所有平台含主机 |
| 工程复杂度 | 15% | 7 | **8** | 两者代码量接近，方案 2 维护性更好 |
| 架构合理性 | 15% | 8 | **9** | 方案 2 层级更少，tgfx 与 UE RHI 同构 |
| 可维护性 | 10% | 6 | **8** | 方案 2 单一适配层 vs 多平台共享机制 |
| 可扩展性 | 5% | **8** | 6 | 方案 1 可复用到其他引擎 |
| **加权总分** | **100%** | **6.35** | **9.20** | **方案 2 胜出** |

---

## 十、Unity 引擎适用性分析

### 10.1 核心问题：Unity 能否复制 UE RHI 后端方案？

**结论：不能。** Unity 的渲染插件架构与 UE 有本质差异，无法将 Unity 接口作为 tgfx 的独立渲染后端。

### 10.2 UE RHI vs Unity Native Plugin 接口对比

UE RHI 提供**完整的跨平台渲染抽象层**，一套统一接口覆盖所有平台的 GPU 操作：

| 能力 | UE RHI 接口 |
|------|------------|
| 纹理创建 | `RHICreateTexture(FRHITextureCreateDesc)` |
| Buffer 创建 | `RHICreateBuffer(Size, Usage, Access)` |
| Shader 创建 | `RHICreateVertexShader(Code, Hash)` |
| PSO 创建 | `RHICreateGraphicsPipelineState(Initializer)` |
| RenderPass | `RHIBeginRenderPass` / `RHIEndRenderPass` |
| 绘制调用 | `RHIDrawPrimitive` / `RHIDrawIndexedPrimitive` |
| 状态设置 | `RHISetViewport` / `RHISetScissorRect` |

Unity 的 Native Plugin 接口**不提供跨平台抽象层**，只暴露原生 GPU API 对象的句柄：

| Unity 接口 | 暴露内容 | 使用方式 |
|-----------|---------|---------|
| `IUnityGraphicsD3D12v7` | `GetDevice()` -> `ID3D12Device*`<br>`GetCommandQueue()` -> `ID3D12CommandQueue*`<br>`CommandRecordingState()` -> `ID3D12GraphicsCommandList*` | 拿到原生 D3D12 对象后，插件自行调用 D3D12 API |
| `IUnityGraphicsVulkanV2` | `Instance()` -> `VkDevice`, `VkQueue`, `VkInstance`<br>`CommandRecordingState()` -> `VkCommandBuffer` | 拿到原生 Vulkan 对象后，插件自行调用 Vulkan API |
| `IUnityGraphicsMetal` | `MetalDevice()` -> `id<MTLDevice>` | 拿到原生 Metal 对象后，插件自行调用 Metal API |

**关键差异**：

| 对比维度 | UE RHI | Unity Native Plugin |
|---------|--------|---------------------|
| **抽象层级** | 完整的跨平台 RHI 抽象 | 无抽象，直接暴露原生 API 句柄 |
| **资源创建** | `RHICreateTexture()` 统一接口 | 需调用 `ID3D12Device::CreateCommittedResource()` 等原生 API |
| **PSO 创建** | `RHICreateGraphicsPipelineState()` 统一接口 | 需调用 `ID3D12Device::CreateGraphicsPipelineState()` 等原生 API |
| **绘制命令** | `RHIDrawPrimitive()` 统一接口 | 需调用 `ID3D12GraphicsCommandList::DrawInstanced()` 等原生 API |
| **跨平台** | 写一次代码覆盖所有平台 | 每个 GPU API 需要完全独立的实现 |
| **可作为 tgfx 后端** | 可以，接口与 tgfx GPU 抽象层同构 | 不可以，缺少统一抽象层 |

### 10.3 Unity 场景下的 PAG 集成方案

由于 Unity 不提供 RHI 抽象层，PAG 在 Unity 中应采用**纹理共享方案**（即 UE 场景下的方案 1）：

```
+----------------------------------------------------------------------------+
|                          Unity Main Thread                                  |
|   PAGPlayer::setProgress() -> 触发异步渲染                                  |
+--------------------------------------+-------------------------------------+
                                       |
+--------------------------------------v-------------------------------------+
|                     PAG 渲染线程 (独立线程)                                  |
|  tgfx::Device (D3D12/Vulkan/Metal) -> 绘制到共享纹理 -> Semaphore 信号      |
+---------------+--------------------------+---------------------------------+
                | 共享纹理                  | Semaphore
+---------------v--------------------------v---------------------------------+
|                         Unity Render Thread                                 |
|  CommandBuffer.IssuePluginEvent 回调:                                       |
|    D3D12: IUnityGraphicsD3D12v7::GetDevice() -> 导入共享纹理                 |
|    Vulkan: IUnityGraphicsVulkanV2::Instance() -> 导入共享纹理                |
|    Metal: IUnityGraphicsMetal::MetalDevice() -> 导入共享纹理                 |
|  -> Texture2D 显示                                                          |
+----------------------------------------------------------------------------+
```

Unity 的 Native Plugin 回调机制（`IssuePluginEvent`）天然适合纹理共享模式 -- 它的设计目的就是让插件
在渲染线程中使用原生 GPU API 做自己的事。tgfx 已有（或将有）D3D12/Vulkan/Metal 后端可直接复用。

### 10.4 为什么 Unity 接口不能作为 tgfx 后端

如果尝试将 Unity 接口作为 tgfx 后端，面临以下问题：

1. **无统一抽象层**：Unity 的 `IUnityGraphicsD3D12v7`、`IUnityGraphicsVulkanV2`、
   `IUnityGraphicsMetal` 是三套完全不同的接口，没有公共基类或统一的资源创建/绘制 API。适配这三套
   接口实质等于编写三个独立后端（D3D12、Vulkan、Metal），这正是 tgfx 自身已经在做的事。

2. **接口能力有限**：Unity Native Plugin 接口只提供基础的设备句柄和命令列表访问，不提供：
   - 跨平台的 Shader 创建接口
   - 跨平台的 PSO 创建接口
   - 跨平台的资源状态管理
   - 跨平台的 RenderPass 抽象

3. **回调驱动模型不兼容**：Unity 通过 `IssuePluginEvent` 回调驱动插件渲染，插件无法主动发起渲染，
   而 tgfx 的后端模型是主动式的（由 tgfx 控制渲染节奏）。

### 10.5 UE 与 Unity 集成策略总结

| | UE 插件 | Unity 插件 |
|--|--------|-----------|
| **推荐方案** | UE RHI 后端（方案 2） | 纹理共享（方案 1） |
| **核心原因** | UE RHI 提供完整跨平台抽象，可作为 tgfx 后端 | Unity 无跨平台抽象，只暴露原生 API 句柄 |
| **tgfx 后端** | 新增 `UERHI` 一个后端 | 复用已有 D3D12/Vulkan/Metal 后端 |
| **纹理传递** | 不需要（共享同一 RHI 上下文） | 通过 `IssuePluginEvent` 回调导入共享纹理 |
| **主机平台** | 支持（UE RHI 封装） | 需单独适配（Unity 有主机平台的 Native Plugin 接口） |
| **性能** | 最优（零拷贝零同步） | 次优（有跨上下文同步开销） |
| **代码复用** | UE RHI 后端代码无法复用到 Unity | 纹理共享层 + 原生后端可在两个引擎间复用 |

### 10.6 同时支持 UE 和 Unity 的整体架构

```
                              libpag
                                |
                              tgfx
                                |
            +-------------------+-------------------+
            |                   |                   |
      UE RHI 后端         D3D12 后端           Vulkan/Metal 后端
      (方案 2)            (原生)               (原生)
            |                   |                   |
       UE 引擎            Unity (D3D12)        Unity (Vulkan/Metal)
       (直接渲染)          (纹理共享)           (纹理共享)
```

同时支持两个引擎时，tgfx 的多后端架构发挥最大价值：

- **UE 场景**：使用 UE RHI 后端，获得最优性能
- **Unity 场景**：复用 D3D12/Vulkan/Metal 原生后端，通过纹理共享集成
- **共享部分**：libpag 核心逻辑、tgfx 渲染管线、Shader 编译管线完全共用
- **引擎差异**：仅在集成层（Drawable/Widget）和后端选择上有区别
