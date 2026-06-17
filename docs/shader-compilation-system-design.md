# TGFX Shader 编译体系设计

*借鉴 Unreal Engine 5 的三阶段 Shader 编译架构*

---

## 摘要

现代 GPU 渲染管线中，从高级着色语言到 GPU 可执行微码的完整路径包含三个本质不同的阶段：**语言翻译**（Source Translation）、**驱动编译**（Driver Compilation）、**管线状态缓存**（Pipeline State Caching）。Unreal Engine 5（以下简称 UE5）围绕这三个阶段构建了业界最完善的 Shader 编译体系——DDC（Derived Data Cache）、ShaderCompileWorker、PSO Cache——支撑了百万级变体、数十个平台的工业化生产。

TGFX 的 Shader 规模远小于 UE5（37 个 Processor / ~200 个变体 vs UE5 的数千个 `.usf` / 数十万变体），但面临的三阶段问题在本质上完全相同。本文档借鉴 UE5 的完整架构，结合 TGFX 的规模优势，设计一套**轻量级但完整**的三阶段 Shader 编译体系。

---

## 一、问题模型

### 1.1 从着色语言到 GPU 执行的完整路径

一段 Shader 代码从源文本到在 GPU 上执行，在逻辑上经过三个阶段：

```
阶段 I: 语言翻译 (Source Translation)
  高级着色语言 (HLSL/GLSL) → 目标中间表示或目标语言 (SPIR-V/MSL/WGSL)
  性质: 纯 CPU 计算, 确定性, 可离线执行
  耗时: 20-40ms/shader (估算, 基于同类引擎基准数据)

阶段 II: 驱动编译 (Driver Compilation)
  中间表示/目标语言 → GPU 硬件微码
  性质: 依赖 GPU 驱动, 依赖硬件型号, 必须在目标设备执行

阶段 III: 管线状态组装 (Pipeline State Assembly)
  Shader 微码 + 渲染状态 (Blend/Depth/Stencil/VertexLayout...) → Pipeline State Object
  性质: 依赖运行时渲染状态, GPU 驱动可能再次优化, 必须在目标设备执行
```

**重要说明**：阶段 II 和阶段 III 是**逻辑概念**，在不同后端的 API 调用中对应关系不同：

| 后端 | `createShaderModule` 做了什么 | `createRenderPipeline` 做了什么 |
|------|------------------------------|-------------------------------|
| **Vulkan** | `vkCreateShaderModule` 仅存储 SPIR-V blob，**不做驱动编译** (<0.1ms) | `vkCreateGraphicsPipelines` **同时完成**驱动编译 + Pipeline 链接（阶段 II+III 合并） |
| **Metal** | `newLibraryWithSource:` **包含** MSL → AIR 驱动编译（阶段 II） | `newRenderPipelineState:` 执行 Pipeline 链接和最终优化（阶段 III） |
| **WebGPU** | `wgpuDeviceCreateShaderModule` 行为依赖底层实现 | `wgpuDeviceCreateRenderPipeline` 行为依赖底层实现 |

这意味着：
- **Vulkan** 中优化 `VkPipelineCache`（序列化到磁盘）**同时加速了阶段 II 和 III**
- **Metal** 中阶段 II 和阶段 III 可以**独立优化**：预编译 `.metallib` 跳过阶段 II，`MTLBinaryArchive` 缓存阶段 III

### 1.2 各阶段对首帧延迟的贡献

在未做任何优化的 TGFX 中，一个 Program（VS + FS）的首次创建耗时约 40-80ms。以下为基于同类引擎基准数据的**估算分解**（实际数值需在目标设备上 profiling 验证）：

| 阶段 | 估算耗时 | 占比 | 可优化性 |
|------|:-------:|:----:|:--------:|
| I. 语言翻译 (shaderc/spirv-cross/tint) | 20-40ms | ~60% | 可完全离线化 |
| II+III. 驱动编译 + Pipeline 组装 | 5-15ms | ~30% | 可通过缓存序列化 + 预创建消除 |
| 其他（GLSL 生成、对象创建） | 2-5ms | ~10% | 不可消除，但已足够小 |

> 注：由于 Vulkan 中阶段 II 和 III 合并在 `vkCreateGraphicsPipelines` 一个调用中，此处将其合并统计。Metal 中两者可独立度量。

首帧通常触发 5-15 个不同 Program，总延迟 200-600ms。三阶段优化逐级叠加，可将首帧延迟从 200-600ms 降至接近零。

### 1.3 UE5 的三阶段对应关系

| 阶段 | UE5 机制 | UE5 产物 | 执行时机 |
|------|---------|---------|---------|
| I. 语言翻译 | HLSLCC / DXC / ShaderConductor | `.ushaderbytecode` | Cook 时（构建期） |
| II. 驱动编译 | GPU 驱动自身完成 | 驱动内部缓存 | 首次运行时 |
| III. 管线状态缓存 | PSO Cache / PSO Precaching | `.upipelinecache` | 采集后打包分发 |

