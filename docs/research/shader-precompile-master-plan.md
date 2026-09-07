# TGFX Shader 预编译系统：设计与实施规范

**版本**: 2.0
**日期**: 2026-07-01
**状态**: 设计规范（终稿）

---

## 目录

1. [引言](#1-引言)
2. [现有系统分析](#2-现有系统分析)
3. [目标架构：多 Pass 分解](#3-目标架构多-pass-分解)
4. [关键技术决策](#4-关键技术决策)
5. [原子 Shader 变体规模分析](#5-原子-shader-变体规模分析)
6. [静态 Shader 源文件规范](#6-静态-shader-源文件规范)
7. [构建期声明与代码生成](#7-构建期声明与代码生成)
8. [构建工具设计](#8-构建工具设计)
9. [Bundle 格式规范](#9-bundle-格式规范)
10. [运行时集成设计](#10-运行时集成设计)
11. [转写与验证方法论](#11-转写与验证方法论)
12. [关键实现问题与解决方案](#12-关键实现问题与解决方案)
13. [验证体系](#13-验证体系)
14. [成本收益分析](#14-成本收益分析)
15. [风险评估与缓解措施](#15-风险评估与缓解措施)
16. [实施路线](#16-实施路线)
17. [持续维护规范](#17-持续维护规范)

---

## 1. 引言

### 1.1 研究背景与问题定义

TGFX 当前采用运行时动态 Shader 编译管线：通过 GP/FP/XP 树动态拼接 GLSL 源码，经 `PreprocessGLSL()` 添加 binding/location 限定符，由 shaderc 编译为 SPIR-V，再经 SPIRV-Cross/Tint 翻译为目标平台着色语言（MSL/WGSL），最终提交 GPU 驱动编译。

该架构存在四项核心缺陷，其中第 4 项是本次方案选型的决定性因素：

1. **首帧性能**：单个 Program（VS + FS）首次创建需经历完整的编译翻译链路，应用首帧通常触发 10–20 个不同 Program 变体，累积拼接+转换耗时 15–70 ms。
2. **运行时二进制膨胀**：shaderc（含 glslang + SPIRV-Tools）、SPIRV-Cross、Tint 三套编译器以静态库形式链入最终产物，Release arm64 下合计约 9–14 MB。
3. **代码可维护性**：约 4000–6600 行 C++ 通过 `codeAppendf()` 格式化拼接 GLSL 字符串，形成"代码中的代码"模式，不可调试、不可离线验证。
4. **结构性开放组合空间**（决定性问题）：GP + FP[] + XP 的拼接模型使得 shader 变体空间是**运行时开放**的——下游 App 无需修改 tgfx 源码，仅通过 Canvas API 的合法调用（如 `Shader::MakeBlend()`、`ColorFilter::Blend()` 任意组合 BlendMode）即可产生任何编译期穷举方案都未覆盖的新 `programKey`。这意味着"编译期枚举全部变体"这条路径本身就不成立，无论枚举后的变体数是 800 还是 8000。

### 1.2 设计目标与约束条件

**目标**：将 shader 的拼接与转换从渲染提交的关键路径上**彻底消除**。构建期完成 GLSL → SPIR-V → MSL/WGSL 的全链路语言翻译；运行时通过调度已编译的原子 Shader 完成效果组合，不产生新的 shader 源码，因此不存在"未覆盖变体"的可能性。

**约束条件**：

| 编号 | 约束 | 说明 |
|:----:|------|------|
| C1 | 交付范围 | 单版本一次性交付全部 ~359 个原子 Shader，不分阶段留存旧路径（详见 §16.1 交付策略理由） |
| C2 | 过渡期动态路径 | 交付前的开发阶段允许保留 `ProgramBuilder::CreateProgram()` 作为对照验证基线，交付后移除 |
| C3 | 平台无关 | Bundle 格式与加载抽象不绑定特定平台；当前仅覆盖 OpenGL/Vulkan/Metal/WebGPU 4 个已实现后端 |
| C4 | 确定性 | 相同输入保证相同输出（缓存安全性） |
| C5 | 包体敏感 | 预编译 Bundle 单后端压缩后控制在 1 MB 以内（原子 Shader 数有界，远低于开放组合空间的预算） |
| C6 | RuntimeEffect 不受影响 | 用户自定义着色器保持独立的 `CommandEncoder → GPU::createShaderModule()` 路径，不经过本方案的原子 Shader 管线 |

### 1.3 范围限定（重要）

本文档**仅**讨论 Shader **拼接与转换**阶段（GLSL 拼接 → SPIR-V → MSL/WGSL 跨语言翻译）的消除方案。Driver 侧的最终编译——`glCompileShader`+`glLinkProgram`（OpenGL）、`newLibraryWithSource:`（Metal）、SPIR-V→GPU ISA（Vulkan 驱动内部）——属于独立问题，**不在本文range内**。

这一限定直接影响全文对"性能收益"的表述口径：§14.1 表格中"0 ms"指的是拼接+转换阶段耗时归零，**不代表 Pipeline 创建总耗时归零**。Metal 后端即便拿到预编译 MSL text，仍需驱动把 MSL 编译为 ISA；OpenGL 后端即便拿到预编译 GLSL text，仍需 `glCompileShader` 走驱动编译。这两个后端的首帧改善幅度明显小于 Vulkan/WebGPU（后两者可直接把预编译产物交给驱动，不经过任何文本级编译步骤）。

### 1.4 术语定义

| 术语 | 定义 |
|------|------|
| Processor | 着色器逻辑的最小功能单元（GP/FP/XP），负责生成一段 GLSL 代码片段并计算唯一标识 Key |
| 原子 Shader (Atomic Shader) | 一个独立预编译的 VS+FS pair，只负责一种固定的 Processor 组合，不感知其他原子 Shader 的存在 |
| Level 1 Shader | 基础渲染层原子 Shader：GP × 常见 colorFP × ±coverageFP 的固定组合 |
| Level 2 Shader | 效果处理层原子 Shader：独立的 colorFilter/maskFilter 后处理 |
| Level 3 Shader | 合成层原子 Shader：高级 BlendMode（DstTexture 读取型）合成 |
| EffectDecomposer | 运行时组件，判断一次 draw 能否匹配单个原子 Shader（快速路径），否则分解为多个 Pass |
| PipelineKey | 唯一决定一次绘制调用完整 Pipeline State Object 的 Key，含 shaderID + variantIndex + 渲染状态 |
| PrecompiledShaderCache | 构建期产物的运行时加载器，按 PipelineKey 索引查找已转换的 Shader 二进制/文本 |
| ProgramBuilder | 现有运行时动态拼接系统，交付后移除（`RuntimeEffect` 走独立路径不受影响） |

### 1.5 文档范围与读者对象

本文档整合 `multi-pass-blend-proposal-review.md`（架构方案与可行性论证）与 `multi-pass-blend-risks.md`（风险审计结论）两份文档的结论，构成多 Pass 分解架构的完整施工规范。读者为实施此方案的工程师。

---

## 2. 现有系统分析

### 2.1 Shader 生成流水线架构

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    TGFX 运行时 Shader 编译管线（当前实现）                      │
├─────────────────────────────────────────────────────────────────────────────┤
│  OpsCompositor::addDrawOp()                                                  │
│       │  brush.shader/colorFilter/maskFilter → 动态构造 FP 链                │
│       ▼                                                                     │
│  DrawOp::execute()                                                           │
│       ▼                                                                     │
│  ProgramInfo::getProgram()                                                   │
│       ├─ 1. 计算 programKey (BytesKey)                                      │
│       │      ├─ GP.computeProcessorKey()                                    │
│       │      ├─ FP[i].computeProcessorKey()（递归含子 FP，可深度嵌套）        │
│       │      └─ XP.computeProcessorKey() + blendMode + format + ...        │
│       ├─ 2. 查询 GlobalCache (内存 LRU)                                     │
│       │      └─ 命中 → 直接返回 Program                                     │
│       └─ 3. 未命中 → ProgramBuilder::CreateProgram()                        │
│              ├─ emitAndInstallProcessors() → 拼接完整 VS/FS GLSL 源码       │
│              ├─ [各后端] shaderc → SPIR-V → spirv-cross/tint → MSL/WGSL     │
│              └─ GPU::createShaderModule() + createRenderPipeline()          │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 各后端完整编译链路

> 当前工程已实现的后端为 **OpenGL / Vulkan / Metal / WebGPU 四个**（`include/tgfx/gpu/Backend.h` 的 `Backend` enum 无 DX12）。DX12 仅作为潜在未来扩展方向，本文档不将其作为既有事实描述。

| 后端 | 编译链路 | 跨语言翻译 | 运行时依赖 |
|------|---------|-----------|-----------|
| OpenGL | emitCode → GLSL → glCompileShader | 无 | 无 |
| Vulkan | emitCode → GLSL → PreprocessGLSL → shaderc → SPIR-V → vkCreateShaderModule | 无 | shaderc |
| Metal | emitCode → GLSL → PreprocessGLSL → shaderc → SPIR-V → spirv-cross → MSL → MTLLibrary | SPIR-V→MSL | shaderc + SPIRV-Cross |
| WebGPU | emitCode → GLSL → preprocessGLSL → shaderc → SPIR-V → tint → WGSL → WGPUShaderModule | SPIR-V→WGSL | shaderc + tint |

### 2.3 Processor 体系与 Key 计算

TGFX 现有约 12 个 GeometryProcessor、20+ 个 FragmentProcessor、2 个 XferProcessor（精确清单以 `src/gpu/processors/*.h` 中 `DEFINE_PROCESSOR_CLASS_ID` 的实际数量为准，本文档不依赖某个固定总数做架构决策）。每个 Processor 通过 `computeProcessorKey()` 向 `BytesKey` 写入标识：

- **GP**：写入 `classID()` + 子类特化参数 + 顶点属性格式
- **FP**：写入 `classID()` + 子类特化参数 + 纹理相关信息（**注意**：纹理格式/类型会实际影响生成的 GLSL 文本，详见 §12.2）+ 递归子 FP Key
- **XP**：写入 `classID()` + 子类特化参数

**核心问题**（本文档要解决的根本问题）：programKey 是 GP.key + FP[].key（递归含任意深度嵌套）+ XP.key 的**拼接**，而 FP 树的嵌套深度和组合方式由用户在运行时通过 Canvas API 决定（如 `Shader::MakeBlend(mode, shaderA, shaderB)`、`ColorFilter::Blend(color, mode)` 的任意组合）。这不是一个可以在构建期完整枚举的封闭空间。

### 2.4 性能瓶颈定性分析

以中等复杂度 Fragment Shader（1 GP + 2 FP + 1 XP）为基准，各阶段开销占比：

| 阶段 | 开销级别 | 说明 |
|------|:--------:|------|
| GLSL 拼接（emitCode + string concat） | 极低 | 纯字符串操作，<0.5 ms |
| GLSL 预处理（正则替换） | 低 | 全文扫描匹配 |
| SPIR-V 编译（shaderc） | **高** | 1–5 ms/次，完整词法/语法/语义分析 |
| SPIRV-Cross/Tint 翻译 | 中 | 0.5–2 ms/次 |
| 平台驱动编译 | **高** | GPU 驱动编译，本方案不涉及此阶段优化 |

首帧触发 10–20 个新变体时，拼接+转换累积耗时达 15–70 ms（详见 `multi-pass-blend-proposal-review.md` §1.1）。

### 2.5 运行时依赖体积

| 库 | Release .a（arm64） |
|----|:------------------:|
| shaderc（含 glslang + SPIRV-Tools） | ~7–13 MB |
| SPIRV-Cross | ~2–3 MB |
| tint（WebGPU 后端） | ~4–6 MB |
| **合计** | **~9–14 MB** |

---

## 3. 目标架构：多 Pass 分解

### 3.1 核心原则

> **一个 draw = 一个预编译原子 Shader（快速路径）。效果叠加 = 多次 draw + 中间 Texture 传递（分解路径）。**

- 每个原子 Shader 只负责一种固定的 Processor 组合，构建期编译完成，运行时不产生新代码
- 当 draw 涉及的效果组合超出预编译覆盖范围（约 5.4%，见 §5.2），由 `EffectDecomposer` 将其分解为多个 Pass
- Pass 之间通过 `ProxyProvider` + `ResourceCache` 分配的中间 Texture 传递结果

这从根本上解决了 §2.3 描述的开放组合空间问题：**效果组合从"编译新 shader"变为"调度已有 shader 的执行顺序"**，原子 Shader 总数由 tgfx 内部封闭的 `Shader`/`ColorFilter`/`MaskFilter` 继承体系决定（详见 `multi-pass-blend-proposal-review.md` §A.2），外部 App 无法通过合法 API 调用产生超出预编译范围的原子 Shader。

### 3.2 架构分层

```
┌──────────────────────────────────────────────────────────────┐
│  Layer 1: Canvas API（不变）                                  │
└──────────────┬───────────────────────────────────────────────┘
┌──────────────▼───────────────────────────────────────────────┐
│  Layer 2: OpsCompositor + EffectDecomposer（新增判定逻辑）     │
│    → 匹配预编译原子 Shader？→ 快速路径（单 Pass）              │
│    → 不匹配？→ 分解为多个 Pass（Pass A/B/C/D + 中间 Texture）  │
└──────────────┬───────────────────────────────────────────────┘
┌──────────────▼───────────────────────────────────────────────┐
│  Layer 3: DrawOp::execute()                                   │
│    → 不调用 ProgramBuilder 拼接 GLSL                           │
│    → 直接按 PipelineKey 查找 PrecompiledShaderCache            │
│    → 交给现有 GPU::createShaderModule/createRenderPipeline     │
└──────────────┬───────────────────────────────────────────────┘
┌──────────────▼───────────────────────────────────────────────┐
│  Layer 4: PrecompiledShaderCache（新增）                       │
│    应用启动时加载构建期转换好的 Bundle                          │
│    运行时按 PipelineKey 二分查找 → 取出后端专属 Shader 二进制/文本│
└──────────────────────────────────────────────────────────────┘
```

### 3.3 快速路径 vs 分解路径

基于 802 次实际 draw 调用采样（详见 `multi-pass-blend-proposal-review.md` §3.2，采样来自测试用例，非生产环境，代表性需后续验证但不阻塞开发）：

| 场景 | 是否需要分解 | 占比 |
|------|:--------:|:----:|
| 纯色几何 / 纹理+clip / 渐变+clip / 文字 atlas 等简单 draw | 否 | **94.3%** |
| 纹理/渐变 + colorFilter | 是 | 3.7% |
| 带 maskFilter | 是 | 1.6% |
| **需分解小计** | | **5.4%** |

`EffectDecomposer` 的判定条件（详见 §10.2）：`colorFilter == nullptr && maskFilter == nullptr && blendMode` 属于硬件 blend 可表达的范围 且 colorFP/coverageFP 类型命中已知的 Level 1 Shader。

### 3.4 数据流全程规范

```
1. 开发者编写原子 Shader 源文件（每个 Processor 组合对应一个独立文件）：
   src/gpu/shaders/level1/texture_fill.frag  （含 //! 元数据声明，见 §7.2）

2. 构建工具 build_shaders.py 执行：
   ① 编译一个 shader_reflect 可执行文件解析 //! 元数据（不用正则解析 C++，见 §8.3）
   ② 枚举有效变体 → 逐变体宏展开为独立 GLSL
   ③ glslangValidator → SPIR-V → spirv-cross/tint → MSL/WGSL；OpenGL 直接使用 GLSL
   ④ 通过 SPIR-V reflection 自动生成 uniform/sampler binding 布局（见 §12.4）
   ⑤ 按 PipelineKey hash 排序索引 → 打包为 Bundle

3. 运行时：
   OpsCompositor::addDrawOp()
     → EffectDecomposer::decompose(brush, clip, blendMode)
     → 命中快速路径：单个 PipelineKey → PrecompiledShaderCache::find() → 直接创建 Pipeline
     → 未命中：分解为多 Pass，每个 Pass 独立走上述查找流程，中间结果写入 Texture
```

### 3.5 不变量声明

以下子系统在本方案中不发生改变：

- **`RuntimeEffect`**：用户自定义着色器保持独立的运行时编译路径，不经过本方案管线
- **`GlobalCache` 的 LRU 缓存机制**：作为 Pipeline 级缓存层继续存在
- **DrawOp 的 record/execute 两阶段调度**：`OpsCompositor`、`OpsRenderTask` 的整体流程不变，仅在 `addDrawOp()` 内新增 `EffectDecomposer` 判定
- **截图测试框架**：作为迁移正确性的验证手段

### 3.6 净效果量化

| 维度 | 当前 | 迁移后 |
|------|------|--------|
| 首帧拼接+转换耗时 | 15–70 ms | **0 ms** |
| 稳态帧 94.3% draw | 1 draw call | 1 draw call（**相同**） |
| 稳态帧 5.4% draw | 1 draw call | 2–3 draw calls + 中间 Texture |
| 运行时二进制体积 | 含编译器 ~9–14 MB | 交付后可移除（`RuntimeEffect` 场景仍需保留，见 §12.6） |
| Shader 变体空间 | 运行时开放 | **~359 个封闭集合** |

---

## 4. 关键技术决策

### 4.1 变体管理策略选型

**决策：采用纯 Permutation（编译期枚举），不使用 Specialization Constant**。

原理由保持不变，与单/多 Pass 选型正交：

1. **结构性差异不可表达**。GP 的多数 key bit 是结构性的——改变 vertex attribute 声明数量、varying 数量、uniform 存在与否，Specialization Constant 无法增减 shader 的 input/output 声明。
2. **后端兼容性**。OpenGL 无可靠的 Specialization Constant 等价机制，而 tgfx 需要支持全部已实现后端。
3. **确定性与可调试性**。每个 Permutation 是一份完整独立的 GLSL 文件，可离线编译验证。

### 4.2 Shader 组合粒度

**决策：采用多 Pass 分解，废弃单 Pass 拼接模型**（详见 `multi-pass-blend-proposal-review.md` 全文）。

理由：

1. Coverage 传递语义按 BlendMode 分为 A/B 两类均已验证等价——A 类（SrcOver/DstOver/Screen 等，`primaryOutputType == Modulate` 且不需要 secondary output）走硬件 blend，coverage 以 `color × coverage` 乘积形式编码在中间 Texture 像素中天然保留；B 类（Clear/Src/SrcIn/DstIn 等）当前就走 DstTexture shader 读取合成，多 Pass 下行为一致（详见 §15.1 风险审计）。
2. GP + FP[] + XP 拼接模型是**运行时开放组合空间**，多 Pass 分解将原子 Shader 数收敛为 tgfx 内部封闭继承体系决定的有界集合（~359 个，见 §5）。
3. Flutter Impeller 已在 iOS/Android 亿级设备生产环境验证此模式的可行性（全部 shader 构建期预编译，效果组合 = 多 Pass + 中间 texture）。tgfx 方案比 Impeller 更保守：94.3% 场景维持单 Pass 性能。

**决策依据**：多 Pass 方案结构性封闭了开放组合空间，是单 Pass 路线无法达成的。

### 4.3 Shader Key 体系的分层设计

**决策：采用三层 Key 模型**，避免单一扁平 Key 遗漏 Sampler 属性、ShaderCaps profile 等 shader 可见因素：

```
StableProcessorKey  = constexpr ClassID（见 §12.1）+ 该 Processor 自身的 Permutation 参数

ShaderShapeKey       = 单个原子 Shader 的 Processor 组合 StableProcessorKey
                     + Sampler 的 shader 可见属性（TextureType、读 swizzle、YUV layout，见 §12.2）
                     + ShaderCaps profile token（framebuffer fetch 模式等，见 §12.5）
                     + outputSwizzle
                     → 唯一决定该原子 Shader 的源代码文本

PipelineKey          = shaderID（~359 个原子 Shader 的唯一编号）+ variantIndex
                     + render target format + sampleCount
                     + blendMode + cullMode + colorWriteMask + depth/stencil state
                     → 唯一决定完整 Pipeline State Object
```

**收益**：Bundle 按 `shaderID + variantIndex` 索引原子 Shader 二进制；`PipelineKey` 在此基础上叠加渲染状态用于运行时 Pipeline 查找/创建。

### 4.4 Bundle 索引键的选取

**决策：使用 `PipelineKey` 的 128-bit hash 作为 Bundle 索引，索引表按 (hi, lo) 双字段字典序排序**，确保 `hi` 相同时仍可通过 `lo` 精确定位。

构建期校验 128-bit hash 全局无重复；运行时计算 `PipelineKey` 后取 128-bit hash，先比较 hi 再比较 lo 做二分查找。

### 4.5 OpenGL 后端的处理

**决策：OpenGL 按 `ShaderCaps` profile 拆分为多个 Bundle 文件，而非单一 `opengl.tgfxshader`**。

理由：`src/gpu/ShaderCaps.cpp` 中 framebuffer fetch 根据设备 extension（`GL_EXT_shader_framebuffer_fetch` / `GL_ARM_shader_framebuffer_fetch` / 不支持）动态选择不同的 GLSL 代码路径，单一 Bundle 无法覆盖。此外 OpenGL ES 与 OpenGL Desktop 使用不同的 GLSL 版本（ES 300 vs Desktop 330），二者语法不完全兼容，必须作为独立 profile：

```
opengl_es300_no_fbfetch.tgfxshader
opengl_es300_ext_fbfetch.tgfxshader
opengl_es300_arm_fbfetch.tgfxshader
opengl_desktop330_no_fbfetch.tgfxshader
opengl_desktop330_ext_fbfetch.tgfxshader
```

运行时根据设备类型（ES/Desktop）和能力探测结果选择对应 profile 的 Bundle 加载。

---

## 5. 原子 Shader 变体规模分析

### 5.1 三层原子 Shader 划分

多 Pass 架构下，任意一种理论效果组合（GP × colorFP × coverageFP × XP × BlendMode 的笛卡尔积，理论上界约 54,000）可分解为 1–4 个原子 Shader 的有序串联，每层原子 Shader 独立构建期穷举（详见 `multi-pass-blend-proposal-review.md` §A.2、§C）：

| 层次 | 功能 | 数量 | 说明 |
|:----:|------|:----:|------|
| **Level 1** | 基础渲染：GP × 常见 colorFP × ±coverageFP 固定组合 | ~295 | 覆盖全部 12 种 GP；含 TextureEffect(~40)、TiledTextureEffect(~80)、Gradient 系列、PerlinNoise(64 = 8[numOctaves]×2[noiseType]×2[stitchTiles]×2[AA]，API 层将 `numOctaves` 上限从 255 收紧为 8，完整覆盖收紧后的全部取值，不留 miss 缺口) 等 |
| **Level 2** | 效果处理：独立 colorFilter/maskFilter 后处理 | ~49 | color_matrix、color_blend(15 mode)、luma、alpha_threshold、gaussian_blur、shader_mask、alpha_mask_blend、shader_blend(15 mode) 等 |
| **Level 3** | 合成：高级 BlendMode（走 DstTexture 的 B 类模式） | ~15 | advanced_blend，对应 W3C Compositing Level 1 定义的 15 种高级混合公式 |
| **合计** | | **~359** | 封闭集合，由 tgfx 内部继承体系决定上界，不随下游 App 的 API 调用组合方式增长 |

### 5.2 全覆盖的充分条件

以下三点保证 ~359 个原子 Shader 可覆盖所有 Brush 层面的实际使用场景（详见 `multi-pass-blend-proposal-review.md` §A.2.3）：

1. `RuntimeEffect` 走独立的 `CommandEncoder → GPU::createShaderModule()` 路径，不经过本方案管线，不影响覆盖范围
2. 每种 FP 子类的 Permutation 维度均为引擎内部枚举值。`PerlinNoiseFP.numOctaves` 是唯一需要 API 层介入的维度：当前 clamp 到 255，需收紧为 8（视觉依据：>8 阶振幅衰减 < 0.4%，肉眼不可分辨）——**收紧后必须完整覆盖 1–8 全部取值（64 变体，见 §5.1），不能只精编常用的低阶值再对高阶值降级**，否则 5–8 阶会落入 Bundle miss，与"原子 Shader 集合封闭、不存在 miss 可能"的核心设计原则矛盾
3. `FP` 嵌套模式有界：审计确认仅存在 6 种固定嵌套结构（`AARectEffect`、`DeviceSpaceTextureEffect`、`TextureEffect` shape mask、`MulInputByChildAlpha` 组合、二者的 `Compose` 组合、`GaussianBlur1DFP`），可完整枚举

### 5.3 产物体积预算

| 后端 | 单 Shader 平均体积 | ~359 个总体积 | 压缩后 |
|------|:------:|:------:|:------:|
| SPIR-V (Vulkan) | ~2 KB | ~718 KB | ~360 KB |
| MSL text (Metal) | ~2 KB | ~718 KB | — |
| WGSL text (WebGPU) | ~2 KB | ~718 KB | — |
| GLSL text (OpenGL，每个 profile 独立) | ~2 KB | ~718 KB/profile | — |

C5 约束"单后端压缩后控制在 1 MB 以内"按**运行时实际加载的单个 Bundle** 计算，而非仓库中全部 profile 的总和——OpenGL 现有 5 个 profile（§4.5：ES 300 × 3 + Desktop 330 × 2），设备运行时只根据自身能力探测结果加载其中 1 个，单个 profile ~718 KB 远低于预算；仓库中 5 个 profile 全部入库（§8.6）的总体积约 3.6 MB，属于源码仓库体积而非运行时内存/包体占用，具体打包时按目标平台裁剪不需要的 profile。

远低于 C5 约束，且原子 Shader 数不随 App 使用场景增长。

---

## 6. 静态 Shader 源文件规范

### 6.1 文件组织与命名

```
src/gpu/shaders/
├── level1/                          # 基础渲染层：GP × colorFP × coverageFP
│   ├── solid_fill.vert / .frag
│   ├── texture_fill.vert / .frag
│   ├── tiled_texture_fill.vert / .frag
│   ├── linear_gradient.frag / radial_gradient.frag / sweep_gradient.frag / diamond_gradient.frag
│   ├── rrect_fill.frag
│   ├── atlas_text.frag
│   └── perlin_noise.frag
├── level2/                          # 效果处理层
│   ├── color_matrix.frag
│   ├── color_blend.frag
│   ├── luma.frag / alpha_threshold.frag
│   ├── gaussian_blur.frag
│   ├── shader_mask.frag
│   ├── alpha_mask_blend.frag        # affectsTransparentBlack 分解用
│   └── shader_blend.frag            # BlendShader 分解用
├── level3/                          # 合成层
│   └── advanced_blend.frag
├── common/                          # 共享代码
│   ├── blend_modes.glsl
│   ├── gradient_colorizer.glsl
│   ├── coord_transform.glsl
│   └── premultiply.glsl
└── build_shaders.py                 # 构建脚本
```

每个原子 Shader 文件对应**一种固定的 Processor 组合**（例如 `texture_fill.frag` = QuadPerEdgeAAGP + TextureEffect + ±AA coverage 的固定组合），不存在"Processor 模板"与"Pipeline 组装产物"的额外中间层。

### 6.2 GLSL 版本与跨后端兼容规则

所有静态 Shader 文件使用 **GLSL 4.50** 编写，构建工具离线降级到各后端所需版本：

| 目标后端 | 构建工具处理 |
|---------|------------|
| Vulkan | GLSL 4.50 → glslangValidator → SPIR-V（直接使用） |
| Metal | SPIR-V → spirv-cross → MSL 2.3 |
| WebGPU | SPIR-V → tint → WGSL |
| OpenGL ES | SPIR-V → spirv-cross --output glsl --version 300es（按 §4.5 profile 生成对应扩展声明） |
| OpenGL Desktop | SPIR-V → spirv-cross --output glsl --version 330（独立于 ES 生成，语法不完全兼容） |

### 6.3 Shader 文件内容规范

原子 Shader 文件仅包含**纯着色算法 + 头部 `//! ` 元数据声明**（元数据格式见 §7.2），declarations（uniform/sampler/varying）由构建工具根据元数据自动注入：

```glsl
// texture_fill.frag

//! vertex: level1/default_quad.vert
//! permutation: FORMAT        enum  rgba alpha_only rgbaaa
//! permutation: YUV           enum  none i420_limited i420_full nv12_limited nv12_full
//! permutation: HAS_SUBSET    bool
//! permutation: STRICT        bool
//! permutation: AA_COVERAGE   bool
//! exclude: FORMAT=rgbaaa && YUV!=none
//! exclude: FORMAT=alpha_only && YUV!=none

void main() {
#if HAS_SUBSET
    vec2 coord = clamp(v_texCoord, u_subset.xy, u_subset.zw);
#else
    vec2 coord = v_texCoord;
#endif
    vec4 color = texture(u_sampler, coord);
#if FORMAT == 1
    color = vec4(0.0, 0.0, 0.0, color.a);
#endif
    fragColor = color;
}
```

### 6.4 Uniform 与 Attribute 命名规范

| 类型 | 命名模式 | 示例 |
|------|---------|------|
| Uniform | `u_{name}` | `u_colorMatrix` |
| Sampler | `u_sampler{N}` | `u_sampler0` |
| Varying | `v_{name}` | `v_texCoord` |
| Fragment output | `fragColor` | — |

> 注：原子 Shader 不存在多 Processor 拼接，因此不需要 mangling 后缀区分同名变量。

---

## 7. 构建期声明与代码生成

### 7.1 设计约束与格式选型

**决策：使用 Shader 文件头 `//!` 内联元数据，不使用独立 `.def` 文件或 C++ 宏声明**（详见 `multi-pass-blend-proposal-review.md` §4.2 决策点 #4 的四方案对比）。

理由：
1. 构建期外部工具链（Python）无法读取 C++ 类型信息，排除纯 C++ 宏方案
2. `//!` 内联元数据实现**零信息分离**——打开 shader 文件即可看到变体维度、排除规则和源码，新增/修改 shader 只需改一个文件
3. 与 §7.4 的 SPIR-V 反射天然契合——shader 文件是唯一真相源

### 7.2 元数据语法

```glsl
//! vertex: {顶点 shader 路径}
//! permutation: {NAME} bool
//! permutation: {NAME} enum {value1} {value2} ...
//! permutation: {NAME} int {max}
//! exclude: {条件表达式}
```

### 7.3 元数据解析方式

**决策：不使用正则解析元数据文本的语义，而是解析为结构化 token 后交给编译期校验**。

由于元数据是本方案专门设计的结构化行（`//!` 前缀 + 固定关键字），逐行正则匹配是可靠的（不同于解析任意 C++ 语法）：

```python
BOOL_PATTERN = r'//!\s*permutation:\s*(\w+)\s+bool'
ENUM_PATTERN = r'//!\s*permutation:\s*(\w+)\s+enum\s+(.+)'
INT_PATTERN = r'//!\s*permutation:\s*(\w+)\s+int\s+(\d+)'
EXCLUDE_PATTERN = r'//!\s*exclude:\s*(.+)'
VERTEX_PATTERN = r'//!\s*vertex:\s*(.+)'
```

### 7.4 构建期代码生成

```
src/gpu/shaders/**/*.frag (含 //! 元数据)
    → [build_shaders.py]
        → ShaderPermutations_generated.h  （C++ 枚举 + 变体索引，供运行时 EffectDecomposer 使用）
        → ShaderBindings_generated.h      （C++ uniform binding struct，由 SPIR-V reflection 自动生成）
        → SPIR-V / MSL / WGSL / GLSL 编译产物
        → Shader Bundle (.tgfxshader)
```

生成的 uniform binding struct 通过 SPIR-V reflection（而非手写声明）自动生成，避免 ~359 个 shader 手写 binding struct 的维护负担（详见 §12.4）。

---

## 8. 构建工具设计

### 8.1 工具定位与技术栈

**定位**：CLI 工具，输入 `.vert`/`.frag`（含 `//!` 元数据），输出各后端 Bundle 文件。

**技术栈**：Python 3.10+ 驱动，调用 `glslangValidator` / `spirv-cross` / `tint` 的命令行工具。

### 8.2 有效变体枚举与排除

1. 解析 `//! permutation:` 声明，计算笛卡尔积
2. 应用 `//! exclude:` 声明式规则过滤无效组合（声明式规则不需要编译执行小型可执行文件来查询每个组合，构建速度更快且不依赖 C++ 编译环境）
3. 输出有效变体列表

### 8.3 编译与跨后端翻译阶段

| 目标 | 工具 | 配置 |
|------|------|------|
| SPIR-V (Vulkan) | glslangValidator | GLSL 4.50 → SPIR-V |
| MSL (Metal) | spirv-cross | MSL 2.3 |
| WGSL (WebGPU) | tint | spirv reader → wgsl writer |
| GLSL (OpenGL，多 profile） | spirv-cross | `--output glsl --version 300es`，按 §4.5 各 profile 独立生成 |

### 8.4 SPIR-V Reflection 生成 Binding 布局

**决策：Uniform 填充机制采用 SPIR-V reflection 自动生成，而非手写填充**（详见 `multi-pass-blend-proposal-review.md` §4.2 决策点 #6）。

理由：~359 个 shader 手写 binding struct 维护成本高且易出错；shader 文件是唯一真相源，编译为 SPIR-V 后反射 uniform 布局并自动生成 C++ binding struct 是构建流水线的自然副产物，前期投入可控。

### 8.5 CLI 接口规范

```bash
python build_shaders.py \
  --input src/gpu/shaders/ \
  --output bundles/ \
  --backend vulkan,metal,webgpu,opengl \
  --jobs 8
```

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--input` | Shader 源文件根目录 | 必填 |
| `--output` | Bundle 输出目录 | 必填 |
| `--backend` | 目标后端（逗号分隔，OpenGL 按 §4.5 展开为多 profile） | `vulkan,metal,webgpu,opengl` |
| `--jobs` | 并行编译线程数 | CPU 核数 |
| `--incremental` | 启用增量编译（记录源文件 SHA-256 hash） | 默认开启 |

全量构建耗时 ~17–39 s，8 核并行约 3–5 s；增量构建 < 1 s（详见 `multi-pass-blend-proposal-review.md` §F.5）。

### 8.6 产物入库策略

**决策：SPIR-V/MSL/WGSL 产物随源码一起提交到 Git 仓库**（详见决策点 #5）。理由：外部 App 已在使用 tgfx，不能要求外部开发者安装 glslangValidator/spirv-cross/tint 三套工具链。CI 校验源码哈希与产物一致性防止过期。

---

## 9. Bundle 格式规范

### 9.1 文件结构总览

```
┌─────────────────────────────────────────────────────────┐
│ TGFXShaderBundle (.tgfxshader)                          │
├─────────────────────────────────────────────────────────┤
│ File Header                                              │
├─────────────────────────────────────────────────────────┤
│ Index Table (entryCount × sizeof(IndexEntry))            │
├─────────────────────────────────────────────────────────┤
│ Reflection Data Pool（uniform/sampler/attribute 布局）    │
├─────────────────────────────────────────────────────────┤
│ Shader Data Pool（VS/FS 二进制或文本，4-byte 对齐）       │
└─────────────────────────────────────────────────────────┘
```

Reflection Data Pool 用于承载 §8.4 生成的 uniform/sampler binding 信息——Bundle 命中后仅凭 Processor 声明无法构造完整的 vertex/fragment uniform 列表、sampler binding、attribute layout 等运行时创建 Program 所需的全部信息，因此需要独立的反射数据池承载这部分信息。

### 9.2 文件头定义

```c
struct FileHeader {
    uint32_t magic;                // 4  "TGSH"
    uint32_t version;              // 4  格式版本
    uint32_t backend;              // 4  0=Vulkan,1=Metal,2=WebGPU,3=OpenGL（无 DX12）
    uint32_t glProfile;            // 4  OpenGL profile 编号（非 OpenGL 后端为 0，见 §4.5）
    uint32_t compilerVersion;      // 4  构建工具版本 hash
    uint32_t entryCount;           // 4  原子 Shader 变体数量
    uint32_t flags;                // 4  bit0: compressed(LZ4，独立压缩每个 blob 而非整体压缩，见 §9.6)
    uint32_t indexOffset;          // 4
    uint32_t reflectionOffset;     // 4
    uint32_t dataOffset;           // 4
    uint32_t sourceHash[2];        // 8  源文件集合的 64-bit hash
    uint32_t endianMarker;         // 4  字节序标识
    uint32_t headerSize;           // 4  本结构体大小，便于前向兼容
};
// sizeof(FileHeader) = 56 bytes（含 static_assert 校验）
```

### 9.3 索引表结构

```c
struct IndexEntry {
    uint64_t pipelineKeyHash_hi;   // PipelineKey 128-bit hash 高 64 位
    uint64_t pipelineKeyHash_lo;   // PipelineKey 128-bit hash 低 64 位
    uint32_t vertexBlobOffset;
    uint32_t vertexBlobSize;
    uint32_t fragmentBlobOffset;
    uint32_t fragmentBlobSize;
    uint32_t reflectionOffset;     // 指向 Reflection Data Pool 中的 ReflectionEntry（见 §9.4）
};
// sizeof(IndexEntry) = 36 bytes
```

**决策：索引表按 `(pipelineKeyHash_hi, pipelineKeyHash_lo)` 字典序升序排列**——先比较 hi，相等则比较 lo，确保 hi 碰撞时仍可正确定位。构建期校验全局 128-bit hash 无重复。查找复杂度为 **O(log N) 二分查找**（~359 个 entry 二分查找最多约 9 次比较，延迟可忽略）。

### 9.4 Reflection Data 内容

Uniform/Sampler/Attribute 均为变长列表，不能直接塞进定长 struct，需采用"定长头 + 变长子池偏移"的序列化设计：

```c
// IndexEntry.reflectionOffset 指向的定长头
struct ReflectionEntry {
    uint32_t vertexUniformCount;
    uint32_t vertexUniformOffset;      // 指向 UniformDesc 子池
    uint32_t fragmentUniformCount;
    uint32_t fragmentUniformOffset;    // 指向 UniformDesc 子池
    uint32_t samplerCount;
    uint32_t samplerOffset;            // 指向 SamplerDesc 子池
    uint32_t attributeCount;
    uint32_t attributeOffset;          // 指向 AttributeDesc 子池
};
// sizeof(ReflectionEntry) = 32 bytes

struct UniformDesc {
    char     name[32];   // 与 GLSL 源码中的 uniform 名一致（见 §6.4 命名规范）
    uint32_t offset;      // 在 UniformData buffer 中的字节偏移
    uint32_t size;        // 字节大小
    uint8_t  stage;       // 0=Vertex, 1=Fragment
};
// sizeof(UniformDesc) = 41 bytes（需显式 padding 到 4 字节对齐）

struct SamplerDesc {
    char    name[32];
    uint8_t binding;       // 后端 binding index
    uint8_t textureType;   // 0=sampler2D, 1=samplerExternalOES, 2=sampler2DRect（见 §12.2）
};

struct AttributeDesc {
    char    name[32];
    uint8_t location;
    uint8_t format;        // VertexFormat 编码
};
```

`UniformDesc`/`SamplerDesc`/`AttributeDesc` 三类子池各自连续存放在 Reflection Data Pool 中，`ReflectionEntry` 中的 `xxxOffset` 是相对 Reflection Data Pool 起始位置的字节偏移，`xxxCount` 决定读取条目数。由 §8.4 的 SPIR-V reflection 自动生成，运行时创建 Program 时用于建立 name → binding 的映射（详见 §12.4）。

### 9.5 版本兼容性策略

- `version` / `backend` / `glProfile` 不匹配 → 忽略 Bundle，走过渡期 fallback（若仍存在）或跳过绘制
- `sourceHash` 变化 → 源码已修改但产物未重新构建，仅 DEBUG 模式下 assert 提示

### 9.6 压缩策略

**决策：每个 Shader blob 独立 LZ4 压缩，而非对整个 Data Pool 统一压缩**，以兼顾未压缩模式下的 zero-copy mmap 能力。

- 未压缩模式（`flags.bit0=0`）：可对整个文件 zero-copy mmap，直接按 offset 访问
- 压缩模式（`flags.bit0=1`）：每个 blob 独立压缩，mmap 后按 entry 单独解压到独立 buffer，不影响索引表的 mmap 直接访问

---

## 10. 运行时集成设计

### 10.1 ShaderModuleDescriptor 接口扩展

**决策：扩展 `ShaderModuleDescriptor` 支持二进制 blob，不再局限于 `std::string code`**，否则 Bundle 无法传递 SPIR-V binary/MSL text：

```cpp
enum class ShaderModuleFormat { GLSL, SPIRV, MSL, WGSL };

class ShaderModuleDescriptor {
 public:
  ShaderModuleFormat format = ShaderModuleFormat::GLSL;
  std::shared_ptr<Data> data;    // 替代原 std::string code，支持二进制 blob
  ShaderStage stage = ShaderStage::Vertex;
};
```

各后端新增直通路径，跳过 shaderc/spirv-cross/tint：

- Vulkan：`SPIRV` → 直接 `vkCreateShaderModule`
- Metal：`MSL` → 直接 `newLibraryWithSource:`
- WebGPU：`WGSL` → 直接 `createShaderModule`（需在 `WebGPUShaderModule` 中新增 format 判断分支，否则会被误当作 GLSL 走 legacy 路径）
- OpenGL：`GLSL` → 仍走 `glCompileShader`（驱动编译无法跳过）

### 10.2 EffectDecomposer 分解决策逻辑

```cpp
class EffectDecomposer {
 public:
  struct DecomposeResult {
    enum class Strategy { SinglePass, MultiPass };
    Strategy strategy;
    PipelineKey pipelineKey;                // SinglePass 时使用
    std::vector<PassDescriptor> passes;     // MultiPass 时使用
  };

  static DecomposeResult decompose(const Brush& brush, const AppliedClip& clip,
                                   BlendMode blendMode, const FPArgs& args);
};
```

判定流程：

```
Step 1: 确定 colorFP / coverageFP 类型
Step 2: 检查特殊结构
  → brush.shader 是 BlendShader？          → 走 §10.4 BlendShader 分解路径
  → colorFilter->affectsTransparentBlack() 且 brush.shader 存在？
                                            → 走 §10.4 alpha mask 分解路径
Step 3: 匹配 Level 1 原子 Shader
  → 条件：colorFilter == nullptr && maskFilter == nullptr
           && blendMode 可用硬件 blend 表达（A 类）
           && colorFP/coverageFP 类型已知
  → 命中：返回 SinglePass(pipelineKey)
Step 4: 不满足快速路径 → 分解为多 Pass（Pass A 基础渲染 → Pass B colorFilter[可选]
        → Pass C maskFilter[可选] → Pass D 最终合成）
```

### 10.3 DrawOp::execute() 集成点

```cpp
void DrawOp::execute(RenderPass* renderPass) {
  auto decomposeResult = EffectDecomposer::decompose(brush_, clip_, blendMode_, fpArgs_);
  if (decomposeResult.strategy == Strategy::SinglePass) {
    auto shaderBlobs = context->precompiledShaderCache()->find(decomposeResult.pipelineKey);
    if (!shaderBlobs) {
      // 过渡期：走 ProgramBuilder::CreateProgram() 对照验证；交付后此分支不应触达
      return executeFallback(renderPass);
    }
    auto program = createProgramFromBlobs(context, *shaderBlobs);
    setUniformsAndSamplers(program);
    onDraw(renderPass, program);
    return;
  }
  executeMultiPass(renderPass, decomposeResult.passes);
}
```

### 10.4 特殊 FP 结构的多 Pass 分解

**`affectsTransparentBlack` ColorFilter 融合**（原单 Pass 中通过 `XfermodeFragmentProcessor::MakeFromTwoProcessors(..., SrcIn)` 实现，多 Pass 下分解为）：

```
Pass A: 用 shader 渲染到 T1（Src 模式，含 coverage）→ T1.alpha 保留透明区域信息
Pass B: 对 T1 做 colorFilter → T2（Src 模式）
Pass C: alpha_mask_blend → output = T2 × T1.alpha → RT（用户 blendMode）
```

**`Shader::MakeBlend()` 产生的 BlendShader**（原单 Pass 中通过 `XfermodeFragmentProcessor` TwoChild 模式实现，多 Pass 下分解为）：

```
Pass A: 用 dst shader 渲染到 T1（alpha=1 顶点颜色，Src 模式）
Pass B: 用 src shader 渲染到 T2（alpha=1 顶点颜色，Src 模式）
Pass C: shader_blend(mode) → 采样 T1/T2 做 blend，结果乘以 uAlpha(=brush.color.alpha) → RT
```

**正确性依据**：`GLSLXfermodeFragmentProcessor::emitCode()` 对 TwoChild 模式的原始处理是将 `inputColor.alpha` 强制设为 1 后分别调用两个子 FP，blend 后再乘回原始 alpha；Pass A/B 使用 alpha=1 顶点颜色、Pass C 补乘 `uAlpha` 精确复现此语义（详见 `multi-pass-blend-proposal-review.md` §D.5.2，此设计已通过审计确认，见 `multi-pass-blend-risks.md` "风险一审计结论"）。

### 10.5 中间 Texture 管理

复用现有 `ProxyProvider` + `ResourceCache` + `ScratchKey` 机制，`BackingFit::Approx` 向上取整提高复用率。中间 Texture 仅覆盖当前 draw 的设备坐标边界（非全屏），Scratch 资源默认 2 帧后过期回收。

| Pass 阶段 | 输出格式 | MSAA | Dither |
|-----------|---------|:----:|:------:|
| Pass A（基础渲染） | RGBA8 premultiplied | 继承最终 RT sampleCount | 否 |
| Pass B/C（效果处理） | RGBA8 premultiplied | 1x | 否 |
| Pass D（最终合成） | RT format | 继承最终 RT sampleCount | 是 |

### 10.6 未命中路径：Fallback 策略

多 Pass 架构下，原子 Shader 集合为 tgfx 内部封闭继承体系决定的有界集合（~359 个），Bundle miss 仅可能来自：
1. Bundle 文件版本与二进制不匹配（升级未同步更新 Bundle）
2. Bundle 文件损坏或未加载

**策略**：

- **开发/过渡期**：miss 时走 `ProgramBuilder::CreateProgram()` 动态路径并 LOGW 告警，用于交付前的双路径截图对比验证（见 §13.2）
- **交付后最终态**：由于原子 Shader 空间封闭，Bundle 必须覆盖全集。miss 触发 assert（DEBUG）或 LOGE + 跳过绘制（RELEASE），表示集成错误而非正常路径

---

## 11. 转写与验证方法论

### 11.1 从现状到原子 Shader 的标准流程

```
Step 1: 选定目标原子 Shader（如 texture_fill）
Step 2: 通过临时插桩 dump 现有 ProgramBuilder 对应 Processor 组合产出的 GLSL，作为转写参考
Step 3: 编写独立 .frag 文件（含 //! 元数据），用 #if 包裹差异代码块
Step 4: 运行构建工具，验证展开后的 GLSL 语法正确
Step 5: 运行 Golden Test（预编译路径 vs ProgramBuilder 动态路径），验证像素级等价
```

### 11.2 Dump 机制（仅用于迁移期参考，非最终架构组成部分）

在 `GLSLProgramBuilder::finalize()` 中插入 dump 逻辑，通过环境变量 `TGFX_SHADER_DUMP_DIR` 控制，输出完整 GLSL 供转写时对比参考。**注意**：dump 出的是当前单 Pass 拼接产物（GP+FP 树+XP 合并后的完整 GLSL），用于确认转写后的独立原子 Shader 与其对应的效果片段逐行一致，dump 本身不代表最终原子 Shader 的文件粒度。

### 11.3 逐 Shader 验证标准

- glslangValidator 离线编译无错误
- Golden Test 像素级对比（bit-exact 要求见 §13.2）

---

## 12. 关键实现问题与解决方案

### 12.1 ClassID 跨进程稳定性

**问题**：当前 `DEFINE_PROCESSOR_CLASS_ID` 宏使用 `UniqueID::Next()` 配合函数内 static 局部变量惰性分配：

```cpp
#define DEFINE_PROCESSOR_CLASS_ID               \
  static uint32_t ClassID() {                   \
    static uint32_t ClassID = UniqueID::Next(); \
    return ClassID;                             \
  }
```

`UniqueID::Next()` 是全局递增计数器（非 Processor 专用命名空间），其值取决于首次调用顺序。构建工具（独立可执行文件）与用户 App 是两个不同的二进制进程，**不能保证**调用顺序一致，即便同一 App 二进制在不同初始化路径（如 iOS 冷启动 vs 后台恢复）下也可能不同。因此"构建工具与运行时链接同一份代码即可保证 ClassID 一致"的假设不成立。

**决策：改为基于类名字符串的 `constexpr` hash**：

```cpp
#define DEFINE_PROCESSOR_CLASS_ID_WITH_NAME(Name)     \
  static constexpr uint32_t ClassID() {               \
    return StableHash32(#Name);                       \
  }
```

Bundle manifest 保存 `processorName → classID` 映射，运行时 DEBUG 模式下校验一致性。这是使 Bundle key 跨二进制稳定的唯一可靠方案。

### 12.2 纹理格式对 Shader 代码的实际影响

**问题**：`texture->format()` / `texture->type()` **直接影响生成的 GLSL 文本**，纹理格式差异不能仅视为 Pipeline State 层面的区别：

1. `UniformHandler::addSampler()` 根据 `TextureType` 生成不同 sampler 声明（`samplerExternalOES` + extension / `sampler2DRect` + extension / 默认 `sampler2D`）
2. `ShaderBuilder::appendTextureLookup()` 的 swizzle 取自 `Swizzle::ForRead(texture->format())`（Alpha-only 格式用 `.aaaa`）
3. YUV 纹理涉及完全不同的多 plane 采样与颜色转换代码

**处理方式**：原子 Shader 的 `ShaderShapeKey`（§4.3）**保留** `TextureType`、`isAlphaOnly`、`YUVFormat` 等 shader 可见属性作为独立的 Permutation 维度（§5.1 中 `texture_fill` 的 FORMAT/YUV 维度即为此设计），仅从 Key 中剥离真正不影响代码的纹理实例信息（如 GPU handle、具体尺寸）。

### 12.3 computeProcessorKey 的 Context 参数依赖

顶层 `computeProcessorKey(Context*, BytesKey*)` 签名中的 `Context*` 参数在当前实现中实际未被使用（子类 `onComputeProcessorKey(BytesKey*)` 不含此参数），可安全传入 `nullptr`。

### 12.4 Uniform/Sampler Binding 布局来源

**问题**：当前 `UniformData` 和 binding layout 完全来自 `emitCode()` 过程中 `UniformHandler` 的动态收集，Processor 的少量声明宏远不足以表达 vertex/fragment uniform 列表、sampler binding、attribute layout 等全部信息，无法仅从声明层面静态推导。

**决策**：如 §9.4 所述，Bundle 的 Reflection Data Pool 存储由 SPIR-V reflection（构建期，见 §8.4）自动生成的完整绑定布局，运行时创建 Program 时直接消费该 Reflection 数据，不依赖手写的 Processor 参数声明。

### 12.5 ShaderCaps 差异与后端特化

不同后端/profile 的 `ShaderCaps` 不同，同一逻辑 Shader 在不同后端/profile 下生成不同 GLSL。构建工具为每个后端及 OpenGL 各 profile（见 §4.5）独立生成一份 Bundle，运行时按设备探测结果选择对应 Bundle。

### 12.6 RuntimeEffect 路径隔离

`RuntimeEffect`（`include/tgfx/gpu/RuntimeEffect.h`）已使用独立的 `CommandEncoder → GPU::createShaderModule()` 路径，不经过 `ProgramBuilder` 和 FP 管线，因此本方案交付后移除 `ProgramBuilder` **不影响** `RuntimeEffect`。但 `RuntimeEffect` 仍需要 shaderc（及 Vulkan/Metal/WebGPU 后端可能需要的 spirv-cross/tint）作为运行时依赖保留，两者服务对象不同：

- **无 RuntimeEffect 的瘦身构建**（`-DTGFX_NO_RUNTIME_SHADER_COMPILER=ON`）：可移除 shaderc
- **完整构建**：保留 shaderc 供 `RuntimeEffect` 使用（详见 §17.4 分构建目标）

`Image::makeWithFilter()` 内联路径目前依赖 `ProgramBuilder` 的动态拼接能力，方案交付时改造为走离屏渲染路径（使用已有的多 Pass 中间 Texture 机制），使用频率低，改造成本可控。

---

## 13. 验证体系

### 13.1 单元验证：静态 Shader 的离线编译检查

构建工具的每个变体展开后立即调用 `glslangValidator` 检查语法正确性，编译失败阻断构建。

### 13.2 集成验证：截图测试对比

**决策：不承诺 bit-exact，按后端区分验证严格度**——即使 GLSL 文本相同，shaderc/spirv-cross 版本差异、浮点优化级别差异也可能引入细微不一致：

- **OpenGL**（同源 GLSL 文本）：追求 bit-exact
- **Vulkan/Metal/WebGPU**：使用现有截图容差框架（`Baseline::Compare`），对 shader 文本等价性使用 canonicalized 文本 diff，对 SPIR-V 使用 `spirv-dis` 后 normalized diff

CI 中跑两轮 `TGFXFullTest`：预编译路径（启用 `PrecompiledShaderCache`）vs 动态路径（`ProgramBuilder`，过渡期保留）对比。

### 13.3 覆盖率验证：Bundle miss 计数归零

CI 中统计 miss 事件，全量测试完成后要求 `missCount == 0`（交付后此项应恒为真，见 §16.4）。

### 13.4 Alpha 语义专项验证

针对 §10.4 的两种特殊分解（`affectsTransparentBlack` 融合、`BlendShader`）单独编写测试用例，逐一验证 alpha 语义正确性（详见 `multi-pass-blend-proposal-review.md` §A.1 的定理 1–3 形式化证明）。

### 13.5 体积验证

| 指标 | 上限 |
|------|------|
| 单后端 Bundle 压缩体积 | ≤ 1 MB |
| 移除 shaderc/spirv-cross/tint 后的二进制缩减（无 RuntimeEffect 构建） | ≥ 9 MB |

---

## 14. 成本收益分析

### 14.1 性能收益

| 指标 | 当前方案 | 本方案 |
|------|---------|--------|
| 首帧拼接+转换总耗时 | 15–70 ms | **0 ms** |
| 稳态帧 94.3% draw | 1 draw call | 1 draw call（**相同**） |
| 稳态帧 5.4% draw | 1 draw call | 2–3 draw calls + 1–3 MB 带宽/draw |
| 高级 BlendMode 首帧 | 300–750 ms（150 变体拼接+转换） | **0 ms**（15 个 Level 3 shader 全部预编译） |

### 14.2 带宽代价

以 400×400 区域的 3-Pass 分解为例，额外带宽约 3.1 MB，在 Tile-Based GPU 上通过中间 Texture 尺寸最小化 + `loadAction=DontCare` + 连续 ops 合并可进一步降低。此代价仅发生在 5.4% 的场景中，换取的是首帧 15–70 ms 卡顿的完全消除。

### 14.3 运行时二进制体积变化

| 变化项（无 RuntimeEffect 构建） | 体积 |
|--------|-----:|
| 移除 shaderc (含 glslang + SPIRV-Tools) | −7~13 MB |
| 移除 SPIRV-Cross | −2~3 MB |
| 移除 tint | −4~6 MB |
| 新增 Bundle 资源（~359 shader，多后端） | +1~2 MB |
| **净变化** | **−12~20 MB** |

保留 RuntimeEffect 的构建：shaderc 等运行时依赖仍需保留（见 §12.6），仅新增 Bundle 资源体积，运行时二进制不缩减。

### 14.4 代码量变化

| 类别 | 变化 |
|------|------|
| 移除：`ProgramBuilder` + 全部 FP 的 `emitCode()`（~3000–4000 行，交付时移除） | 删除 |
| 新增：~359 个静态 `.vert`/`.frag` 文件 | 新建 |
| 新增：构建工具（Python） | 新建 |
| 新增：`EffectDecomposer` + `PrecompiledShaderCache` + 中间 Texture 管理 | 新建 |

---

## 15. 风险评估与缓解措施

### 15.1 三大风险的审计结论

`multi-pass-blend-risks.md` 对方案提出三项质疑，经代码级审计（覆盖 `BlendFormula.cpp/h`、`TiledTextureEffect.h/cpp`、`BlendShader.cpp`、`ColorFilterShader.cpp` 等核心文件）得出以下结论，均已纳入本方案设计：

| 风险 | 审计结论 | 本方案的应对 |
|------|---------|-------------|
| `BlendShader`/`ColorFilterShader` 单 Pass 内联优势丧失 | **有效但影响被高估**——方案已有完整分解设计（§10.4），alpha=1 语义修复已设计，Flutter Impeller 已验证可行 | §10.4 明确设计了两种结构的多 Pass 分解 |
| Coverage 信息离屏后不可恢复 | **不成立**——`BlendModeNeedDstTexture()` 的判定逻辑本身就区分了"能否以 `color×coverage` 乘积表达"（A 类）与"需要 shader 读 dst texture"（B 类），A 类多 Pass 下天然保留，B 类当前就在 shader 中处理 coverage | §4.2 理由 1、§15.2 详述 |
| `TiledTextureEffect` 采样变种被低估 | 需在 Level 1 设计中如实反映真实变体数（9 种 ShaderMode × 双向 = 81 种基础组合，而非简化为 4 种 TileMode） | §5.1 已按 ~80 变体纳入 Level 1 |

### 15.2 Coverage/BlendMode 耦合的完整审计（风险二的详细反驳）

`BlendModeNeedDstTexture()` 的判定逻辑（`src/gpu/BlendFormula.cpp:150-160`）：当 `hasCoverage=true` 时，只有 `primaryOutputType() == Modulate` 且不需要 secondary output 的模式才走硬件 blend（A 类，如 SrcOver）；其余模式（B 类，如 Clear/Src/SrcIn/DstIn）本来就需要 `NeedDstTexture=true`，当前架构已经通过 shader 读 dst texture 实现完整的 coverage-aware 合成公式。

多 Pass 方案中：A 类模式下 Pass A 输出 `color × coverage` 到中间 Texture（等价于原 XP 的 Modulate 输出），最终 Pass 以相同硬件 blend 写入 RT，数学完全等价；B 类模式归入 Level 3 `advanced_blend` shader，在 shader 中同时采样 src 和 dst 实现 W3C 合成公式，处理方式与当前架构本质相同。**不存在"Coverage 信息离屏后丢失"的问题。**

### 15.3 已知的像素级差异（已确认接受）

| 场景 | 差异 | 处理方式 |
|------|------|---------|
| `BlendShader` + `brush.color.alpha < 1` | 若不做 alpha=1 修复会导致双重调制 | **已修复**：见 §10.4 的 alpha=1 顶点颜色 + 补乘 uAlpha 方案，修复后 100% 等价 |
| `affectsTransparentBlack` ColorFilter + AA coverage < 1 | AA 边缘 1–2 像素，色值差 ≤10/255 | **接受差异**：肉眼不可感知，修复需额外 Texture，性价比极低 |

覆盖 >99.5% 实际 draw 的场景（shader + coverage、shader + 普通 ColorFilter、shader + ColorMatrix、shader + MaskFilter、shader + 高级 BlendMode）均为 100% 像素级等价（详见 `multi-pass-blend-proposal-review.md` §A.1.7）。

### 15.4 其他风险矩阵

| 风险 | 概率 | 缓解策略 |
|------|:----:|---------|
| 多 Pass 带宽致 TBR GPU 性能回退 | 中 | 尺寸最小化 + `loadAction=DontCare` + 连续 ops 合并 |
| ~359 个 shader 维护成本 | 中 | 模块化共享 `common/*.glsl` include + Golden test 自动化 |
| 不同后端精度差异致截图不一致 | 高 | `highp float` + 合理容差（见 §13.2） |
| 5.4% 拆分比例的生产代表性不足 | 低 | 边开发边在生产 App 插桩采样，不阻塞开发（详见决策点 #7） |
| `TiledTextureEffect` 变种数超预期 | 已识别 | Level 1 变体数已按 9×9 ShaderMode 组合的真实空间纳入估算（§5.1） |

### 15.5 不阻塞启动的开放问题

以下问题在方案启动时不需要立即解决，但必须在对应实施阶段明确处理，不能遗漏：

| 问题 | 说明 | 处理时机 |
|------|------|---------|
| `GaussianBlur1DFP` 作为 coverageFP 的处理时序 | 单 Pass 中 `GaussianBlur1DFP` 可直接作为 coverageFP 参与 blend 系数计算；多 Pass 下 clip mask / shape mask 需先渲染到中间 Texture，再由 blur shader 采样该 Texture，时序与单 Pass 不同，需要专门设计（不能直接复用 Level 1 的 coverageFP 组合方式） | §16.2 Shader 编写阶段，编写 `gaussian_blur` Level 2 Shader 时专项设计 |
| Stencil-based 光栅化路径 | tgfx 当前生产代码路径走 `PathTriangulator` 三角化，不使用 stencil-based bezier 光栅化，本方案暂不覆盖该路径的原子 Shader 化 | 明确 scope out，若后续启用该路径需重新评估变体空间 |

### 15.6 数据口径说明

§3.3 表格中 3.7% + 1.6% = 5.3%，与小计"5.4%"存在 0.1 个百分点的舍入误差（源自 `multi-pass-blend-proposal-review.md` §3.2 原始采样数据的小数位四舍五入），不代表统计错误，实施时应以原始 802 次采样的未舍入数据为准。

---

## 16. 实施路线

### 16.1 交付策略

**决策：单版本交付，不分阶段留存旧路径**（详见 `multi-pass-blend-proposal-review.md` §6.1）。

理由：外部 App 已在使用 tgfx，若分阶段交付且中途暂停，`ProgramBuilder` 与新的原子 Shader 管线需要长期并存，双份代码同时维护的成本高于一次性交付。交付时运行时拼接+转换完全消除，`ProgramBuilder` 移除（`RuntimeEffect` 走独立路径不受影响）。

### 16.2 任务分解

| 模块 | 工作内容 |
|------|---------|
| 阻碍消除 | `ShapeDrawOp`/`ShapeInstancedDrawOp` 的 FP 组装策略前置到构造函数（消除 `onMakeGeometryProcessor()` 在 execute 时追加 FP 的问题） |
| 基础框架 | `PipelineKey` 体系 + `ShaderModuleDescriptor` blob 扩展（§10.1）+ `PrecompiledShaderCache` + Bundle 打包与运行时查找 |
| Shader 编写 | 全部 ~359 个原子 Shader 源文件（Level 1 全量 GP 覆盖 + Level 2 含特殊分解用 shader + Level 3） |
| 构建工具链 | Python 脚本 + `glslangValidator` + `spirv-cross` + `tint` + SPIR-V reflection 生成器 |
| 分解引擎 | `EffectDecomposer` 快速路径匹配 + 多 Pass 分解逻辑（§10.2-§10.4） |
| OpsCompositor 改造 | 临时 Texture 分配 + 中间 Pass 创建 + 高级 BlendMode 路径替换 |
| Uniform 改造 | 基于 SPIR-V reflection 的 binding 布局消费逻辑 |
| 测试与验证 | 全场景截图对比（Golden test）+ 全后端验证 + Alpha 语义专项验证 + 性能基准 |
| 收尾 | `Image::makeWithFilter` 内联路径改造为离屏渲染 + Bundle 体积优化 + CI 集成 + `ProgramBuilder` 移除 |

### 16.3 里程碑参考时间线

| 阶段 | 内容 | 关键产出 |
|------|------|---------|
| 1 | 阻碍消除 + 基础框架 | `PipelineKey`、`ShaderModuleDescriptor` 扩展、`PrecompiledShaderCache` 骨架 |
| 2 | Level 1 Shader 编写 + 构建工具链 MVP | ~295 个 Level 1 shader + Bundle 生成流水线 |
| 3 | `EffectDecomposer` + 中间 Texture 管理 | 快速路径 + 多 Pass 分解均可运行，miss=0 |
| 4 | Level 2/3 Shader 编写 + 特殊结构分解 | `BlendShader`/`affectsTransparentBlack` 分解落地 |
| 5 | 全量验证 + 清理旧系统 | Golden test 全通过，`ProgramBuilder` 移除 |

具体天数需在任务分解（§16.2）确定后由实施工程师评估。

### 16.4 资源需求

- 设备：macOS（Metal + OpenGL）+ Linux（Vulkan + SwiftShader）
- CI 资源：无新增（复用现有截图测试流水线）

---

## 17. 持续维护规范

### 17.1 新增效果的开发流程

```
1. 编写原子 Shader 源文件（.frag，含 //! 元数据声明变体维度和排除规则）
2. 运行 build_shaders.py → 自动生成 C++ 头文件 + 编译产物 + 更新 Bundle
3. 在 EffectDecomposer 中添加匹配规则
4. 运行 Golden Test → 验证截图 + miss=0
5. 提交代码 + 更新的 Bundle
```

**核心约束**：新增效果必须作为独立原子 Shader 加入封闭集合，**禁止**引入运行时拼接逻辑（即不再需要编写 `emitCode()` 方法，也不需要在其他文件中额外注册）。

### 17.2 CI 流水线集成方案

```yaml
steps:
  - name: Build Shader Bundle
    run: python build_shaders.py --input src/gpu/shaders/ --output bundles/ --backend vulkan,metal,webgpu,opengl
  - name: Build TGFX
    run: cmake --build ... --target TGFXFullTest_OpenGL
  - name: Run Golden Test (Precompiled vs Fallback)
    run: ./TGFXFullTest_OpenGL --shader-bundle bundles/vulkan.tgfxshader
  - name: Verify Bundle Coverage
    run: test $(cat bundle_miss_count.txt) -eq 0
```

### 17.3 分构建目标

- **完整构建**（保留 `RuntimeEffect` 能力）：保留 shaderc/spirv-cross/tint 供 `RuntimeEffect` 使用
- **瘦身构建**（`-DTGFX_NO_RUNTIME_SHADER_COMPILER=ON`）：完全移除运行时编译器依赖，适用于不使用 `RuntimeEffect` 的下游 App

### 17.4 Bundle 版本管理

Bundle 文件（连同 SPIR-V/MSL/WGSL 源产物）提交到 Git 仓库，约 1–2 MB（多后端 + 多 OpenGL profile 合计）。

版本同步保证：
- 修改 `.frag`/`.vert` 文件后必须同步更新 Bundle（CI 校验：rebuild → 比对是否有 diff）
- 新增/删除原子 Shader 后必须更新 Bundle（Reflection 数据同步变化）

### 17.5 覆盖率监控告警

多 Pass 架构下原子 Shader 集合封闭，覆盖率应恒为 100%；出现 miss 表示 Bundle 与二进制不匹配（版本错配或构建遗漏），应触发 CI 红灯而非静默降级。

---

## 附录

### 附录 A：Shader 文件目录结构

见 §6.1。

### 附录 B：PipelineKey 完整定义

```cpp
struct PipelineKey {
    uint16_t shaderID;        // ~359 个原子 Shader 的唯一编号
    uint8_t  variantIndex;    // 该 Shader 内部的变体索引（含 outputSwizzle，见下方说明）
    uint8_t  rtFormat;        // render target format
    uint8_t  sampleCount;     // MSAA sample count
    uint8_t  blendState;      // hardware blend 配置索引
    uint8_t  cullMode;
    BytesKey toKey() const;
};
```

> **说明**：`outputSwizzle` 不作为 `PipelineKey` 的独立字段。原因：`outputSwizzle` 决定的是 fragment shader 最终写出颜色前的 swizzle 表达式（如 `fragColor = color.xxxx`），这段代码在构建期就固化进 GLSL 文本，属于 §4.3 `ShaderShapeKey` 的组成部分，已经体现在 `shaderID + variantIndex` 里。若在 `PipelineKey` 中再放一个独立的 `outputSwizzle` 字段，等于同一份信息被两层 Key 重复编码，且容易在运行时被错误地当作"可与任意 shaderID 自由组合"的正交维度（实际不能，因为 swizzle 已经写死在该 shaderID 对应的 GLSL 里）。因此 `outputSwizzle` 由 `variantIndex` 隐含决定，不单独出现在 `PipelineKey` 结构体中。

考虑 rtFormat/sampleCount 维度后，单平台变体数会在 ~359 基础上因渲染状态叠加而膨胀（主要影响 Vulkan/Metal），但压缩后体积仍可控（见 §5.3）。

### 附录 C：与 Flutter Impeller 的架构对比

| 维度 | Flutter Impeller | tgfx 本方案 |
|------|:----------------:|:------------:|
| Shader 来源 | 构建期编译 | 构建期编译 |
| 运行时拼接 | 无 | 无（过渡期保留 fallback） |
| 效果组合方式 | EntityPass 多 Pass | EffectDecomposer 多 Pass |
| 快速路径 | 无（统一多 Pass） | **有（94.3% 单 Pass）** |
| 架构改造范围 | 从零设计 | 渐进式迁移 |

### 附录 D：文献引用索引

| 已有文档 | 定位 |
|----------|------|
| multi-pass-blend-proposal-review.md | 多 Pass 架构完整方案、可行性论证、任务分解 |
| multi-pass-blend-risks.md | 三大风险的提出与代码级审计结论 |
| shader-compilation-pipeline-design.md | 三阶段模型、业界对比、性能量化背景 |
| shader-variant-reduction.md / shader-variant-minimization.md | 单 Pass 路线下的变体缩减策略（历史参考，多 Pass 架构下变体空间已结构性封闭，不再依赖此类策略） |

---

*文档结束*
