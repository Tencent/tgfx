# 变体系统：从运行时编译到单一来源预编译

> 本文记录 tgfx 着色器变体系统的完整设计与实现，从问题定义出发，经过架构演进、数学基础、验证方法论，到最终落地与实测数据。

---

## 目录

1. [问题定义](#1-问题定义)
2. [架构概览](#2-架构概览)
3. [维度设计原理](#3-维度设计原理)
4. [编译清单的单一来源](#4-编译清单的单一来源)
5. [混合表优化](#5-混合表优化)
6. [验证方法论](#6-验证方法论)
7. [实测数据](#7-实测数据)
8. [已否决的替代方案](#8-已否决的替代方案)
9. [遗留问题与未来方向](#9-遗留问题与未来方向)

---

## 1. 问题定义

### 1.1 运行时编译的延迟

原始渲染管线在 cache miss 时采用即时（JIT）着色器构建：绘制操作触发 `ProgramBuilder` 生成 GLSL，再按后端走不同的编译链。三个后端不能被表述为同一条统一翻译链：

```
Canvas API → DrawOp → Processor 组合
  → ProgramBuilder::emitAndInstallProcessors() → GLSL 源码 (VS + FS)
  ├─ Vulkan: CompileGLSLToSPIRV() → SPIR-V → shader module / pipeline
  ├─ Metal:  CompileGLSLToSPIRV() → SPIR-V → spirv-cross MSL → Metal library / pipeline
  └─ OpenGL: GLSLProgramBuilder 直接交给 glShaderSource → glCompileShader → link
```

因此，JIT 首帧开销由 tgfx 侧源码生成、后端翻译、驱动编译与链接共同组成；其绝对耗时取决于后端、驱动、GPU 和首次遇到的 Program 集合。本文不将特定历史测量值泛化为跨平台常数。

### 1.2 约束条件

本方案在以下硬约束下设计：

- **no-fallback 是覆盖目标**：运行时编译是过渡态，hitRate 100% 是硬指标；任何 precompiled 变体缺失导致的静默 JIT 回退均视为覆盖缺陷。当前通用运行时默认仍使用 `AllowRuntimeFallback` 保证产品可用性，只有 `PrecompiledOnly` 调用点才强制拒绝 JIT。
- **跨后端统一**：Metal、Vulkan、OpenGL 三后端共享同一套排列声明、可达集和匹配逻辑，但 bundle 内的 stage artifact 格式按后端不同。
- **像素一致性**：precompiled 路径与 JIT 路径的渲染结果必须在 1 LSB 以内一致；混合表修改由真实 GPU 上的 AOT-vs-JIT 门禁验证。

### 1.3 目标

将 tgfx 侧 GLSL 生成、shaderc 编译及跨语言翻译尽可能移至构建期；运行时从 bundle 读取 stage artifact，并仍按实际状态创建 shader module 与管线状态对象（PSO）。该方案旨在消除由 tgfx 翻译链引入的首次构建开销，具体首帧收益须按后端和设备实测。

---

## 2. 架构概览

### 2.1 三层架构

```
┌─────────────────────────────────────────────────────────────────┐
│  Bundle 数据层                                                   │
│  PrecompiledShaderCache — format v4                             │
│  header(80B) + vert pool(28B/entry) + frag pool(28B/entry)       │
│  + data pool(可选 zstd) + reflection pool(未压缩)                │
├─────────────────────────────────────────────────────────────────┤
│  匹配与可达集层                                                  │
│  PermutationMatcher — 30 条有序 TryMatch 规则                   │
│  runtime structure gating/Input extraction + shared Compose      │
│  PermutationRules — 30 个手写 input lattice 的枚举器             │
├─────────────────────────────────────────────────────────────────┤
│  声明层                                                         │
│  30 个内核头文件 — vert/frag PermutationDomain                  │
│  PermutationRules.h — 设计章程 + 同步契约                       │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 构建期与运行时的数据流

```
构建期（shader_build_tool）:
  30 个内核声明 ──→ EnumerateReachablePermutations(shaderName)
                ──→ 30 个手写 EnumerateXxx() 穷举各自输入格子
                ──→ ComposeXxx() 计算可达 (vertIndex, fragIndex) 集合
                ──→ 编译 GLSL → SPIR-V → 后端 stage artifact
                ──→ WriteBundle()（请求 zstd maxCLevel；无收益时保持未压缩）

运行时:
  DrawOp → ProgramInfo → GlobalCache 查完整 ProgramKey
         → MatchPermutation() 的有序 TryMatchXxx() 结构筛选
         → 提取 Inputs → ComposeXxx(inputs) → domain.encode()
         → PrecompiledShaderCache 分别查 vertex / fragment stage artifact
         → 创建 shader module + runtime render pipeline → 绘制
         → 若 AOT 失败且 lookup mode 为 AllowRuntimeFallback，则 ProgramBuilder JIT
```

关键设计决策：**Compose 函数是“已给定 Inputs 后的维度映射与维度相关 reject”的唯一作者**。构建期通过手写输入 lattice 调用 Compose 获得可达集；运行时通过真实输入调用同一 Compose 获得匹配索引。该设计消除了旧 `ShouldCompile` 与 matcher 对同一维度组合的双作者问题；但 Inputs 的枚举完备性与 whole-draw 结构筛选仍是独立维护点。

### 2.3 Pair、stage artifact 与后端格式

一个可达 pair 表示 `(shaderName, vertPermutationIndex, fragPermutationIndex)`，不是一个预创建 pipeline。bundle 将 pair 投影为独立 stage artifact：同一 `vertPermutationIndex` 被多个 fragment pair 复用时，只保存一次 vertex artifact；fragment 同理。因此当前 308 个可达 pair 对应 101 个 logical vertex-stage artifact 与 274 个 logical fragment-stage artifact，共 375 个 stage entries。

构建工具对不同 backend 写入不同 artifact：

| 后端 | bundle 中的 stage artifact | 运行时后续动作 |
|---|---|---|
| Vulkan | SPIR-V 二进制 | 创建 Vulkan shader module 与 pipeline |
| Metal | metallib 二进制 | 创建 Metal shader function/module 与 pipeline |
| OpenGL | GLSL 文本 | 驱动编译/链接 OpenGL program |
| WebGPU | WGSL 文本 | 创建 WebGPU shader module 与 pipeline |

因此 AOT 消除了 tgfx 的 GLSL 生成、shaderc 编译和跨语言翻译；它**不**保存预创建的 PSO，runtime pipeline creation 仍依赖 render target、blend/stencil 等实际状态。

### 2.4 Bundle v4 格式

当前 bundle 使用 little-endian format v4：

```text
[header: 80B]
[vertex pool: 28B × vertPoolCount]
[fragment pool: 28B × fragPoolCount]
[data pool: raw or zstd-compressed]
[reflection pool: raw]
```

header 包含 magic `TGSF`、format version、compression type、profile tag、两个 pool 的数量/偏移、data pool 的**未压缩**大小及 reflection offset。每个 pool entry 为 `hashHi:u64 | hashLo:u64 | dataOffset:u32 | dataSize:u32 | reflOffset:u32`，固定 28B。v4 在 stage reflection 条目中加入 `arraySize:u16`；loader 同时兼容 v3/v4，并兼容 compression type 0（raw）、1（legacy zlib）和 2（zstd）。

生产 CMake 请求 `--compress`。writer 使用 `ZSTD_maxCLevel()` 压缩 data pool，但仅在压缩结果更小时写 type 2；否则保留未压缩 type 0。reflection pool、header 和 pool table 不压缩。

写入器的去重仅按 stage key（`shaderName + stage + permutationIndex + profileTag`）折叠，形成上述 101/274 logical stage artifact。report 中的 `uniqueVertexCount`/`uniqueFragmentCount` 是基于 blob 字节相等的**分析统计**，当前不会驱动 bundle payload 的内容去重；zstd 可能自行压缩重复字节，但不改变 pool entry 数量。

---

## 3. 维度设计原理

### 3.1 编译期与运行时的边界判据

一个着色器参数成为编译期维度的充要条件是：**它改变了资源绑定（sampler 数量/布局）或 varying 接口（顶点-片元数据传递）**。一切纯数学运算（算子选择、模式标志、颜色值）必须通过运行时 uniform 传递。

此判据的推论：

| 参数类型 | 归属 | 示例 |
|---|---|---|
| 改变 sampler 数量 | 编译期维度 | `HAS_LOCAL_MASK`、`HAS_MASK_TEXTURE` |
| 改变 varying 声明 | 编译期维度 | `HAS_COVERAGE`、`HAS_UV_COORD`、`HAS_COLOR` |
| 改变顶点属性布局 | 编译期维度 | `GP_LAYOUT`、`STROKE` |
| 算子/模式选择 | 运行时 uniform | `OpType`、`XPBlendMode`、`InputMode` |
| 颜色/系数值 | 运行时 uniform | `Color`、`BlendCoeffs` |
| 纹理属性标志 | 运行时 uniform | `AlphaOnly`、`HasRgbaaa` |

### 3.2 维度收窄的历史轨迹

历史上，逻辑可达 pair 数通过以下机制从 532 收窄至当前 308：

| 机制 | 变体数变化 | 示例 |
|---|---|---|
| TEXTURE_KIND 退役 | 532 → 472 | `sampler2DRect` → `sampler2D` 统一 |
| 渐变四合一 | 472 → 429 | `UnifiedGradientShader` 合并 4 个渐变内核 |
| TEXTURE_COUNT 域收拢 | 429 → 405 | 语义上收敛为无叶与 four-slot padded 两类；编码值为 `0/1`，1-3 叶绑 phantom |
| mask 运行时化 | 405 → 345 | 5 内核的 device mask 改 runtime uniform |
| 后续微调 | 345 → 308 | 新内核接入 |

### 3.3 零真空性质

历史人工审计与 30 条规则迁移阶段的旧清单对账均表明：当前编译的 308 个可达 pair 没有已知的"编译但 matcher 永远不会使用"条目。终态 `--audit` 改为验证可达集 pair 的结构合法性（详见第 4 节）；它不再重新证明 input lattice 与 runtime matcher 的集合等价。

---

## 4. 编译清单的单一来源

### 4.1 问题：两个作者

在架构演进的早期阶段，"该编译什么"存在两个独立的声明来源：

1. **内核头文件**中的 `ShouldCompile` 函数——声明哪些维度组合合法
2. **matcher 规则**（`PermutationMatcher.cpp`）——声明运行时能产生哪些组合

两者必须精确相等：多了浪费 bundle 体积，少了导致静默 JIT 回退。但代码中没有任何机制强制这种相等，只能靠人工同步——同一批领域知识（如 "atlas 布局不携带 coverage"）需要在两个文件中各写一遍。

### 4.2 解决方案：Compose 单一来源

每条 matcher 规则在逻辑上拆分为三部分（源码中仅部分规则具有独立命名的 `ExtractXxx()` 函数；多数规则将前两部分直接写在 `TryMatchXxx()` 中）：

```cpp
// 输入契约（纯 POD，仅含塑形维度的谓词）
struct RoundStrokeRectInputs {
  bool isCoverageAA;
  bool hasCommonColor;
  bool hasUVMatrix;
  int xpType;  // -1 = 不可表示
};

// 提取（运行时专用，不参与穷举）
static std::optional<RoundStrokeRectInputs> ExtractRoundStrokeRect(
    const ProgramInfo* programInfo) {
  // 整体拒绝（错误的 GP 类型、透视 UV）留在这里
  // 它们不改变可达集的形状
}

// 组合（纯函数，运行时和构建期共享）
static std::optional<RuleComposedValues> ComposeRoundStrokeRect(
    const RoundStrokeRectInputs& inputs) {
  // 与维度取值相关的拒绝必须在这里
  // 穷举才能看到与运行时相同的拒绝
}
```

构建期通过 `EnumerateXxxReachable()` 穷举输入格子的笛卡尔积，逐一调用 Compose，收集所有非拒绝输出作为编译清单。运行时通过 Extract 获得真实输入，调用同一 Compose 获得匹配索引。

### 4.3 关键设计规则

以下三条纪律确保方案的正确性：

1. **拒绝必须放 Compose，不能只放 Extract。** 如果 `xpType < 0` 的拒绝留在 Extract，穷举会为 `xpType = -1` 编译出运行时永远不会使用的变体——正是要消灭的虚胖类 bug。

2. **Inputs 字段覆盖会影响结构或维度的 Extract 谓词。** 这是代码审查与规则测试维护的同步契约，不受 C++ 编译器自动证明。遗漏字段可能静默造成 runtime 匹配与可达集漂移，或导致 bundle 缺变体。

3. **枚举是输入空间声明。** 每个 `EnumerateXxx()` 必须覆盖 Inputs 每个字段的全部可取值；遗漏循环层会使 bundle 缺失对应变体。`--audit` 验证输出 pair 的结构合法性，但不自动证明输入 lattice 完备。

### 4.4 迁移过程与成果

30 条规则逐条迁移（每条一测一提交）。迁移阶段的 `--audit` 对旧 `ShouldCompile` 清单与 Compose 可达集做对账；终态删除 ShouldCompile 后，该模式改为验证可达集的结构合法性：

```
$ shader_build_tool --audit
[audit] summary: 30 audited, 0 violations
```

迁移过程中审计器捕获了 **第一个真实漂移**：PointwiseChain 规则可结构性产出 atlas+无叶链（`uvCoord=1, coverage=0, TEXTURE_COUNT=0`）× 2 色 × 3 XP = 6 个"从未编译→运行时静默落 JIT"的幽灵组合。根因是 atlas 链路由的存在意义就是采样 atlas 覆盖叶，无叶 atlas 链语义空洞（分解器恒附加 atlas 叶），规则代码却未拒绝它。修复：在 Compose 中加入语义 guard。

### 4.5 终局两步

迁移完成后执行了两步终结操作：

1. **生成器翻转**（commit `8bdacd475`）：`shader_build_tool` 的编译清单来源从"笛卡尔积 × IsBuildablePermutation"改为直接消费 `EnumerateReachablePermutations` 并集。bundle 内容验证与对账总数完全一致（308 条）。

2. **删除 ShouldCompile**（commit `629adc3b9`）：`PrecompiledShaderInfo` 的 `shouldCompile` 字段、9 个实际的 `ShouldCompile` 过滤函数，以及 `IsBuildablePermutation` 的该层过滤全部移除（37 文件，+101/−390 行）。由于 metadata 字段消失，30 个内核头的聚合初始化也同步调整。`IsBuildablePermutation` 保留为纯结构检查（索引范围 + 镜像维度一致），继续作为运行时防御。

---

## 5. 混合表优化

### 5.1 体积归因

单变体体积的支配者是 `xpBlendColors` 运行时混合模式表：

- 每个 PorterDuff 变体（~180/274 frag 条目）携带完整 ~30 模式公式表 ≈ 30KB SPIR-V
- Empty-XP 变体仅 ~1KB（ConstColor[0] = 956B vs [1]/[2] ≈ 31KB）
- 该表占 Vulkan/Metal 逻辑体积约 50%

表内部体积分布（nulling 实验测量）：

| 模式组 | 每变体体积 | 占表比 |
|---|---|---|
| HSL 模式（Hue/Sat/Color/Luminosity） | ~7.1KB | 23% |
| 循环模式（ColorDodge/Burn/SoftLight） | ~3.9KB | 13% |
| 三元模式（Overlay/HardLight） | ~1.8KB | 6% |
| 系数模式（SrcOver/Multiply/Screen 等 17 个） | ~11.5KB | 37% |
| 基础着色器体 | ~1.0KB | 3% |

### 5.2 结构层优化：三元分派提升

`pointwise_op.inc` 的 OP_BLEND 算子使用三元表达式分派函数调用：

```glsl
// 优化前：三元分派函数调用 → 编译器为每个分支内联一份完整混合表
result = BlendConstFirst != 0 ? xpBlendColors(ConstColorValue, color, BlendModeValue)
                              : xpBlendColors(color, ConstColorValue, BlendModeValue);

// 优化后：先选参数，再调函数 → 只内联一份
vec4 blendSrc = BlendConstFirst != 0 ? ConstColorValue : color;
vec4 blendDst = BlendConstFirst != 0 ? color : ConstColorValue;
result = xpBlendColors(blendSrc, blendDst, BlendModeValue);
```

这是一次纯结构重排——公式逐字不变，只是将"在两个 30KB 函数体之间选择"改为"在两个 vec4 值之间选择"（commit `060e74aea`）。5 个受影响内核各缩减 29-30%。

### 5.3 控制流重写：向量化选择

重型模式（Overlay、ColorDodge、ColorBurn、HardLight、SoftLight、HSL 族）的原始实现使用逐通道标量循环和分支树。重写为向量化的 `mix(x, y, bvec)` 选择式（commit `dbad6371b`）：

**数学基础**：

1. `mix(genType x, genType y, genBType a)` 在 GLSL 规范中定义为分量级真选择（SPIR-V `OpSelect`），非算术运算。被弃通道的值完全不参与结果。

2. 除法分母消毒：将 0 分母替换为 1.0，仅在将被 select 丢弃的通道上生效。所有参与运算的通道保持有限。

3. 逐通道算术表达式与原始标量分支的求值序完全一致，选定通道产生逐位一致的结果。

**等价性验证路径**：

```
float32 numpy 模拟（预检）
  ↓ 大多数模式逐位一致
AOT-vs-JIT 像素门禁（真 GPU 裁决）
  ↓ 全部 29 模式 maxDelta = 0
确认安全
```

---

## 6. 验证方法论

### 6.1 结构验证：审计模式

`shader_build_tool --audit` 验证每个内核已枚举可达集的结构有效性；它不证明 Inputs lattice 覆盖了 runtime 全部输入，也不证明真实产品 draw 覆盖达到 100%：

```
对每个 shader:
  reachable = EnumerateReachablePermutations(name)
  for each (vertIndex, fragIndex) in reachable:
    assert IsBuildablePermutation(info, vertIndex, fragIndex)
    // 索引在域范围内 + 镜像维度一致
```

不通过则退出码 1 并列出违规条目。

### 6.2 像素验证：AOT-vs-JIT 门禁

`ShaderPermutationTest.BlendModesMatchJIT` 是混合表的像素一致性门禁：

```
for each BlendMode in 29 modes (Clear 排除，Metal 预存崩溃):
  渲染参考图: bundle 卸载 → JIT 路径（运行时编译的混合代码）
  渲染候选图: bundle 加载 → AOT 候选路径（记录实际 AOT hit/miss）
  比较: maxChannelDiff ≤ 1, structuralDifference = false
  要求: 任一 AOT-served 模式不得与 JIT 参考图发生结构差异
```

场景以 8 条不同 RGB 的不透明背景带和一个半透明前景矩形构成，覆盖不同颜色关系下的混合公式分支。由于 Metal 上同一 pass 连续多次 PorterDuff draw 存在预存崩溃，门禁刻意限制为每个 render pass 一次 blend-mode draw。

### 6.3 运行时验证

`ShaderPermutationTest` 套件（Metal 43 / GL 30 测试）验证：
- 域编码/解码一致性
- matcher 规则匹配正确性
- bundle 加载与缓存命中
- AOT vs JIT 像素一致性（多个场景）
- 镜像维度合法性

---

## 7. 实测数据

### 7.1 三端压缩 bundle 体积

下表是 2026-08-19 使用同一构建工具对指定源码快照重新生成的**实验产物**，用于度量三元分派 hoist 与混合表重写的影响；它不是当前 `resources/shaders/` 已提交 bundle 的字节数。当前资源目录仍保存优化前 artifact payload（但格式与条目计数已同步）。

| 后端 | 实验基线 | hoist 后 | hoist + 重写 | 总变化 |
|---|---|---|---|---|
| OpenGL | 153KB | 153KB | 151KB | −1.4% |
| Vulkan | 1,774KB | 1,614KB | 1,293KB | **−27.1%** |
| Metal | 1,143KB | 1,086KB | 1,001KB | **−12.4%** |
| **三端合计** | **3,070KB** | 2,854KB | **2,445KB** | **−20.4%** |

当前 `resources/shaders/` 已提交 bundle 的压缩字节数为 OpenGL 153,286B、Vulkan 1,773,681B、Metal 1,143,309B；应由后续资源刷新流程决定何时采用优化后的实验产物。

### 7.2 Vulkan 逻辑体积逐内核明细

全部 28 个受影响内核缩减 8.7-14.7%，无一例外。最大绝对节省为 PointwiseChainShader（−274KB）。

### 7.3 变体统计

| 指标 | 数值 |
|---|---|
| 内核数 | 30 |
| 可达 permutation pair | 308（`shaderName, vertIndex, fragIndex`） |
| logical vertex-stage artifact | 101（`shaderName, vertIndex` 的并集） |
| logical fragment-stage artifact | 274（`shaderName, fragIndex` 的并集） |
| bundle stage entry | 375（101 vertex pool + 274 fragment pool） |
| raw domain pair | 4164（声明域笛卡尔积总和） |
| 已枚举可达集的结构合法性 | 已验证（audit 30/0 violations） |
| 变体维度种类 | 21 种编译期维度 |
| 运行时可设置参数 | ~130 个（所有内核声明的 uniform block 成员 + sampler 绑定的近似并集） |

---

## 8. 已否决的替代方案

以下方案经数学证明或实测数据否决：

| 方案 | 否决原因 | 依据 |
|---|---|---|
| BlendMode 拆为编译期维度 | 总字节不变（同一份代码分配到更多变体） | 数学证明：20 模式 × 180 变体 ≈ 3600 条目 × 容器开销 > 180 × 30KB |
| XP 维度分裂（系数 vs 高级） | 同上 | 数学证明：系数 11.5KB + 高级 19.2KB = 30.7KB = 全表 |
| shaderc size 优化等级 | 产出与 performance 等级逐字节相同 | 实测 +0.1% |
| 免内联编译管线（保留函数边界） | 多调用点内核受益 −4.6%，单调用点受损 +50%，总体 +21.9% | 实测 |
| Metal 单 metallib 打包 | 每条目容器开销仅 ~100-200B，375 条目节省 ~1% 压缩后 | `xcrun metallib` 合并实验 |
| Bundle 内容去重 | 24 对重复，压缩后 ~1-2% | 实测 |
| Metal function constants / VK spec constants | OpenGL 不支持 → 三后端双架构维护成本翻倍 | 架构分析 |

---

## 9. 遗留问题与未来方向

### 9.1 已知缺口

| 问题 | 状态 |
|---|---|
| Metal 同 pass 连续两次 PorterDuff draw 导致 SIGSEGV | 预存管线 bug，待修 |
| BlendMode::Clear 在 Metal 上崩溃 | 预存，门禁已跳过 |
| GL 无二进制程序缓存（`GL_ARB_get_program_binary`） | 待实现，首帧性能瓶颈 |
| GL 后端 17 个图片 2D 精度漂移 | 基线机制决策待定 |

### 9.2 远期方向

**Metal function constants / Vulkan specialization constants** 是远期架构研究方向：通过固定 fat ABI 与 pipeline-time specialization，可显著减少 stage artifact 重复；具体收益尚未原型测量。OpenGL 没有等价机制，因此若仅迁移 Metal/Vulkan 将引入双架构维护成本。OpenGL 退役会显著改善该方案的经济性，但不是技术上的唯一前提。

---

## 附录 A：可复现实验协议与解释边界

本文的计数和体积数据属于 2026-08-19 的特定源码快照。以下命令可在同一环境重新生成当前值：

```bash
# 结构合法性：所有已枚举 pair 的索引范围和 mirrored dimensions
./cmake-build-debug/shader_build_tool --audit

# 各后端 bundle 与逐 shader artifact report
./cmake-build-debug/shader_build_tool \
  --shader-dir src/gpu/shaders/glsl \
  --backends opengl,vulkan,metal \
  --compress \
  --out-dir /tmp/tgfx_shader_report

# Metal 真实 GPU 的混合表 AOT-vs-JIT 门禁
./cmake-build-debug-metal/TGFXFullTest_Metal \
  --gtest_filter='ShaderPermutationTest.BlendModesMatchJIT'
```

解释时应遵守以下边界：

1. `--audit` 证明的是已枚举可达 pair 的**结构合法性**，而不是 Inputs lattice 对 runtime 真实输入的完备证明。
2. stage artifact 数（101/274）与 permutation pair 数（308）是不同层次的计数，前者由后者对 stage index 投影得到。
3. bundle 中没有 PSO；AOT 只预置 stage artifact，runtime 仍创建 module 和 pipeline。
4. OpenGL bundle 内保存的是 GLSL 文本；其驱动编译延迟不能从 bundle 体积直接推导。
5. 体积报告中的 `unique*Count` 为内容相等分析统计，不能被误读为已写入 bundle 的 content-addressed 去重。

## 附录 B：设计章程

> **维度判据**：一个排列维度只有在改变资源绑定或 varying 布局时才成立，其余一切（值运算、算子选择、模式标志、颜色）皆为运行时 uniform。评审新维度时问一句"它 gate 什么 ABI 差异"，答案是"没有"就改 uniform。

## 附录 C：同步契约

> 新增 `*Inputs` 结构的字段必须同步三处：
> 1. **Extract**（PermutationMatcher.cpp）——从 ProgramInfo 赋值
> 2. **Compose**（PermutationRules.cpp）——在塑形维度或拒绝逻辑中使用
> 3. **Enumerate**（PermutationRules.cpp）——添加覆盖该字段所有可能取值的循环层
>
> 遗漏第 3 处导致该字段在穷举中只取默认值，bundle 缺失对应变体，受影响的绘制静默回退至运行时编译。`--audit` 模式仅验证可达集的结构有效性，不验证输入格子的完备性——Enumerate 循环是输入空间的唯一声明。

## 附录 D：源码导航索引（2026-08-19 快照）

以下引用用于将本文的关键陈述映射到当前实现。行号会随未来代码演进而变化；路径和符号名是长期主索引。

| 主题 | 源码位置 |
|---|---|
| shader metadata 与 `IsBuildablePermutation` | `src/gpu/shaders/PrecompiledShader.h:28-49`；`PrecompiledShader.cpp:34-42` |
| Compose/枚举分派 | `src/gpu/shaders/PermutationRules.cpp:1191-1284` |
| matcher 最终防御性检查 | `src/gpu/PermutationMatcher.cpp:1719-1739` |
| PointwiseChain 的 runtime structure gating 示例 | `src/gpu/PermutationMatcher.cpp:674-783` |
| 构建工具消费可达 pair 集 | `tools/shader_build_tool/main.cpp:340-355` |
| backend artifact 生成 | `tools/shader_build_tool/main.cpp:422-485` |
| Bundle v4 写入与 zstd 压缩 | `tools/shader_build_tool/BundleWriter.cpp:138-145`、`228-266` |
| Bundle v3/v4 与压缩类型加载兼容 | `src/gpu/PrecompiledShaderCache.cpp:425-474`、`539-601` |
| runtime AOT/JIT 选择 | `src/gpu/ProgramInfo.cpp:177-215` |
| runtime stage lookup 与 pipeline 创建 | `src/gpu/PrecompiledProgramCreator.cpp:232-405` |
| closure verifier 的 stage projection | `src/gpu/AOTClosureVerifier.cpp:55-83` |
| CMake 生产 bundle 命令 | `CMakeLists.txt:820-830` |
| 混合表 AOT-vs-JIT 门禁 | `test/src/ShaderPermutationTest.cpp:1811-1950` |

## 附录 E：关键 Commit 索引

| Commit | 内容 |
|---|---|
| `fb17f195` | 试点：RoundStrokeRect 迁移至 Compose 模式 + `--audit` 模式 |
| `8bdacd475` | 生成器翻转：消费规则可达集作为编译清单 |
| `629adc3b9` | 删除 ShouldCompile（37 文件，+101/−390 行） |
| `d523c2f15` | 设计章程与同步契约写入 PermutationRules.h |
| `060e74aea` | 三元分派 hoist（结构层优化，−7.8%） |
| `9a6a67d90` | AOT-vs-JIT 混合模式像素门禁（29 模式） |
| `dbad6371b` | 混合表向量化选择式重写（控制流重写，−11.8%） |