---

## 二、阶段 I：语言翻译（Source Translation）

### 2.1 问题定义

TGFX 采用"GLSL 先行"架构：所有 Processor 的 `emitCode()` 生成 GLSL 450 源码，运行时逐级翻译为各后端目标语言：

```
Vulkan:  GLSL → PreprocessGLSL → shaderc(GLSL→SPIR-V) → vkCreateShaderModule
Metal:   GLSL → PreprocessGLSL → shaderc(GLSL→SPIR-V) → spirv-cross(SPIR-V→MSL) → newLibraryWithSource
WebGPU:  GLSL → preprocessGLSL → shaderc(GLSL→SPIR-V) → tint(SPIR-V→WGSL) → wgpuDeviceCreateShaderModule
```

其中 `shaderc` 编译占总耗时的 60% 以上。这些翻译步骤是纯 CPU 确定性计算，不依赖 GPU 硬件，可以在构建期完成。

### 2.2 UE5 做法

UE5 在 Cook 阶段通过 **ShaderCompileWorker**（独立进程，防止崩溃影响主进程）遍历所有 Shader Permutation，将 HLSL 通过跨平台编译器链翻译为各目标平台的字节码：

```
UE5 Cook Pipeline:
  Material Graph → HLSL 生成
    → FShaderType 注册 (IMPLEMENT_GLOBAL_SHADER 宏, 静态初始化自动注册)
    → ShouldCompilePermutation() 剪枝无效变体
    → ShaderCompileWorker (独立进程 × N 实例, 并行编译)
      → HLSL → DXC → DXIL (DirectX)
      → HLSL → DXC → SPIR-V (Vulkan)
      → HLSL → ShaderConductor → SPIR-V → spirv-cross → MSL (Metal)
    → DDC (Derived Data Cache) 缓存编译产物
      → Key = hash(源码内容 + 编译参数 + 目标平台 + 引擎版本)
      → 任何因素变化 → key 变化 → 自动重编，零手动维护
```

**关键设计决策**：

1. **声明式注册**：`IMPLEMENT_GLOBAL_SHADER` 宏利用 C++ 静态初始化自动将 `FShaderType` 插入全局注册表，无需手动维护清单
2. **内容 hash 驱动失效**：DDC key 完全由内容决定，开发者永远不需要手动递增版本号
3. **`ShouldCompilePermutation()` 剪枝**：理论排列空间可达百万级，通过此函数剪枝至实际需要的数千种
4. **粉红色占位**：未编译完的 Shader 使用 Error Material 占位，不阻塞运行

### 2.3 TGFX 设计

#### 2.3.1 架构映射

| UE5 概念 | TGFX 对应 | 规模对比 |
|---------|----------|:-------:|
| `FShaderType` + `IMPLEMENT_GLOBAL_SHADER` | `DEFINE_PROCESSOR_CLASS_ID` 宏 | 37 vs 数千 |
| Material × Permutation 排列组合 | Processor 组合 × Pipeline State | ~200 vs 数十万 |
| ShaderCompileWorker (多进程并行) | 单 CLI 工具串行执行 | 30-60s vs 数小时 |
| DDC hash 失效 | Source Hash + CMake DEPENDS | 联合 hash vs 逐条目 hash |
| 粉红色 Error Material | 保留完整动态翻译兜底路径 | **更安全** |

#### 2.3.2 构建期翻译流程

```
┌──────────────────────────────────────────────────────────────────┐
│  构建期 (CI / 开发机)                                             │
│                                                                   │
│  ShaderPrecompiler CLI                                            │
│    1. ShaderVariantEnumerator                                     │
│       枚举所有 Processor 组合 + Pipeline State → VariantDesc[]    │
│                                                                   │
│    2. 对每个 VariantDesc:                                         │
│       a. 构造 Processor 对象 (预设 ShaderCaps + TextureSamplerDesc)│
│       b. computeProgramKey() → BytesKey                           │
│       c. ProgramBuilder::emitAndInstallProcessors() → GLSL       │
│       d. 执行翻译链:                                              │
│          Vulkan:  PreprocessGLSL → shaderc → SPIR-V              │
│          Metal:   PreprocessGLSL → shaderc → spirv-cross → MSL   │
│          WebGPU:  preprocessGLSL → shaderc → tint → WGSL         │
│       e. 写入 Bundle: {BytesKey, Backend, VS产物, FS产物}         │
│                                                                   │
│    3. 输出 shaders.bundle (单文件, 含所有后端变体)                  │
└──────────────────────────────────────────────────────────────────┘
```

#### 2.3.3 自动失效机制

借鉴 UE5 DDC 的核心理念——**内容决定 key，key 变化即失效**——但针对 TGFX 的规模简化为联合 Source Hash：

```
UE5 DDC:
  Key = hash(单个 .usf 源码 + 编译参数 + 平台 + 引擎版本)
  粒度: 每个 Shader × Permutation 独立缓存
  适用: 百万级变体, 增量编译关键

TGFX Source Hash:
  Key = hash(所有影响 GLSL 的源文件的联合内容)
  粒度: 全局, 任一文件变化 → 全量重新生成
  适用: ~200 变体, 全量生成 30-60s, 增量无必要
```

Source Hash 涵盖的文件范围：

| 源文件 | 影响 |
|--------|------|
| `src/gpu/processors/*.h/*.cpp` | `onComputeProcessorKey()` 决定 key 空间 |
| `src/gpu/glsl/processors/*.h/*.cpp` | `emitCode()` 决定 GLSL 输出 |
| `src/gpu/glsl/ShaderBuilder.*` | 公共 GLSL 拼接逻辑 |
| `src/gpu/glsl/GLSL*ShaderBuilder.*` | VS/FS 特定拼接逻辑 |
| `src/gpu/ProgramBuilder.*` | Processor 组装流程 |
| `src/gpu/ShaderCaps.*` | 后端能力标志影响条件分支 |

CMake `add_custom_command` 的 `DEPENDS` 参数天然支持文件时间戳比对，等价于 UE5 DDC 的 hash 比对但零额外基础设施：

```cmake
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/shaders.bundle
  COMMAND ShaderPrecompiler --output-dir ${CMAKE_BINARY_DIR}
  DEPENDS ShaderPrecompiler ${SHADER_SOURCE_FILES}
  COMMENT "Generating shader bundle..."
)
```

#### 2.3.4 运行时加载

运行时改造集中在 `GLSLProgramBuilder::finalize()`，在现有 ShaderModule 创建步骤前插入 Bundle 查找：

```
ProgramInfo::getProgram()
  → GlobalCache::findProgram(key)       [第一级: 内存 LRU]
    → 命中: 返回完整 Program (0ms)
  → ShaderBundle::find(key)             [第二级: Bundle 查找, 本阶段新增]
    → 命中: 从预翻译产物创建 ShaderModule, 跳过翻译链 (~5ms)
  → 动态翻译 (现有路径)                  [第三级: 兜底]
    → 完整 GLSL → SPIR-V/MSL/WGSL 翻译 (~40ms)
```

兜底路径的存在是 TGFX 相较 UE5 的设计优势：UE5 的未编译 Shader 显示为粉红色 Error Material，而 TGFX 的未覆盖变体通过动态翻译**功能完全正确**，仅有性能差异。

### 2.4 预期收益

| 指标 | 优化前 | 优化后 (Bundle 命中) |
|------|:------:|:-------------------:|
| 单 Shader 翻译耗时 | 20-40ms | **0ms** |
| 单 Program 创建耗时 | 40-80ms | **5-10ms** |
| 首帧总开销 (10 Programs) | 400-800ms | **50-100ms** |

---

## 三、阶段 II：驱动编译缓存（Driver Compilation Cache）

### 3.1 问题定义

阶段 I 的 Bundle 跳过了语言翻译，但后续的驱动编译和 Pipeline 链接仍需在运行时执行。各后端的具体表现不同：

- **Vulkan**：`vkCreateShaderModule` 本身很快（<0.1ms，仅存储 SPIR-V blob），驱动编译**延迟到** `vkCreateGraphicsPipelines` 中与 Pipeline 链接一并完成。TGFX 当前**已创建** `VkPipelineCache`（`VulkanGPU.cpp:154`），并传入 `vkCreateGraphicsPipelines`（`VulkanRenderPipeline.cpp:582`），因此同一进程内重复创建相同 Pipeline 已能命中内存缓存。但当前**未做磁盘持久化**（`initialDataSize = 0`，无 `vkGetPipelineCacheData` 调用），每次应用重启后缓存为空。
- **Metal**：`newLibraryWithSource:` 包含真正的 MSL → AIR 编译（耗时 3-5ms），`newRenderPipelineState:` 做 Pipeline 链接（耗时 1-3ms），两步独立可分。当前**未使用** `MTLBinaryArchive`。
- **WebGPU**：行为依赖底层实现（Dawn/wgpu），无标准持久化 API。

### 3.2 UE5 做法

UE5 通过 **Shader Pipeline Cache**（`FShaderPipelineCache`）解决此问题：

```
运行时:
  首次遇到新 Shader
    → GPU 驱动编译 SPIR-V/DXIL → 硬件微码 (~5ms)
    → 编译结果被 GPU 驱动自身缓存 (驱动内部机制)

  同时:
    → FPipelineFileCache::LogNewPipelineState() 记录 Shader 描述
    → 序列化为 .upipelinecache 文件 (包含 Shader Hash + 编译参数)

  应用重启:
    → 加载 .upipelinecache → 反序列化 Shader 描述
    → 后台线程批量重新提交编译请求
    → GPU 驱动命中自身缓存 → 编译接近瞬时 (<0.1ms)
```

关键点：驱动缓存是 GPU 厂商实现的（NVIDIA/AMD/Apple 各有不同），UE5 的 `.upipelinecache` 本质上是**触发驱动预热**的描述清单，而非直接存储微码。

### 3.3 TGFX 设计

TGFX 可利用各后端 API 提供的原生管线缓存机制：

| 后端 | API 机制 | 缓存内容 | 序列化能力 |
|------|---------|---------|:---------:|
| Vulkan | `VkPipelineCache` | `vkCreateGraphicsPipelines` 的驱动编译结果 | ✅ `vkGetPipelineCacheData` / `vkCreatePipelineCache` |
| Metal | `MTLBinaryArchive` | `newRenderPipelineStateWithDescriptor` 的编译结果 | ✅ `addRenderPipelineFunctions` / `serializeToURL` |
| WebGPU | 暂无标准 API | 浏览器/Dawn 内部管理 | ❌ 浏览器环境不可控 |

#### 3.3.1 Vulkan Pipeline Cache

TGFX 当前状态：`VulkanGPU` 已创建 `VkPipelineCache` 并传入 `vkCreateGraphicsPipelines`，但 `initialDataSize = 0`（无磁盘持久化）。优化只需添加序列化/反序列化逻辑：

```
当前行为 (已实现):
  启动 → 创建空 VkPipelineCache (initialDataSize = 0)
    → vkCreateGraphicsPipelines(pipelineCache, ...) — 同一进程内后续 Pipeline 可命中内存缓存
    → 退出 → vkDestroyPipelineCache — 缓存丢失

目标行为 (需新增磁盘持久化):
  启动:
    读取 pipeline_cache_vulkan.bin (若存在)
      → vkCreatePipelineCache(createInfo.pInitialData = cachedData)
    或创建空 VkPipelineCache (文件不存在或校验失败)

  运行中:
    → vkCreateGraphicsPipelines(pipelineCache, ...) — 命中磁盘缓存, 编译加速

  退出:
    → vkGetPipelineCacheData(pipelineCache, &size, &data)
    → 序列化到本地文件: pipeline_cache_vulkan.bin
```

由于 Vulkan 中 `vkCreateGraphicsPipelines` 同时完成驱动编译和 Pipeline 链接，磁盘持久化 `VkPipelineCache` **同时加速了逻辑上的阶段 II 和阶段 III**。

#### 3.3.2 Metal

Metal 的优化可在两个层次展开：

**层次 A：预编译 Metal Library（阶段 I 延伸，跳过 `newLibraryWithSource:`）**

TGFX 当前使用 `newLibraryWithSource:` 传入 MSL 源码，Metal 驱动在此调用中编译 MSL → AIR（耗时 3-5ms）。构建期可直接将 MSL 预编译为 `.metallib`：

```
构建期:
  MSL 源码 → metal 命令行工具 → .metallib (预编译 Metal 库)

运行时:
  [device newLibraryWithURL:metallibURL error:&error]  — 跳过 MSL 编译, 直接加载
```

此优化属于阶段 I 的 Bundle 产物扩展——对 Metal 后端，Bundle 中可以直接存储 `.metallib` 而非 MSL 文本。

**层次 B：MTLBinaryArchive（阶段 III，缓存 Pipeline State）**

```
首次运行:
  创建 MTLBinaryArchive
    → [archive addRenderPipelineFunctionsWithDescriptor:descriptor error:&error]
    → 应用退出时:
       [archive serializeToURL:fileURL error:&error]
       → 序列化到本地文件

后续运行:
  [device newBinaryArchiveWithDescriptor:desc error:&error]
    → 从 archive 加载已编译的 Pipeline State
```

**注意**：`MTLBinaryArchive` 缓存的是**Pipeline State 的编译结果**（阶段 III），而非 Shader 本身（阶段 II）。两层优化可独立实施。

#### 3.3.3 运行时集成

```cpp
// src/gpu/PipelineCache.h (新增)

class PipelineCache {
 public:
  // 从磁盘加载缓存 (应用启动时调用)
  bool loadFromDisk(const std::string& path);

  // 序列化缓存到磁盘 (帧结束或应用退出时调用)
  bool saveToDisk(const std::string& path);

  // 各后端自行实现创建 Pipeline 时传入缓存对象
  // Vulkan: 将 VkPipelineCache 传给 vkCreateGraphicsPipelines
  // Metal:  将 MTLBinaryArchive 传给 newRenderPipelineState
};
```

#### 3.3.4 与 UE5 的对比

| 维度 | UE5 | TGFX |
|------|-----|------|
| 缓存粒度 | 每个 PSO 独立记录 + 后台批量预编译 | 整个 Pipeline Cache 作为不透明 blob |
| 主动预热 | 启动时从 `.upipelinecache` 批量提交编译请求 | 首次使用时自动积累，后续启动命中驱动缓存 |
| 清单管理 | `FPipelineFileCache` 记录 Shader Hash + 渲染状态 | 不需要——API 原生缓存自动管理 |
| 跨版本兼容 | Shader Stable Key 保证版本间缓存有效 | 驱动自行校验缓存有效性，不匹配时静默重编译 |

TGFX 无需 UE5 那样复杂的 PSO 描述清单系统，因为 Vulkan/Metal 的原生 Pipeline Cache API 已经自动记录了所有需要的信息。

### 3.4 预期收益

| 指标 | 无缓存 (首次运行) | 有缓存 (后续运行) | 说明 |
|------|:----------------:|:----------------:|------|
| 单 Pipeline 创建 (Vulkan) | 5-15ms | **<0.5ms** | `VkPipelineCache` 持久化同时加速 II+III |
| 单 ShaderModule 创建 (Metal) | 3-5ms | **<0.5ms** | 预编译 `.metallib` 跳过编译 |
| 首帧总开销 (阶段 I 已优化, 10 Pipelines) | 50-100ms | **5-10ms** | |

> 注：以上为估算值。Vulkan 数值反映的是 `vkCreateGraphicsPipelines` 命中持久化 `VkPipelineCache` 前后的差异。

---

## 四、阶段 III：管线状态预创建（Pipeline State Precaching）

### 4.1 问题定义

即使阶段 I（语言翻译）和阶段 II（驱动编译）都被缓存命中，`createRenderPipeline` 仍需在运行时执行，因为 Pipeline State Object 的创建依赖于运行时才能确定的渲染状态参数（Blend Mode、PixelFormat、SampleCount、VertexLayout 等）。

首次使用某个 Shader + 渲染状态组合时，仍然存在 1-5ms 的 Pipeline 创建延迟。在快速切换场景或首次渲染复杂内容时，这些延迟可能累积为可感知的卡顿。

### 4.2 UE5 做法

UE5 在这个阶段有两套互补的系统：

#### 4.2.1 PSO Cache（手动采集）

```
采集阶段 (QA/自动化测试):
  运行游戏 → 触发所有渲染路径
    → 每次 GPU 创建新 PSO → FPipelineFileCache::LogNewPipelineState()
    → 记录: {Shader Hash (VS+PS), BlendState, DepthStencil, RasterState,
              VertexDecl, RT Formats, MSAA Count, PrimitiveType, ...}
    → 序列化为 Bundled PSO Cache (.upipelinecache)

分发:
  Bundled PSO Cache 随安装包打包

运行时加载:
  引擎启动 → 加载 .upipelinecache → 反序列化 PSO 描述清单
    → 后台线程池 (75% 硬件线程) 批量异步创建 PSO
    → 加载界面期间完成预创建
    → 进入游戏时所有常见 PSO 已就绪
```

#### 4.2.2 PSO Precaching（自动预测）

UE5 引入了 PSO Precaching（5.0 起可用，后续版本持续改进），基于组件加载时的材质和顶点工厂信息**自动预测**需要的 PSO：

```
UPrimitiveComponent::PostLoad()
  → PrecachePSOs()
    → 遍历所有 MeshPassProcessor 收集 PSO Initializer
    → 提交后台异步编译
    → 组件 Proxy 延迟创建直到 PSO 就绪 (或用默认材质占位)
```

核心函数 `ShouldPrecachePermutation()` 在编译期过滤不需要的排列，`CollectPSOInitializers()` 在运行时收集实际需要的 PSO 描述。

#### 4.2.3 验证与追踪

UE5 提供了完整的 PSO 命中率追踪体系：

| 指标 | 含义 |
|------|------|
| **Missed** | 运行时需要但未被预缓存的 PSO 数量 |
| **Hit** | 成功命中预缓存的 PSO 数量 |
| **Too Late** | 已提交预缓存但在需要时仍未编译完的 PSO 数量 |
| **Untracked** | 未启用预缓存追踪的 PSO 数量 |

通过 `stat PSOPrecache` 控制台命令实时查看这些统计数据。

### 4.3 TGFX 设计

TGFX 的 Pipeline State 参数空间远小于 UE5（TGFX 没有复杂的 Pass 系统、Vertex Factory 种类有限），因此可以采用更简化的方案。

#### 4.3.1 基于 GlobalCache 扩展的预创建

TGFX 当前的 `GlobalCache` 已具备 Program LRU 缓存（≤256 个），但缓存的是完整 Program 对象，每次应用启动后缓存为空。阶段 III 的目标是在应用启动时**主动预创建**高频 Pipeline。

```cpp
// 预创建策略

// 方案 A: 基于 Bundle 变体清单 (推荐, 与阶段 I 复用基础设施)
void PipelinePrecacher::precacheFromBundle(const ShaderBundle& bundle) {
  // Bundle 中已知所有高频变体的 ProgramKey
  // 逐个提交预创建请求到后台队列
  for (const auto& entry : bundle.entries()) {
    submitPrecacheRequest(entry.programKey, entry.backend);
  }
}

// 方案 B: 基于运行时采集 (类似 UE5 PSO Cache, 作为补充)
void PipelinePrecacher::precacheFromHistory(const PipelineHistory& history) {
  // 从上次运行采集的 Pipeline 描述清单中预创建
  for (const auto& desc : history.descriptors()) {
    submitPrecacheRequest(desc);
  }
}
```

#### 4.3.2 异步预创建流程

```
应用启动
  ├── Context 初始化
  │     └── PipelineCache::loadFromDisk()       [阶段 II 缓存加载]
  ├── ShaderBundle::load()                       [阶段 I Bundle 加载]
  └── PipelinePrecacher::start()                 [阶段 III 预创建]
        │
        ├── 从 Bundle 变体清单读取高频 ProgramKey
        ├── 对每个 ProgramKey:
        │     1. ShaderBundle::find(key) → 获取预翻译产物
        │     2. 创建 ShaderModule (驱动编译, 命中阶段 II 缓存)
        │     3. 执行 emitAndInstallProcessors() → 获取 UniformData 布局 (*)
        │     4. 组装 RenderPipelineDescriptor (使用常见渲染状态)
        │     5. createRenderPipeline → Pipeline
        │     6. 构造 Program(pipeline, vertexUniformData, fragmentUniformData)
        │     7. GlobalCache::addProgram(key, program)
        │
        └── 预创建完成, 首帧直接从 GlobalCache 命中
```

> (*) **UniformData 约束**：TGFX 的 `Program` 对象包含 `RenderPipeline` + `UniformData`（vertex/fragment 的 Uniform 布局信息）。`UniformData` 在 `ProgramBuilder::emitAndInstallProcessors()` 过程中收集，依赖于实际执行 Processor 的 `emitCode()` 路径。因此预创建时**仍需执行 GLSL 生成流程**以获取 Uniform 布局——Bundle 跳过的仅是后续的 shaderc/spirv-cross/tint 翻译链。
>
> 替代方案：将 `UniformData` 布局信息也序列化到 Bundle 中，可完全跳过 `emitAndInstallProcessors()`，但增加了 Bundle 格式的复杂度。两种方案的选择可在实施时决定。

#### 4.3.3 渲染状态的预测

Pipeline 创建需要完整的渲染状态。高频状态组合非常有限：

| 参数 | 高频取值 |
|------|---------|
| BlendMode | SrcOver (绝大多数) |
| PixelFormat | RGBA_8888, BGRA_8888 |
| SampleCount | 1 (无 MSAA) |
| CullMode | None |
| Output Swizzle | RGBA |

对于 TGFX 的 2D 渲染场景，上述组合覆盖了 90%+ 的使用情况。预创建时使用这些默认状态，未命中的组合走现有动态创建路径。

#### 4.3.4 运行时 Pipeline 描述采集（可选）

类似 UE5 的 Bundled PSO Cache，TGFX 可在运行时采集实际使用的 Pipeline 描述并序列化：

```
运行时:
  ProgramInfo::getProgram()
    → 创建新 Program
    → PipelineHistory::record({programKey, blendMode, pixelFormat, sampleCount, ...})

应用退出:
  PipelineHistory::saveToDisk("pipeline_history.bin")

下次启动:
  PipelineHistory::loadFromDisk("pipeline_history.bin")
    → PipelinePrecacher::precacheFromHistory(history)
```

此机制与 Bundle 预创建互补：Bundle 覆盖已知的高频变体，History 覆盖实际运行中遇到的边缘情况。

### 4.4 与 UE5 的对比

| 维度 | UE5 PSO Cache | UE5 PSO Precaching | TGFX Pipeline Precaching |
|------|:------------:|:-----------------:|:------------------------:|
| 数据来源 | QA 手动采集 | 组件加载时自动预测 | Bundle 变体清单 + 运行时采集 |
| 覆盖方式 | 打包分发 | 实时计算 | 两种模式互补 |
| 异步编译 | 后台线程池 (75% 核心) | 后台线程 | 后台任务 (规模小, 单线程足够) |
| 未命中处理 | 可能卡顿 + 粉红占位 | 延迟创建/默认材质 | 走现有动态路径, 功能正确 |
| 验证工具 | `stat PSOPrecache` | 同上 | 命中率日志 |

### 4.5 预期收益

| 指标 | 无预创建 | 有预创建 |
|------|:-------:|:-------:|
| 首帧 Pipeline 创建 | 5-10ms × N | **0ms (预创建命中)** |
| 首帧总开销 (I+II+III, 10 Pipelines) | 5-10ms | **<1ms** |

---

## 五、三阶段叠加效果

### 5.1 性能对比矩阵

| 场景 | 首帧总开销 (10 Programs) | 说明 |
|------|:----------------------:|------|
| 无任何优化 (当前状态) | **400-800ms** | 全链路运行时执行 |
| +阶段 I: Bundle (跳过翻译) | **50-100ms** | 跳过 shaderc/spirv-cross/tint |
| +阶段 II: Pipeline Cache 持久化 | **5-10ms** | Vulkan 中同时加速 II+III |
| +阶段 III: Pipeline Precaching | **<1ms** | 启动时异步预创建 |

> 注：上述数据为基于同类引擎经验的估算值，实际数据需在目标设备上 profiling 验证。Vulkan 中阶段 II/III 的收益会合并体现（`VkPipelineCache` 持久化同时缓存了驱动编译和 Pipeline 链接结果）。

### 5.2 完整运行时查找链

```
ProgramInfo::getProgram()
│
├── L1: GlobalCache (内存 LRU, ≤256 Programs)
│   命中 → 返回完整 Program          开销: 0ms
│   [阶段 III 预创建的产物在此层命中]
│
├── L2: ShaderBundle (只读, 随包分发)
│   命中 → 从预翻译产物创建 ShaderModule + Pipeline
│   [阶段 I 的产物在此层命中]
│   [阶段 II 的缓存使 ShaderModule 创建更快]
│   开销: ~5ms (首次运行) / ~1ms (后续运行, 驱动缓存命中)
│
├── L3: 动态翻译 (现有路径, 兜底)
│   处理 Bundle 未覆盖的变体
│   开销: ~40-80ms
│
└── 新创建的 Program 写入 L1 (GlobalCache)
    [若启用] Pipeline 描述写入 History (供下次启动预创建)
```

### 5.3 UE5 对照总览

| UE5 层次 | TGFX 对应 | 阶段 | 优先级 |
|---------|----------|:----:|:-----:|
| DDC + ShaderCompileWorker | ShaderBundle + CLI 工具 | I | **P0** |
| DDC 内容 hash 自动失效 | Source Hash + CMake DEPENDS | I | **P0** |
| `ShouldCompilePermutation()` | ShaderVariantEnumerator | I | **P0** |
| GPU 驱动缓存 | 驱动自身行为 (无需实现) | II | — |
| `VkPipelineCache` / `MTLBinaryArchive` | PipelineCache 序列化 | II | **P1** |
| Bundled PSO Cache (手动采集) | Pipeline History 采集 | III | **P2** |
| PSO Precaching (自动预测) | PipelinePrecacher (基于 Bundle) | III | **P2** |
| ShaderCompileWorker 独立进程 | 不需要 (规模太小) | — | — |
| 分布式 DDC 共享 | 不需要 (规模太小) | — | — |
| `recompileshaders` 热重载 | 不需要 (兜底路径即时可用) | — | — |

---

## 六、实施路线

### Phase 1: 阶段 I 基础设施 (语言翻译)

**目标**：构建期完成 GLSL → SPIR-V/MSL/WGSL 翻译，运行时跳过翻译链。

- ClassID 改为编译期确定性 hash
- ProgramBuilder 解耦 Context 依赖
- addSampler 接口改造
- ShaderBundle 加载器与 CLI 翻译工具
- CMake DEPENDS 自动失效机制

**预期交付**：首帧延迟从 400-800ms → 50-100ms。

### Phase 2: 阶段 II 驱动编译缓存

**目标**：应用重启后跳过 GPU 驱动重编译。

- Vulkan: `VkPipelineCache` 序列化/反序列化
- Metal: `MTLBinaryArchive` 序列化/反序列化
- WebGPU: 依赖浏览器/Dawn 内部缓存，不做额外处理
- 缓存文件本地存储与版本校验

**预期交付**：后续启动首帧延迟从 50-100ms → 5-10ms。

### Phase 3: 阶段 III 管线预创建

**目标**：应用启动时预创建高频 Pipeline，消除首帧创建延迟。

- 基于 Bundle 变体清单的启动期预创建
- 运行时 Pipeline History 采集与序列化
- 异步预创建任务管理
- 命中率统计与日志

**预期交付**：首帧延迟从 5-10ms → <1ms。

---

## 七、风险评估

| 风险 | 概率 | 影响 | 所属阶段 | 缓解措施 |
|------|:----:|:----:|:-------:|---------|
| Bundle 变体覆盖不足 | 中 | 中 | I | 兜底路径保证正确性；运行时 miss 统计持续补充 |
| Source Hash 粒度过粗导致频繁全量重生成 | 低 | 低 | I | 全量生成仅 30-60s；可演进为逐条目增量更新 |
| 驱动 Pipeline Cache 跨设备不兼容 | 低 | 低 | II | Vulkan/Metal 驱动自行校验，不兼容时静默重编译 |
| Pipeline Cache 文件损坏 | 极低 | 低 | II | 加载失败时丢弃缓存，退化到无缓存路径 |
| 预创建的渲染状态与运行时不匹配 | 中 | 低 | III | 未命中的组合走动态创建路径，功能不受影响 |
| 阶段 II/III 增加应用启动耗时 | 低 | 低 | II/III | 异步执行，不阻塞首帧渲染 |

---

## 附录 A：UE5 Shader 编译体系参考架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        UE5 Cook 阶段 (构建期)                            │
│                                                                          │
│  Material Graph → HLSL 生成                                              │
│    → FShaderType 注册 (IMPLEMENT_GLOBAL_SHADER, 静态初始化)               │
│    → ShouldCompilePermutation() 剪枝                                     │
│    → ShaderCompileWorker × N 进程                                        │
│      → HLSL → DXC/FXC → DXIL/DXBC (DirectX)                            │
│      → HLSL → DXC → SPIR-V (Vulkan)                                    │
│      → HLSL → ShaderConductor → SPIR-V → spirv-cross → MSL (Metal)     │
│    → DDC 缓存: Key = hash(源码 + 参数 + 平台 + 版本)                     │
│    → 产出: .ushaderbytecode                                              │
│                                                                          │
├─────────────────────────────────────────────────────────────────────────┤
│                        QA 测试阶段                                       │
│                                                                          │
│  运行游戏 → 触发渲染 → 记录 PSO 描述                                     │
│    → FPipelineFileCache::LogNewPipelineState()                           │
│    → 序列化: .upipelinecache (Bundled PSO Cache)                         │
│                                                                          │
├─────────────────────────────────────────────────────────────────────────┤
│                        打包阶段                                          │
│                                                                          │
│  .ushaderbytecode + .upipelinecache → 打入 .pak 包                       │
│                                                                          │
├─────────────────────────────────────────────────────────────────────────┤
│                        运行时                                            │
│                                                                          │
│  引擎启动                                                                │
│    → 加载 .upipelinecache → 反序列化 PSO 描述                            │
│    → PSO Precaching: 后台线程池批量异步创建 PSO                           │
│    → UPrimitiveComponent::PostLoad() → PrecachePSOs() (自动预测)         │
│    → 加载界面等待: FShaderPipelineCache::NumPrecompilesRemaining()        │
│                                                                          │
│  运行中                                                                  │
│    → 首次遇到未缓存 PSO → 实时创建 (可能卡顿)                            │
│    → 记录到增量 .rec.upipelinecache                                      │
│    → 下次启动预编译                                                       │
│                                                                          │
│  验证: stat PSOPrecache → {Missed, Hit, Too Late, Untracked}             │
└─────────────────────────────────────────────────────────────────────────┘
```

## 附录 B：TGFX 三阶段目标架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        构建期 (CI)                                       │
│                                                                          │
│  Processor 源码 → ShaderPrecompiler CLI                                  │
│    → ShaderVariantEnumerator (枚举 ~200 变体)                            │
│    → ProgramBuilder::emitAndInstallProcessors() → GLSL                  │
│    → shaderc/spirv-cross/tint → SPIR-V/MSL/WGSL                        │
│    → 产出: shaders.bundle                                                │
│    → 失效: Source Hash (联合内容 hash, 零手动维护)                        │
│                                                                          │
├─────────────────────────────────────────────────────────────────────────┤
│                        分发                                              │
│                                                                          │
│  shaders.bundle + libTGFX → SDK Release / App 资源目录                   │
│  pipeline_cache_*.bin → 本地存储 (首次运行后生成)                         │
│  pipeline_history.bin → 本地存储 (首次运行后生成)                         │
│                                                                          │
├─────────────────────────────────────────────────────────────────────────┤
│                        运行时                                            │
│                                                                          │
│  应用启动                                                                │
│    ① PipelineCache::loadFromDisk()           [阶段 II 缓存预热]          │
│    ② ShaderBundle::load()                     [阶段 I Bundle 加载]       │
│    ③ PipelinePrecacher::start()               [阶段 III 异步预创建]      │
│                                                                          │
│  运行中                                                                  │
│    ProgramInfo::getProgram()                                             │
│      L1: GlobalCache        → 0ms     (阶段 III 命中)                    │
│      L2: ShaderBundle       → 1-5ms   (阶段 I+II 命中)                  │
│      L3: 动态翻译           → 40-80ms (兜底, 功能正确)                   │
│                                                                          │
│  应用退出                                                                │
│    PipelineCache::saveToDisk()                [阶段 II 缓存持久化]        │
│    PipelineHistory::saveToDisk()              [阶段 III 采集持久化]       │
└─────────────────────────────────────────────────────────────────────────┘
```

## 附录 C：各阶段在 TGFX 各后端的适用性

| 后端 | 阶段 I (翻译) | 阶段 II (驱动缓存) | 阶段 III (预创建) |
|------|:------------:|:-----------------:|:----------------:|
| **Vulkan** | ✅ GLSL → SPIR-V | ✅ `VkPipelineCache` | ✅ 基于 Bundle |
| **Metal** | ✅ GLSL → MSL | ✅ `MTLBinaryArchive` | ✅ 基于 Bundle |
| **WebGPU** | ✅ GLSL → WGSL | ⚠️ 浏览器管理 | ⚠️ 受限于 async API |
| **OpenGL** | ❌ 无跨语言翻译 (驱动直接编译 GLSL，但编译本身仍耗时) | ⚠️ `glProgramBinary` 可缓存编译结果 (当前未使用) | ⚠️ `glProgramBinary` 可选 |
