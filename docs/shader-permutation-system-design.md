# TGFX Shader Permutation 系统设计

**版本**: 2.1
**日期**: 2026-07-02
**状态**: 设计规范（评审稿）

---

## 目录

1. [问题定义与设计目标](#1-问题定义与设计目标)
2. [总体架构](#2-总体架构)
3. [开发者工作流](#3-开发者工作流)
4. [Shader 源文件规范](#4-shader-源文件规范)
5. [Shader 清单与变体规模分析](#5-shader-清单与变体规模分析)
6. [Permutation 声明系统详细设计](#6-permutation-声明系统详细设计)
7. [构建工具链](#7-构建工具链)
8. [运行时集成](#8-运行时集成)
9. [错误检查体系](#9-错误检查体系)
10. [附录](#10-附录)

---

## 1. 问题定义与设计目标

### 1.1 问题陈述

TGFX 现有渲染管线在 `DrawOp::execute()` 关键路径上通过 `ProgramBuilder::CreateProgram()`（`src/gpu/ProgramBuilder.h`）动态拼接 GLSL：递归调用 GP/FP[]/XP 的 `emitCode()` 生成源码文本，经 shaderc 编译为 SPIR-V，再经 SPIRV-Cross/Tint 翻译为 MSL/WGSL。该管线的成本可归纳为两项：

| 维度 | 现象 | 根因 |
|------|------|------|
| 首帧延迟 | 应用首帧遇到 10–20 个新 `programKey` 时，累积拼接+转换耗时约 15–70 ms | 拼接与跨语言翻译发生在渲染提交的关键路径上，而非构建期 |
| 运行时二进制体积 | shaderc（含 glslang + SPIRV-Tools）、SPIRV-Cross、Tint 三套编译器以静态库形式常驻，Release arm64 下合计约 9–14 MB | 三套编译器仅为服务运行时拼接而存在；若拼接消除，多数场景不再需要它们 |

> 上述数据引自既有分析文档（`docs/research/shader-precompile-master-plan.md` §2.4–2.5），本文档不重复论证，仅作为设计动机引用。

### 1.2 设计目标

将 GLSL 拼接与跨语言翻译从渲染提交路径上消除，改为构建期一次性完成；运行时仅通过查表获取已编译好的 Shader 二进制/文本。

### 1.3 约束条件

| 编号 | 约束 | 说明 |
|:----:|------|------|
| C1 | 变种声明位置 | 使用 C++ 类型系统声明 Permutation 维度，不使用 shader 文件内的注释元数据 |
| C2 | 构建工具技术栈 | C++ 独立可执行文件，CMake 驱动；不引入 Python 依赖 |
| C3 | 架构基线 | 以单 Pass 为主（覆盖绝大多数场景），多 Pass 仅作为少数复杂 Processor 组合的兜底手段；具体比例见 §5.4 |
| C4 | 热重载 | 不支持。修改 shader 源码等价于修改一个 `.cpp`，走正常的 CMake 增量构建 |
| C5 | `RuntimeEffect` 隔离 | 用户自定义着色器（`include/tgfx/gpu/RuntimeEffect.h`）保持独立的运行时编译路径，不受本方案影响，理由见 §8.5 |
| C6 | 目标后端 | OpenGL / Vulkan / Metal / WebGPU，与 `include/tgfx/gpu/Backend.h` 中 `Backend` enum 一致 |

---

> TGFX 此前存在一份以"全部改为多 Pass 分解 + shader 文件 `//!` 注释声明"为核心的方案（`docs/research/shader-precompile-master-plan.md`）。本文档在关键决策上与之不同，差异与理由见 [附录 A](#附录-a与既往方案的决策差异)；正文不依赖该文档即可独立阅读。

## 2. 总体架构

### 2.1 构建期流程

```
src/gpu/shaders/**/*.h     ← 开发者编写：Permutation 维度声明 + 裁剪规则
src/gpu/shaders/**/*.frag  ← 开发者编写：纯 GLSL 渲染逻辑 + #if 宏消费
src/gpu/shaders/**/*.vert
        │
        ▼  （C++ 静态自注册，构建工具无需解析任何 C++ 语法）
shader_build_tool（独立可执行文件，由 CMake custom command 驱动）
  1. 遍历全局 Shader 注册表（ShaderRegistry::All()）
  2. 对每个已注册 Shader：枚举其 PermutationDomain 的笛卡尔积
  3. 调用 ShouldCompile() 裁剪无效组合
  4. 对每个有效组合：宏展开 → GLSL → SPIR-V → MSL/WGSL（OpenGL 保留 GLSL 文本）
  5. 打包为 Bundle + 生成构建报告（见 §9.2）
        │
        ▼
build/generated/shader_bundle.{backend}.bin
        │
        ▼  （CMake binary-to-cpp 转换，嵌入为 C++ 全局数组）
EmbeddedShaderBundle_{backend}.cpp → 编译链接进 libtgfx.a（随库分发，零外部文件依赖）
```

### 2.2 运行时流程

```
OpsCompositor::addDrawOp()
        │
        ▼
EffectDecomposer::Decompose(geometryProcessor, fragmentProcessors, xferProcessor, blendMode)
        ├─ 命中已注册 Shader 的某个 Permutation → 单 Pass
        └─ 未命中（复杂叠加）→ 拆分为多个 Pass，每个 Pass 各自走单 Pass 查表
        ▼
DrawOp::execute()
        → 计算 PipelineKey（§8.2）
        → context->precompiledShaderCache()->find(key) 取出已编译好的 Shader 二进制/文本
        → 复用现有 GlobalCache::findProgram()/addProgram() 做 Program 级 LRU 缓存（§8.1）
        → GPU::createShaderModule() + createRenderPipeline()
        （不再调用 ProgramBuilder::CreateProgram() / emitCode() 拼接字符串）
```

### 2.3 核心架构决策

**决策**：Shader 的"预编译单元"是"一个 C++ Shader 描述类声明的全部 Permutation"，而不是"某一次 draw 对应的完整 Processor 树"。

**论证**：现有 `programKey` 是 `GP.computeProcessorKey() + FP[].computeProcessorKey()（递归）+ XP.computeProcessorKey()` 的拼接，而 FP 树的嵌套深度和组合方式由用户通过 Canvas API（如 `Shader::MakeBlend()`）在运行时决定，是一个组合数量在编译期不可确定的空间，构建期无法完整枚举"每一种可能的 Processor 树"。将预编译粒度下沉到"单个 Shader 类的 Permutation"后，一次 draw 是否可预编译，取决于它能否被分解为若干个已注册 Shader 类的具体取值——这是一个封闭、可枚举的问题（每个 Shader 类的维度都是引擎内部枚举，详见 §5）。命中时走单 Pass；命中不了的复合效果（如 `colorFilter` + `maskFilter` + 高级 `BlendMode` 同时出现）才拆多 Pass，多 Pass 的每一段仍是查表得到的预编译 Shader，不产生新的拼接。

下节 §2.3.1 论证该分解的可行性与完备性保证；具体判定规则见 §5.4。

### 2.3.1 可行性论证：为什么任意 draw 都能被覆盖

**结论**：任意合法的 Canvas draw 调用产生的 Processor 树，都能被递归分解为有限数量的预编译 Shader 组合——浅结构走单 Pass，深结构走多 Pass，两者的匹配规则都是构建期可验证的查表，不产生运行时拼接。

三点核心事实支撑这个结论：

1. **除 `colors[0]`（`brush.shader` 产出的 FP）外，Processor 树其余位置的 FP 类型是编译期固定的有限集合**：`colors[1..]`（colorFilter/ColorSpaceXform）、`coverages[]`（maskFilter/clip）、GP、XP 各自的可能类型均由有限个内部类的工厂方法决定（详见 §5.2、§8.4）。
2. **`colors[0]` 是唯一可能产生无界深度的位置**：`Shader::MakeBlend()` 通过 `XfermodeFragmentProcessor` 递归嵌套子 Shader，理论上嵌套深度无界；其余 Shader 子类（`ImageShader`/`GradientShader`/`ColorShader` 等）产出的都是单层叶子 FP。
3. **递归嵌套通过多 Pass 兜住，不依赖穷举**：`EffectDecomposer` 对 `XfermodeFragmentProcessor`/`ComposeFragmentProcessor` 这类容器节点做递归拆解而非整树匹配——每拆开一层，子 Shader 先各自渲染到临时纹理（各自仍是单 Pass 查表），再用一个维度极小（仅 `BlendMode` × 是否有 Child）的合并 Shader 完成拼接。分解算法本身对任意深度的树都能终止（每次递归让子树变浅一层），因此覆盖率不依赖于能否穷举所有树形状。

完备性由三层机制共同保证，而非依赖对 FP 树形状的枚举：构建期检查所有已注册 Processor 子类均在匹配表中有对应规则（§9.2）；运行时 `Decompose()` 的递归终止条件天然满足（叶子类型有限，见事实 1）；过渡期未覆盖的组合保留 `ProgramBuilder::CreateProgram()` 兜底（§8.1），不中断渲染。详细证据与推导过程见 §5、§8.4、§9.2。

### 2.4 贯穿示例：TextureFillShader

为了让 §2.1/§2.2 流程图中的 `ShaderRegistry::All()`、`PermutationDomain`、`ShouldCompile()`、`EffectDecomposer`、`PrecompiledShaderCache` 等抽象名词有具体落点，本节用一个精简示例把"声明 → 构建 → 运行时使用"的完整链路走通一遍：§2.4.1–2.4.2 是开发者要编写的代码，§2.4.3 是构建工具对这些代码做的事，§2.4.4 是一次实际 draw 调用如何用到构建产物。后续 §3–§9 的对应章节是本节某个环节的规则细节展开，不引入新示例；理解本节即可获得系统的完整心智模型。

> **教学简化说明**：`TextureEffect` 对应的生产环境 `TextureFillShader` 实际有 8 个 Permutation 维度（完整声明与依据见 §6.4、§5.3.1）。本节为控制示例的认知负担，仅保留 4 个维度（`HAS_YUV`/`ALPHA_ONLY`/`HAS_RGBAAA`/`HAS_SUBSET`），省略的 `YUV_FORMAT_NOT_I420`/`YUV_RANGE_FULL`/`STRICT_CONSTRAINT`/`HAS_PERSPECTIVE` 在声明方式、裁剪方式、宏消费方式上与保留的维度同构，不引入新概念。本节的 `ShouldCompile`/裁剪后变体数等具体数值仅对这个 4-维度简化版有效，不能与 §5.3.1 的 8-bit 生产数据混用。

#### 2.4.1 声明侧：Permutation 维度声明

```cpp
// src/gpu/shaders/level1/TextureFillShader.h

namespace tgfx {

class TextureFillShader : public PrecompiledShader {
 public:
  TGFX_DEFINE_DIMS(HAS_YUV, ALPHA_ONLY, HAS_RGBAAA, HAS_SUBSET);
  using D = Dims;
  static_assert(D::COUNT == 4, "Update ShouldCompile below when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"TextureFillShader", "level1/texture_fill.vert", "level1/texture_fill.frag",
            D::domain(), ShouldCompile};
  }

 private:
  // YUV textures never carry an independent alpha plane or alpha-only format
  // (see TextureEffect::onComputeProcessorKey), so this combination is excluded.
  static bool ShouldCompile(const std::vector<int>& v) {
    bool hasYuv = v[D::HAS_YUV] != 0;
    bool alphaOnly = v[D::ALPHA_ONLY] != 0;
    bool hasRgbaaa = v[D::HAS_RGBAAA] != 0;
    return !(hasYuv && (alphaOnly || hasRgbaaa));
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TextureFillShader)
```

#### 2.4.2 Shader 侧：渲染逻辑

文件组织与命名约定见 §4；这里只关注 `#if` 如何消费 §2.4.1 声明的 4 个宏：

```glsl
// src/gpu/shaders/level1/texture_fill.frag
#version 450

layout(location = 0) in vec2 v_texCoord;
layout(location = 0) out vec4 fragColor;

#if HAS_YUV
layout(set = 0, binding = 0) uniform sampler2D u_texY;
layout(set = 0, binding = 1) uniform sampler2D u_texU;
layout(set = 0, binding = 2) uniform sampler2D u_texV;
#else
layout(set = 0, binding = 0) uniform sampler2D u_sampler;
#if HAS_RGBAAA
layout(set = 0, binding = 1) uniform sampler2D u_alphaSampler;
#endif
#endif

#if HAS_SUBSET
layout(set = 0, binding = 3) uniform SubsetBlock { vec4 u_subset; };
#endif

void main() {
  vec2 coord = v_texCoord;

#if HAS_SUBSET
  coord = clamp(coord, u_subset.xy, u_subset.zw);
#endif

#if HAS_YUV
  float y = texture(u_texY, coord).r;
  float u = texture(u_texU, coord).r - 0.5;
  float v = texture(u_texV, coord).r - 0.5;
  vec4 color = vec4(y + 1.402 * v, y - 0.344 * u - 0.714 * v, y + 1.772 * u, 1.0);
#else
  vec4 color = texture(u_sampler, coord);
#if ALPHA_ONLY
  color = vec4(0.0, 0.0, 0.0, color.a);
#elif HAS_RGBAAA
  color.a = texture(u_alphaSampler, coord).r;
#endif
#endif

  fragColor = color;
}
```

#### 2.4.3 构建期：从声明到 Bundle

`shader_build_tool`（完整流程见 §7.1）处理 `TextureFillShader` 时：

1. 从全局注册表（§6.3 的 `ShaderRegistry::All()`）取得 §2.4.1 中通过 `TGFX_REGISTER_SHADER` 注册的工厂函数，构造出该描述类实例。
2. 枚举 `D::domain()` 的笛卡尔积：4 个 bool 维度 → 2⁴ = 16 个裸组合。
3. 对每个裸组合调用 `ShouldCompile`：`HAS_YUV = 1` 且（`ALPHA_ONLY = 1` 或 `HAS_RGBAAA = 1`）的组合被排除，共 6 个；剩余 **10** 个有效组合进入编译。
4. 对每个有效组合：把该组合对应的 4 个 `#define` 前置到 §2.4.2 的源码，走 §7.2 的后端转换链路，产出 Shader 二进制/文本 + Reflection 数据。
5. 10 个变体 × 已启用的 backend 数，写入对应 backend 的 `shader_bundle.{backend}.bin`（§7.4 格式）。

#### 2.4.4 运行时：一次 draw 如何用到这个 Shader

`TextureEffect`（`src/gpu/processors/TextureEffect.h`）作为 FragmentProcessor，实现 §8.4 定义的 `onComputePermutationValues()`，把自身当前状态映射为 §2.4.1 声明的 4 个维度取值：

```cpp
// src/gpu/processors/TextureEffect.cpp
std::vector<int> TextureEffect::onComputePermutationValues() const {
  using D = TextureFillShader::Dims;
  std::vector<int> values(D::COUNT, 0);
  values[D::HAS_YUV] = (yuvTexture != nullptr) ? 1 : 0;
  values[D::ALPHA_ONLY] = textureProxy->isAlphaOnly() ? 1 : 0;
  values[D::HAS_RGBAAA] = (alphaStart != Point::Zero()) ? 1 : 0;
  values[D::HAS_SUBSET] = needSubset() ? 1 : 0;
  return values;
}
```

一次 draw 从此处开始，直到提交 GPU 命令的关键路径（各环节的详细设计见括号中标注的章节）：

```cpp
// DrawOp::execute() 关键路径（示意，字段名与真实签名可能有出入，用于说明调用顺序）
void DrawOp::execute(OpsRenderPass* renderPass) {
  // 1. EffectDecomposer 把 Processor 树映射为 PipelineKey（§8.4）。
  //    本例的 GP + 单个 TextureEffect 命中单 Pass，result.passes.size() == 1。
  DecomposeResult result = EffectDecomposer::Decompose(gp, fps, xp, blendMode);
  const PipelineKey& key = result.passes[0];

  // 2. 先查现有 GlobalCache（Program 级 LRU，§8.1），未命中再查 PrecompiledShaderCache。
  BytesKey programKey = key.toKey();
  Program* program = context->globalCache()->findProgram(programKey);

  if (program == nullptr) {
    // 3. 查表取出构建期产物；每个 Context 持有自己的 PrecompiledShaderCache 实例（§8.2）。
    const PrecompiledShaderBlob* blob = context->precompiledShaderCache()->find(key);
    // blob == nullptr 说明出现了 §9.3 第 1 条描述的逻辑错误（Debug 下断言中止）。

    // 4. 用 blob 中的 Shader 二进制/文本与 Reflection 数据创建 Program，并写入 GlobalCache。
    program = CreateProgramFromBlob(context, blob);
    context->globalCache()->addProgram(programKey, program);
  }

  // 5. 按 Reflection 提供的 UniformDesc 布局填充 uniform（§8.3），机制不变。
  for (FragmentProcessor* fp : fps) {
    fp->setData(program->vertexUniformData(), program->fragmentUniformData());
  }

  // 6. 提交绘制，不再经过 ProgramBuilder::CreateProgram()/emitCode() 拼接字符串。
  renderPass->bindPipelineAndDraw(program, ...);
}
```

各维度含义与 `ShouldCompile` 排除依据见 §5.3.1；生产环境完整的 8-维度声明见 §6.4；运行时按位拼装 `permutationIndex` 的通用写法见 §6.5；`EffectDecomposer`/`PipelineKey`/`PrecompiledShaderCache` 的完整接口定义见 §8。

---

## 3. 开发者工作流

本节界定日常开发所需的最小认知集合：新增或修改一个可预编译 Shader 需要理解的操作步骤与信息范围。第 §6 节给出本节涉及的全部 C++ 类型的完整接口定义，供实现与评审阶段查阅，不构成理解本节的前提。

### 3.1 新增或修改一个 Shader 的两项改动

新增一个渲染效果需要改动两处：

1. **声明 Permutation 维度**（`.h` 文件）：为该 Shader 定义或修改一个 `PrecompiledShader` 子类，列出其 Permutation 维度与 `ShouldCompile` 裁剪规则，并在文件末尾调用 `TGFX_REGISTER_SHADER(...)` 完成自注册。
2. **编写渲染逻辑**（`.frag`/`.vert` 文件）：使用纯 GLSL 编写渲染逻辑，通过 `#if DIMNAME` 消费第 1 步中声明的宏，命名与格式约定见 §4。

以下内容不需要改动：`PipelineKey` 的拼接逻辑（由运行时统一处理，见 §8.2）、`EffectDecomposer` 的 Processor–Shader 匹配表（见 §8.4）。仅当新增效果确实引入新的多 Pass 拆分点时才需触及后者，此类改动属于架构级变更，超出本节讨论范围。

### 3.2 本地验证

1. **触发构建**：执行 `cmake --build cmake-build-debug --target tgfx_shader_bundles`。构建工具依据 §9.2 所述规则对声明与语法进行静态检查；检查失败时的报错信息包含具体的 Permutation 取值定位，而非仅给出裸 index。
2. **运行截图测试**：验证渲染正确性。测试流程与现有机制保持一致，不因引入本方案而改变。

### 3.3 端到端示例：回顾 TextureFillShader

§2.4 已给出 `TextureFillShader` 精简示例（对应 `TextureEffect`，`src/gpu/processors/TextureEffect.h/.cpp`）完整的"声明 → 构建 → 运行时使用"链路（§2.4.1–2.4.4），此处不重复贴出。本节把 §3.1 的两步抽象操作对应到具体代码位置：第一步（维度声明）对应 §2.4.1 的 `TGFX_DEFINE_DIMS(...)` 与 `ShouldCompile`；第二步（宏消费）对应 §2.4.2 的 `#if HAS_YUV ... #else ... #if ALPHA_ONLY ... #elif HAS_RGBAAA ... #endif ... #endif` 分支结构。

生产环境该 Shader 完整的 8-维度声明见 §6.4，对应的运行时位拼装写法见 §6.5。

---

## 4. Shader 源文件规范

本节界定 §3.1 第二步的详细规则：`.vert`/`.frag` 源文件应如何组织、如何消费维度宏。

### 4.1 文件组织

Shader 源文件（`.vert`/`.frag`）只包含渲染逻辑，不含变种元数据注释——维度已在 C++ 侧声明，shader 内用 `#if`/`#elif` 消费同名宏。§2.4/§6.4 的贯穿示例全部维度为 bool，本节另给出 `TiledTextureEffect` 对应的 `TiledTextureFillShader` 片段（§5.3.2 有其完整维度分析），用以展示含 `PermutationEnum` 维度时 `#if DIMNAME == N` 的语法，不是贯穿示例的一部分：

```glsl
// src/gpu/shaders/level1/tiled_texture_fill.frag
#version 450

layout(location = 0) in vec2 v_texCoord;
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D u_sampler;

#if HAS_SUBSET
layout(set = 0, binding = 1) uniform SubsetBlock {
  vec4 u_subset;
};
#endif

void main() {
  vec2 coord = v_texCoord;

#if SHADER_MODE_X == 1  // Clamp
  coord.x = clamp(coord.x, u_subset.x, u_subset.z);
#elif SHADER_MODE_X == 2  // RepeatNearestNone
  coord.x = mod(coord.x - u_subset.x, u_subset.z - u_subset.x) + u_subset.x;
#endif
  // ... remaining SHADER_MODE_X / SHADER_MODE_Y branches, mirroring
  // TiledTextureEffect::GetShaderMode() in TiledTextureEffect.cpp:26-61.

  vec4 color = texture(u_sampler, coord);

#if ALPHA_ONLY
  color = vec4(0.0, 0.0, 0.0, color.a);
#endif

  fragColor = color;
}
```

命名约定：

- 枚举维度用 `#if DIMNAME == N`，`N` 为 §6.1 `PermutationEnum::valueNames` 数组下标；下标与名字的对应关系由构建工具在报错信息中回显（见 §9.2 第 4 项），开发者不需要记忆
- bool 维度用 `#if DIMNAME`
- 文件顶部注明依赖的宏与对应声明位置（纯文档作用，构建工具不解析）：

```glsl
// Consumes macros: SHADER_MODE_X/Y(enum, 9 values), ALPHA_ONLY(bool), HAS_SUBSET(bool)
// Declared in: src/gpu/shaders/level1/TiledTextureFillShader.h
```

### 4.2 共享代码

跨 Shader 复用的算法片段（如 blend mode 公式、gradient colorizer 采样逻辑）放入 `src/gpu/shaders/common/*.glsl`，由构建工具在宏展开前做 `#include` 级联，不引入运行时 include 机制。

---

## 5. Shader 清单与变体规模分析

本节在读者已建立"一个 Shader 如何声明与消费"的认知模型（见 §3、§4）的基础上，给出现有 Processor 体系到 Shader 的映射全貌，以及各代表性 Shader 的变体规模估算，作为 §1.3 约束 C3（单 Pass 优先、变体规模可控）的实证依据。

### 5.1 分析方法与置信度声明

本节的变体数来自对 `src/gpu/processors/*.h/.cpp` 中 `onComputeProcessorKey()` 实现的直接阅读，而非估算。受篇幅限制，仅对 3 个代表性 FP 做逐 bit 分析（§5.3），其余 Processor 仅列出清单和角色（§5.2），标注为**待细化**——在进入实现阶段前，需要对每个 Processor 补齐同等深度的 key 分析，本文档不假装已完成这项工作。

### 5.2 现有 Processor 清单

TGFX 当前有 **12 个 GeometryProcessor 子类**、**22 个 FragmentProcessor 子类**、**2 个 XferProcessor 子类**（`src/gpu/processors/` 下 `DEFINE_PROCESSOR_CLASS_ID` 的实际数量，2026-07-02 统计；后续新增 Processor 需同步更新本表）：

| GeometryProcessor（12） | FragmentProcessor（22） | XferProcessor（2） |
|---|---|---|
| DefaultGeometryProcessor | AARectEffect | EmptyXferProcessor |
| QuadPerEdgeAAGeometryProcessor | AlphaThresholdFragmentProcessor | PorterDuffXferProcessor |
| NonAARRectGeometryProcessor | ClampedGradientEffect | |
| ComplexNonAARRectGeometryProcessor | ColorMatrixFragmentProcessor | |
| RoundStrokeRectGeometryProcessor | ColorSpaceXformEffect | |
| EllipseGeometryProcessor | ComposeFragmentProcessor | |
| ComplexEllipseGeometryProcessor | ConicGradientLayout | |
| HairlineLineGeometryProcessor | ConstColorProcessor | |
| HairlineQuadGeometryProcessor | DeviceSpaceTextureEffect | |
| MeshGeometryProcessor | DiamondGradientLayout | |
| ShapeInstancedGeometryProcessor | DualIntervalGradientColorizer | |
| AtlasTextGeometryProcessor | GaussianBlur1DFragmentProcessor | |
| | LinearGradientLayout | |
| | LumaFragmentProcessor | |
| | PerlinNoiseFragmentProcessor | |
| | RadialGradientLayout | |
| | SingleIntervalGradientColorizer | |
| | TextureEffect | |
| | TextureGradientColorizer | |
| | TiledTextureEffect | |
| | UnrolledBinaryGradientColorizer | |
| | XfermodeFragmentProcessor | |

按渐变家族（`*GradientLayout`/`*GradientColorizer`）与合成家族（`Compose`/`Xfermode`）归类后，独立的"渐变 shader 组合逻辑"数量少于 22，具体归并方式在 Shader 清单细化阶段确定，不在本文档中预先假定。

### 5.3 代表性 Processor 的变体分析

#### 5.3.1 TextureEffect（RGBA/YUV/RGBAAA 纹理填充）

对应 §6.4 中 `TextureFillShader` 的生产环境完整声明（§2.4 是其教学简化版，仅保留其中 4 个维度）。该 FP 的 key 由以下独立 bit 组成（见 `TextureEffect.cpp:48-66`）：

| bit | 含义 | 来源 |
|:---:|------|------|
| 0 | `yuvTexture != nullptr` | 是否为 YUV 纹理 |
| 1 | `alphaStart == Point::Zero()`（RGBAAA 布局标志） | 是否存在独立 alpha 平面 |
| 2 | `textureProxy->isAlphaOnly()` | 纹理是否为纯 alpha 格式 |
| 3 | `yuvTexture->yuvFormat() != I420`（仅 YUV 纹理时有效） | YUV 采样格式 |
| 4 | YUV color range 是否为 limited（仅 YUV 纹理时有效） | YUV 色彩范围 |
| 5 | `needSubset()` | 是否需要子区域裁剪采样 |
| 6 | `constraint == Strict` | 采样约束模式 |
| 7 | `coordTransform.matrix.hasPerspective()` | 坐标变换是否含透视 |

8 个 bit 对应 §6.4 中 `TGFX_DEFINE_DIMS` 声明的 8 个维度（`HAS_YUV`/`YUV_FORMAT_NOT_I420`/`YUV_RANGE_FULL`/`ALPHA_ONLY`/`HAS_RGBAAA`/`HAS_SUBSET`/`STRICT_CONSTRAINT`/`HAS_PERSPECTIVE`，声明顺序与本表 bit 顺序不必一致——§6.2 已说明 bit 权重仅由声明顺序决定，不隐含语义上的重要性排序）。裸组合数（未裁剪）：2⁸ = 256；经 §6.4 `ShouldCompile` 排除"YUV 纹理同时具有 alpha-only 或 RGBAAA"的结构性不可达组合后，有效变体数需由构建工具在枚举阶段实际统计（构建报告见 §9.2 第 7 条），本文档不预先给出裁剪后的精确数字。

#### 5.3.2 TiledTextureEffect（环绕采样填充）

依据 `TiledTextureEffect.h:41-51` 与 `TiledTextureEffect.cpp:145-159`。`ShaderMode` 枚举有 **9 个值**（`None`/`Clamp`/`RepeatNearestNone`/`RepeatLinearNone`/`RepeatLinearMipmap`/`RepeatNearestMipmap`/`MirrorRepeat`/`ClampToBorderNearest`/`ClampToBorderLinear`），X/Y 两轴独立取值：

| 维度 | 取值数 | 来源 |
|------|:---:|------|
| `shaderModeX` | 9 | X 轴 `ShaderMode` |
| `shaderModeY` | 9 | Y 轴 `ShaderMode`（`onComputeProcessorKey` 中 `<< 4` 与 X 轴打包） |
| `isAlphaOnly` | 2 | `textureProxy->isAlphaOnly()` |
| `constraint == Strict` | 2 | `SrcRectConstraint` |
| `hasPerspective` | 2 | 坐标变换是否含透视 |

裸组合数 = 9 × 9 × 2 × 2 × 2 = **648**。注意 `ShaderMode::None` 表示该轴完全由硬件采样器处理（`resolve()` 提前返回 `hwMode`），此时对应轴不需要任何 shader 分支代码；`X=None, Y=None` 时该 FP 实际退化为一次普通纹理采样。是否需要为 `ShaderMode::None` 单独设计一个更简单的 shader 变体（而非把它当作 9 选 1 的普通枚举值编译一份包含无用分支的代码）是构建期裁剪规则需要明确回答的问题，不能仅通过笛卡尔积裸算。

**该 FP 的 `getTextureView()` 显式排除 YUV 纹理**（`TiledTextureEffect.cpp:183-189`：`if (textureView && !textureView->isYUV()) return textureView; return nullptr;`）。因此本 FP 与 §5.3.1 的 `TextureEffect` 是**两个职责不重叠的 Shader**：前者处理环绕采样模式，后者处理格式/YUV/RGBAAA；两者不应共用同一个示例混合其维度，这正是此前版本"抽象层次混乱"问题的一个具体例证。

#### 5.3.3 PerlinNoiseFragmentProcessor

依据 `PerlinNoiseFragmentProcessor.h:33-61` 与 `src/core/shaders/PerlinNoiseShader.h:29,34`：

| 维度 | 取值数 | 来源 |
|------|:---:|------|
| `noiseType` | 2 | `PerlinNoiseType { FractalNoise, Turbulence }` |
| `numOctaves` | 当前上限 255（`MAX_OCTAVES`，`PerlinNoiseShader.h:34`） | `std::clamp(numOctaves, 1, MAX_OCTAVES)` |
| `stitchTiles` | 2 | bool |

**当前上限 255 直接编译期枚举不可行**（255 × 2 × 2 = 1020 个变体仅此一个 FP，与 C3 的"单 Pass 优先、变体规模可控"目标冲突）。此前 `shader-precompile-master-plan.md` §5.2 提出将 API 层上限收紧至 8（依据：8 阶以上振幅衰减 <0.4%，视觉不可分辨）。本文档**采纳该收紧方向作为设计前提**，但明确：

1. 该收紧改变 `PerlinNoiseShader` 的公开行为（`include/tgfx/core/shaders/` 下对应的公开 API 语义收窄），属于需要单独评审确认的 API 变更，不是 Shader Permutation 系统内部可以单方面决定的实现细节；
2. 收紧后必须完整覆盖 1–8 的全部取值（16 个变体：8 × 2），不能只精编常用低阶值、高阶值退化到某个近似值，否则违背"预编译集合封闭、不存在 miss"的设计前提。

在收紧决策被确认前，`PerlinNoiseFragmentProcessor` 暂列为**待决维度**，不计入 §5.4 的单 Pass 覆盖率统计。

### 5.4 单 Pass / 多 Pass 判定规则

**判定原则**：某个 Shader 类的 Permutation 笛卡尔积经 `ShouldCompile` 裁剪后，若不显著膨胀（参考阈值：单个 Shader ≤ 256 个已编译变体，超过由 §9.2 第 6 项检查告警——该阈值为工程经验值，大致对应"8 个独立 bool 维度全展开"的规模（如 §6.4 生产版 `TextureFillShader` 的 256 种裸组合），未经严格的包体或构建时长反推，超出阈值时应视为需要人工评估而非自动阻断），优先归入该 Shader 的 Permutation 维度，保持单 Pass；否则将该维度拆分到独立的 Shader 类，通过多 Pass 串联。

| 场景 | 处理方式 | 说明 |
|------|:---:|------|
| GP + 单个 colorFP（含 §5.3 的纹理/渐变/常量色等）+ 可选 coverageFP | 单 Pass | 对应某个 Level-1 Shader 的 Permutation 组合 |
| GP + colorFP + `ColorMatrixFragmentProcessor`/`AlphaThresholdFragmentProcessor` 等类型数量有限的 colorFilter | 单 Pass | colorFilter 类型枚举有界，可并入同一 Shader 的 Permutation 维度或设计为独立小型 Level-2 Shader |
| GP + colorFP + `GaussianBlur1DFragmentProcessor`（maskFilter 场景） | 多 Pass | 涉及独立的模糊处理阶段，且其作为 coverageFP 时在单 Pass 与多 Pass 下的时序不同（详见 `shader-precompile-master-plan.md` §15.5 已登记的待办问题，本文档不重复展开，仅继承其结论：需要专项设计） |
| `ColorFilter` + `MaskFilter` + 高级 `BlendMode`（需读取 dst texture 的合成模式）同时出现 | 多 Pass | 多段拆分，每段仍是预编译 Shader |
| `colors[0]` 为 `Shader::MakeBlend()` 产生的 `XfermodeFragmentProcessor` 递归嵌套（§2.3.1 事实 2） | 多 Pass | 逐层拆解：每层的两个子 Shader 各自渲染到临时纹理（子树若已是单叶子则本身仍为单 Pass），再用一个仅按 `BlendMode` × 是否有 Child 区分的合并 Shader 完成该层拼接；递归深度与嵌套层数一致，详见 §8.4 |

关于 94.3%/5.4% 这一类具体比例：该数据来自 `shader-precompile-master-plan.md` §3.3 对 802 次 draw 调用的采样统计，采样来源为测试用例而非生产环境。本文档的架构决策（单 Pass 优先）不依赖该比例的精确值，仅依赖其方向性结论（多数场景可用单 Pass 覆盖），故不在本文档中重复引用该数字作为设计依据；如需在评审中使用该数据，应直接引用原始采样文档并注明其代表性尚待生产环境验证。

---

## 6. Permutation 声明系统详细设计

本节给出 §3 所述工作流中涉及的全部 C++ 类型的完整接口定义，是实现与评审阶段的查阅对象；理解 §3、§4、§5 不以本节内容为前提。

### 6.1 维度类型

提供 3 种维度类型，用以覆盖 §5 中实际观察到的 GP/FP key 参数类型（bool 开关、枚举、有界整数如 mip level 数）：

```cpp
// src/gpu/shaders/ShaderPermutation.h

namespace tgfx {

// A boolean permutation dimension. Maps to a single #define NAME 0/1 in the shader.
class PermutationBool {
 public:
  constexpr explicit PermutationBool(const char* defineName) : defineName(defineName) {
  }

  static constexpr int valueCount() {
    return 2;
  }

  const char* defineName;
};

// An enum permutation dimension. valueNames.size() must equal the enum's value count.
// The generated shader macro is defined to the zero-based index of the enum value.
// Not constexpr: valueNames is a std::vector, whose construction from an initializer_list
// involves a heap allocation and is therefore not a literal-type operation under C++17.
class PermutationEnum {
 public:
  PermutationEnum(const char* defineName, std::initializer_list<const char*> valueNames)
      : defineName(defineName), valueNames(valueNames) {
  }

  int valueCount() const {
    return static_cast<int>(valueNames.size());
  }

  const char* defineName;
  std::vector<const char*> valueNames;
};

// A bounded integer dimension, value range is [0, count).
class PermutationInt {
 public:
  constexpr PermutationInt(const char* defineName, int count)
      : defineName(defineName), count(count) {
  }

  int valueCount() const {
    return count;
  }

  const char* defineName;
  int count;
};

}  // namespace tgfx
```

> 说明：不采用 UE `TShaderPermutationDomain<Dim1, Dim2, ...>` 式的编译期模板参数包。TGFX 的 Processor 数量为 36 个（12 个 GP + 22 个 FP + 2 个 XP，见 §5.2），Shader 类数量与之同量级，不构成模板实例化膨胀的压力；运行时用 `std::vector<std::variant<...>>` 描述维度组合，类型安全通过 §6.3 的具名访问方式保证，而非依赖模板参数位置。

> 对于像 §6.4 `TextureFillShader` 这样的全 bool 维度场景，不需要逐个手写上述 `PermutationBool` 列表——推荐使用 §6.3 的 `TGFX_DEFINE_DIMS` 宏一行声明；该宏内部生成的仍是由 `PermutationBool` 组成的 `PermutationDomain`，与本节的底层类型完全一致，只是省去了手写下标与保持顺序同步的负担。含 `PermutationEnum`/`PermutationInt` 的维度目前仍需按本节方式手写 `PermutationDomain` 构造（见 §6.2）。

### 6.2 PermutationDomain：组合与索引编解码

`PermutationDomain` 采用 mixed-radix 编码把若干维度组合成一个 `uint32_t` index，index 与"具体取值组合"之间构成双射。编码公式明确规定为：

```
index = Σ_i  v[i] * stride[i]，i ∈ [0, N)，N = dimensionCount()；stride[0] = 1，stride[i] = stride[i-1] * dimensions[i-1].valueCount()
```

即第 0 个维度（声明顺序中最先出现的）权重最小、位于最低位/最低位数；后续维度依次按前面所有维度 `valueCount()` 的累积乘积递增权重。当全部维度的 `valueCount() == 2`（即全 bool domain，如 §6.5 的 `TextureFillShader`）时，该公式退化为标准的 LSB-first 位打包：

```
index = Σ_i  v[i] * 2^i，i ∈ [0, N)
```

也就是第 `i` 个维度恰好占第 `i` 个 bit——这是 §6.5 直接使用 `1u << D::DIM_NAME` 位运算、且其结果与 `domain.encode({...})` 完全等价的前提。`encode()`/`decode()` 的实现必须严格遵循上述公式，不得采用"高位在前"（MSB-first）或其他打包顺序。

```cpp
// src/gpu/shaders/ShaderPermutation.h（续）

namespace tgfx {

using PermutationDimension = std::variant<PermutationBool, PermutationEnum, PermutationInt>;

// Combines multiple permutation dimensions into a single packed index space.
// The total permutation count is the product of every dimension's valueCount().
class PermutationDomain {
 public:
  explicit PermutationDomain(std::vector<PermutationDimension> dimensions);

  // Parses a comma-separated list of boolean dimension names into a domain made of
  // PermutationBool dimensions, one per name, in the order they appear. Each name is
  // trimmed of leading/trailing whitespace before use (the input typically comes from a
  // stringified macro argument list such as "HAS_YUV, ALPHA_ONLY", which contains a space
  // after each comma). The input is the stringified argument list produced by the
  // TGFX_DEFINE_DIMS macro (see §6.3); this method is that macro's underlying
  // implementation, not a general-purpose parser.
  static PermutationDomain FromBoolNames(const char* commaSeparatedNames);

  // Total number of raw combinations (before ShouldCompile filtering).
  uint32_t totalCount() const;

  size_t dimensionCount() const;

  // Decodes a raw index into per-dimension values, in declaration order. Follows the
  // encoding formula defined in §6.2 (dimension 0 occupies the lowest bits/digits).
  std::vector<int> decode(uint32_t index) const;

  // Encodes per-dimension values back into a raw index. values.size() must equal
  // dimensionCount(), in the same declaration order used at construction time. Must be
  // the exact inverse of decode() under the §6.2 encoding formula.
  uint32_t encode(const std::vector<int>& values) const;

  // Returns the #define list for a given raw index, e.g. {"FORMAT=1", "HAS_SUBSET=1"}.
  // Every dimension always contributes exactly one entry regardless of its value —
  // a bool dimension whose value is 0 still emits "HAS_SUBSET=0" rather than being
  // omitted, so that shader code using `#if HAS_SUBSET` never sees an undefined macro.
  std::vector<std::string> defineListFor(uint32_t index) const;

 private:
  std::vector<PermutationDimension> dimensions;
};

}  // namespace tgfx
```

### 6.3 PrecompiledShader 基类与自注册

一个 `.h` 文件声明一个 Shader 的完整元数据，通过静态自注册加入全局表；构建工具链接这些描述类并调用其静态方法，不解析任何 C++ 源码：

```cpp
// src/gpu/shaders/PrecompiledShader.h

namespace tgfx {

struct PrecompiledShaderInfo {
  std::string name;             // Unique shader name, used as shaderID in PipelineKey.
  std::string vertexFile;       // Relative path under src/gpu/shaders/.
  std::string fragmentFile;
  PermutationDomain domain;
  // Returns false to exclude this raw permutation index from being compiled.
  // dimValues follows the declaration order of `domain`.
  std::function<bool(const std::vector<int>& dimValues)> shouldCompile;
};

// Base class for every precompiled shader descriptor. Subclasses are pure declarations:
// they carry no rendering logic, only permutation metadata, so the build tool can link
// against this descriptor set without pulling in the renderer.
class PrecompiledShader {
 public:
  virtual ~PrecompiledShader() = default;

  virtual PrecompiledShaderInfo info() const = 0;
};

// Registers a PrecompiledShader subclass into the global registry at static-init time.
// Every registered shader is compiled by shader_build_tool; there is no other discovery path.
class ShaderRegistry {
 public:
  using Factory = std::unique_ptr<PrecompiledShader> (*)();

  static void Register(Factory factory);

  static const std::vector<Factory>& All();
};

// ClassName may be fully qualified (e.g. tgfx::TextureFillShader as used in §2.4), so the
// expansion must not use `##` to paste it into a new identifier — pasting a token that
// contains `::` is not a single preprocessing-token and is undefined behavior per the
// C++ standard. __COUNTER__ is used instead to generate a unique registrar name per
// translation unit (supported by GCC/Clang/MSVC). __COUNTER__ increments on every
// reference, so it must be expanded exactly once as a macro *argument* — passed through
// TGFX_REGISTER_SHADER_IMPL — and then reused via the resulting parameter (Counter) rather
// than re-invoking __COUNTER__ inside the implementation macro; otherwise the struct name
// and its constructor name would end up with different, mismatching suffixes.
#define TGFX_CONCAT_IMPL(a, b) a##b
#define TGFX_CONCAT(a, b) TGFX_CONCAT_IMPL(a, b)

#define TGFX_REGISTER_SHADER_IMPL(ClassName, Counter)                        \
  namespace {                                                                \
  struct TGFX_CONCAT(ShaderRegistrar_, Counter) {                            \
    TGFX_CONCAT(ShaderRegistrar_, Counter)() {                               \
      ShaderRegistry::Register([]() -> std::unique_ptr<PrecompiledShader> {  \
        return std::make_unique<ClassName>();                                \
      });                                                                    \
    }                                                                        \
  } TGFX_CONCAT(g_shaderRegistrar_, Counter);                                \
  }

#define TGFX_REGISTER_SHADER(ClassName) TGFX_REGISTER_SHADER_IMPL(ClassName, __COUNTER__)

}  // namespace tgfx
```

对于全 bool 维度的 Shader（TGFX 中的多数场景，见 §5），`TGFX_DEFINE_DIMS` 宏把"维度声明"与"维度下标"这两件此前需要在 `PermutationDomain` 构造列表与 `ShouldCompile`/运行时代码两处手动保持同步的事情合并为一处声明：

```cpp
// src/gpu/shaders/ShaderPermutation.h（续）

// Declares a fixed list of boolean permutation dimensions as a nested `Dims` type,
// generating both a named zero-based index for each dimension and the PermutationDomain
// built from the same list — so the two can never drift apart. Intended for use inside a
// PrecompiledShader subclass; see §6.4 for a complete example.
//
// Usage:
//   TGFX_DEFINE_DIMS(HAS_YUV, ALPHA_ONLY, HAS_SUBSET);
//   using D = Dims;
// Expands to a struct equivalent to:
//   struct Dims {
//     enum : uint32_t { HAS_YUV = 0, ALPHA_ONLY = 1, HAS_SUBSET = 2, COUNT };
//     static PermutationDomain domain();
//   };
#define TGFX_DEFINE_DIMS(...)                                    \
  struct Dims {                                                  \
    enum : uint32_t { __VA_ARGS__, COUNT };                      \
    static PermutationDomain domain() {                          \
      return PermutationDomain::FromBoolNames(#__VA_ARGS__);     \
    }                                                             \
  }
```

`COUNT` 是编译期常量，用于 `static_assert(D::COUNT == N)`：增删维度后，若忘记同步更新依赖该数量的代码（`ShouldCompile` 里的裁剪逻辑、运行时拼装 index 的代码），编译直接失败而不是留下静默的下标错位，详见 §9.1。

### 6.4 生产环境完整声明：TextureFillShader

§2.4 给出的是教学简化版（4 个维度）。本节给出 `TextureEffect` 对应的生产环境完整声明——8 个维度，与 §5.3.1 的 bit 分析一一对应，以 `src/gpu/processors/TextureEffect.h/.cpp` 的实际 `onComputeProcessorKey()` 逻辑为依据（而非凭空设计维度）。

**C++ 侧——Permutation 维度声明**：

```cpp
// src/gpu/shaders/level1/TextureFillShader.h

namespace tgfx {

class TextureFillShader : public PrecompiledShader {
 public:
  TGFX_DEFINE_DIMS(HAS_YUV, YUV_FORMAT_NOT_I420, YUV_RANGE_FULL, ALPHA_ONLY, HAS_RGBAAA,
                   HAS_SUBSET, STRICT_CONSTRAINT, HAS_PERSPECTIVE);
  using D = Dims;
  static_assert(D::COUNT == 8, "Update ShouldCompile below when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"TextureFillShader", "level1/texture_fill.vert", "level1/texture_fill.frag",
            D::domain(), ShouldCompile};
  }

 private:
  // YUV textures never carry an independent alpha plane or alpha-only format
  // (see TextureEffect::onComputeProcessorKey), so this combination is excluded.
  static bool ShouldCompile(const std::vector<int>& v) {
    bool hasYuv = v[D::HAS_YUV] != 0;
    bool alphaOnly = v[D::ALPHA_ONLY] != 0;
    bool hasRgbaaa = v[D::HAS_RGBAAA] != 0;
    return !(hasYuv && (alphaOnly || hasRgbaaa));
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TextureFillShader)
```

**Shader 侧——渲染逻辑**（文件组织与命名约定见 §4）：

```glsl
// src/gpu/shaders/level1/texture_fill.frag
#version 450

layout(location = 0) in vec2 v_texCoord;
layout(location = 0) out vec4 fragColor;

#if HAS_YUV
layout(set = 0, binding = 0) uniform sampler2D u_texY;
layout(set = 0, binding = 1) uniform sampler2D u_texU;
layout(set = 0, binding = 2) uniform sampler2D u_texV;
#else
layout(set = 0, binding = 0) uniform sampler2D u_sampler;
#if HAS_RGBAAA
layout(set = 0, binding = 1) uniform sampler2D u_alphaSampler;
#endif
#endif

#if HAS_SUBSET
layout(set = 0, binding = 3) uniform SubsetBlock { vec4 u_subset; };
#endif

#if HAS_YUV
vec3 YuvToRgb(vec3 yuv) {
#if !YUV_RANGE_FULL
  yuv = yuv * vec3(1.164, 1.164, 1.164) - vec3(0.0627, 0.0627, 0.0627);
#endif
  float u = yuv.y - 0.5, v = yuv.z - 0.5;
  // YUV_FORMAT_NOT_I420 only changes which samplers are bound on the CPU side.
  return vec3(yuv.x + 1.402 * v, yuv.x - 0.344 * u - 0.714 * v, yuv.x + 1.772 * u);
}
#endif

void main() {
  vec2 coord = v_texCoord;

#if HAS_SUBSET
#if STRICT_CONSTRAINT
  coord = clamp(coord, u_subset.xy, u_subset.zw);
#else
  coord = clamp(coord, u_subset.xy - 0.5, u_subset.zw + 0.5);
#endif
#endif

#if HAS_YUV
  vec3 yuv = vec3(texture(u_texY, coord).r, texture(u_texU, coord).r, texture(u_texV, coord).r);
  vec4 color = vec4(YuvToRgb(yuv), 1.0);
#else
  vec4 color = texture(u_sampler, coord);
#if ALPHA_ONLY
  color = vec4(0.0, 0.0, 0.0, color.a);
#elif HAS_RGBAAA
  color.a = texture(u_alphaSampler, coord).r;
#endif
#endif

  fragColor = color;
}
```

其 Permutation 声明的类型设计要点：8 个维度均为独立 bool（`HAS_RGBAAA`/`ALPHA_ONLY`/`YUV_FORMAT_NOT_I420`/`YUV_RANGE_FULL` 等互不嵌套），因此整组维度可用一次 `TGFX_DEFINE_DIMS` 声明（见 §6.3），无需手写 `PermutationDomain` 构造列表。其中 `YUV_FORMAT_NOT_I420`/`YUV_RANGE_FULL` 仅在存在 YUV 纹理时才被 shader 消费，声明为与 `HAS_YUV` 平级的独立 bool，而非嵌入某个枚举，避免引入一个"部分维度依赖另一个维度是否生效"的隐式耦合。

`ShouldCompile` 通过 `D::HAS_YUV`/`D::ALPHA_ONLY`/`D::HAS_RGBAAA` 等具名下标访问 `dimValues`，排除"YUV 纹理同时具有 alpha-only 或 RGBAAA"的组合：现有实现中 YUV 纹理不会同时具有独立 alpha 平面或纯 alpha 格式（依据见 §5.3.1），该组合结构性不可达，裁剪掉可避免编译死变体。类内 `static_assert(D::COUNT == 8)` 确保后续增删维度时，若忘记同步更新 `ShouldCompile` 里的裁剪逻辑，编译直接报错而非静默产生错误的裁剪结果。裁剪后的理论变体数见 §5.3.1。

### 6.5 运行时选择 API

运行时复用同一份 `ShaderPermutation.h`（同时编入运行时库与构建工具），不重新实现编码逻辑。对于像 `TextureFillShader` 这样全部维度均为 bool 的 Shader，每个维度在 `PermutationDomain` 打包的 index 中固定占 1 bit（位序与 `TGFX_DEFINE_DIMS` 的声明顺序一致），因此运行时可以直接用 `D::` 具名下标做位运算拼出 index，效果与调用 `domain.encode({...})` 完全等价，但不需要先按位置构造一个 0/1 的 vector：

```cpp
// 运行时调用点示例
using D = TextureFillShader::Dims;
static_assert(D::COUNT == 8, "Update the bit assembly below when dimensions change.");

uint32_t permutationIndex = 0;
if (hasYuv) permutationIndex |= 1u << D::HAS_YUV;
if (yuvFormatNotI420) permutationIndex |= 1u << D::YUV_FORMAT_NOT_I420;
if (yuvRangeFull) permutationIndex |= 1u << D::YUV_RANGE_FULL;
if (alphaOnly) permutationIndex |= 1u << D::ALPHA_ONLY;
if (hasRgbaaa) permutationIndex |= 1u << D::HAS_RGBAAA;
if (needSubset) permutationIndex |= 1u << D::HAS_SUBSET;
if (strictConstraint) permutationIndex |= 1u << D::STRICT_CONSTRAINT;
if (hasPerspective) permutationIndex |= 1u << D::HAS_PERSPECTIVE;

PipelineKey key{"TextureFillShader", permutationIndex, renderTargetFormat, sampleCount, blendState};
```

含 `PermutationEnum`/`PermutationInt` 维度的 Shader 不满足"每个维度恰好占 1 bit"的前提，仍需通过 `domain.encode({...})` 走通用的按位置传值路径。

`PipelineKey` 完整定义见 [附录 B](#附录-bpipelinekey-完整定义)。

---

## 7. 构建工具链

### 7.1 整体流程

```
CMake target: shader_build_tool
  依赖: src/gpu/shaders/**/*.h（全部 PrecompiledShader 子类，静态 link 进来）
        src/gpu/shaders/ShaderPermutation.h / PrecompiledShader.h（描述层，不依赖渲染器运行时）
        glslang / spirv-cross / tint（仅工具链依赖，不进入 App 运行时二进制）

CMake custom command:
  COMMAND shader_build_tool
      --shader-dir src/gpu/shaders
      --out-dir    build/generated
      --backends   opengl,vulkan,metal,webgpu
  DEPENDS <shader_build_tool 本身> + src/gpu/shaders/**/*.{h,vert,frag}
  OUTPUT   build/generated/shader_bundle.{opengl,vulkan,metal,webgpu}.bin
           build/generated/shader_build_report.json
```

`main()` 逻辑：

```cpp
// tools/shader_build_tool/main.cpp

int main(int argc, char** argv) {
  BuildOptions options = ParseArgs(argc, argv);
  BuildReport report;

  for (const auto& factory : ShaderRegistry::All()) {
    std::unique_ptr<PrecompiledShader> shader = factory();
    CompileOneShader(shader->info(), options, &report);
  }

  WriteBundles(report, options.outDir, options.backends);
  WriteReportJson(report, options.outDir + "/shader_build_report.json");

  if (report.hasErrors()) {
    PrintErrorsToStderr(report);
    return 1;  // Non-zero exit fails the CMake build.
  }
  return 0;
}

void CompileOneShader(const PrecompiledShaderInfo& info, const BuildOptions& options,
                      BuildReport* report) {
  std::string vertexSource = ReadFile(options.shaderDir + "/" + info.vertexFile);
  std::string fragmentSource = ReadFile(options.shaderDir + "/" + info.fragmentFile);

  for (uint32_t index = 0; index < info.domain.totalCount(); ++index) {
    std::vector<int> dimValues = info.domain.decode(index);
    if (!info.shouldCompile(dimValues)) {
      continue;  // Excluded, not compiled, not counted as an error.
    }
    auto defines = info.domain.defineListFor(index);
    std::string expandedVertex = PrependDefines(vertexSource, defines);
    std::string expandedFragment = PrependDefines(fragmentSource, defines);
    // The permutation dimension (index) and the backend dimension (options.backends) are
    // independent axes: the same macro-expanded GLSL is the shared input, but each backend
    // runs its own translation chain (§7.2) and produces a separate blob appended to that
    // backend's own Bundle. This inner loop is where the backend axis is iterated.
    for (Backend backend : options.backends) {
      CompileVariant(info.name, index, backend, expandedVertex, expandedFragment, options,
                     report);
    }
  }
}
```

`WriteBundles`/`WriteReportJson`/`CompileVariant`/`PrependDefines` 的具体签名（`BuildReport` 的字段设计、输出路径参数形式等）留待实现阶段确定，此处的调用形式仅用于说明流程顺序，不构成接口约定。

### 7.2 后端转换链路

沿用现有各后端的翻译工具，执行时机由"运行时首次遇到该 programKey"改为"构建期对每个 permutation 各跑一次"：

| 后端 | 链路 |
|------|------|
| OpenGL | 宏展开后的 GLSL 直接作为产物（运行时仍需 `glCompileShader`，驱动编译无法跳过） |
| Vulkan | GLSL → glslang → SPIR-V |
| Metal | GLSL → glslang → SPIR-V → spirv-cross → MSL |
| WebGPU | GLSL → glslang → SPIR-V → tint → WGSL |

OpenGL 需按 `ShaderCaps` profile（framebuffer fetch 扩展可用性、ES/Desktop 版本差异）拆分为多个 Bundle；`src/gpu/ShaderCaps.cpp` 中已有的能力探测逻辑决定运行时加载哪一份，具体 profile 划分留待实现阶段按当前 `ShaderCaps` 实际探测维度确定，不在本文档中预先固定 profile 数量。

**profile 选择逻辑的最小设计**：构建工具为每个 profile 生成的 Bundle 文件名包含该 profile 的标识（如 `shader_bundle.opengl.{profileTag}.bin`，`profileTag` 由参与拆分的 `ShaderCaps` 能力位按固定顺序拼接而成，例如 `es3_fbfetch`/`desktop_no_fbfetch`）；运行时 `PrecompiledShaderCache::loadBundle()`（§8.2）在 `Context` 创建完成、`ShaderCaps` 探测结果已知之后，用同一套拼接规则算出 `profileTag`，据此拼出文件路径并加载对应文件。该拼接规则与构建工具侧共享同一实现（放入 §6 所述的共享层 `ShaderPermutation.h` 或同级头文件），避免运行时与构建期各自维护一份可能分叉的拼接逻辑。

### 7.3 CMake 依赖追踪

```cmake
# CONFIGURE_DEPENDS (CMake 3.12+, supported by the Ninja and Makefile generators used by
# this project) makes CMake re-glob at the start of every build, so that adding or removing
# a shader source file triggers a re-configure and updates DEPENDS accordingly. Without it,
# GLOB results are fixed at the last configure time and newly added files would silently
# fail to be picked up until the next manual re-configure.
file(GLOB_RECURSE TGFX_SHADER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/gpu/shaders/*.h
    ${CMAKE_SOURCE_DIR}/src/gpu/shaders/*.vert
    ${CMAKE_SOURCE_DIR}/src/gpu/shaders/*.frag
)

add_custom_command(
    OUTPUT ${TGFX_SHADER_BUNDLE_OUTPUTS}
    COMMAND shader_build_tool --shader-dir ${CMAKE_SOURCE_DIR}/src/gpu/shaders
                              --out-dir ${CMAKE_BINARY_DIR}/generated
    DEPENDS shader_build_tool ${TGFX_SHADER_SOURCES}
    COMMENT "Compiling precompiled shader permutations"
)

add_custom_target(tgfx_shader_bundles DEPENDS ${TGFX_SHADER_BUNDLE_OUTPUTS})
add_dependencies(TGFXFullTest_OpenGL tgfx_shader_bundles)
```

改一个 `.frag` 触发重跑，等价于改一个 `.cpp` 后的正常增量编译体验；新增/删除 shader 文件因 `CONFIGURE_DEPENDS` 的存在同样能正确触发，符合 C4。

### 7.4 Bundle 格式

```
Bundle（每个 backend/profile 各一份）:
┌─────────────────────────────────────────────┐
│ FileHeader                                    │
├─────────────────────────────────────────────┤
│ IndexEntry[entryCount]                        │
├─────────────────────────────────────────────┤
│ Reflection Data Pool（uniform/sampler/attribute 布局） │
├─────────────────────────────────────────────┤
│ Shader Data Pool（VS/FS 二进制或文本）        │
└─────────────────────────────────────────────┘
```

```c
// All structs below are serialized verbatim into the Bundle binary file and must therefore
// have a platform-independent, padding-free layout: the build tool may run on a different
// host architecture than the one the App eventually runs on. `#pragma pack(push, 1)` /
// `#pragma pack(pop)` bracket every struct in this section so that field offsets and struct
// sizes are identical across compilers/platforms; without it, e.g. UniformDesc's trailing
// `uint8_t stage` would be silently padded to a 4-byte boundary on most ABIs.
#pragma pack(push, 1)

struct FileHeader {
  uint32_t magic;            // "TGSH"
  uint32_t version;
  uint32_t backend;          // 0=Vulkan, 1=Metal, 2=WebGPU, 3=OpenGL
  uint32_t glProfile;        // 非 OpenGL 后端为 0
  uint32_t entryCount;
  uint32_t indexOffset;
  uint32_t reflectionOffset;
  uint32_t dataOffset;
  uint32_t sourceHash[2];    // 源文件集合的 64-bit hash，用于 §9.3 版本校验
};

struct IndexEntry {
  uint64_t pipelineKeyHashHi;  // PipelineKey 128-bit hash 高 64 位，算法见附录 B
  uint64_t pipelineKeyHashLo;  // 低 64 位
  uint32_t vertexBlobOffset;
  uint32_t vertexBlobSize;
  uint32_t fragmentBlobOffset;
  uint32_t fragmentBlobSize;
  uint32_t reflectionOffset;   // 指向 ReflectionEntry
};

// Uniform/sampler/attribute 列表为变长数据，采用"定长头 + 变长子池偏移"序列化：
struct ReflectionEntry {
  uint32_t vertexUniformCount;
  uint32_t vertexUniformOffset;
  uint32_t fragmentUniformCount;
  uint32_t fragmentUniformOffset;
  uint32_t samplerCount;
  uint32_t samplerOffset;
  uint32_t attributeCount;
  uint32_t attributeOffset;
};

struct UniformDesc {
  char name[32];   // 与 GLSL 源码中的 uniform 名一致
  uint32_t offset;  // 在 UniformData buffer 中的字节偏移，见 §8.3
  uint32_t size;
  uint8_t stage;    // 0=Vertex, 1=Fragment
};

struct SamplerDesc {
  char name[32];
  uint8_t binding;
  uint8_t textureType;  // 与 TextureType 编码一致
};

struct AttributeDesc {
  char name[32];
  uint8_t location;
  uint8_t format;
};

#pragma pack(pop)
```

索引表按 `(pipelineKeyHashHi, pipelineKeyHashLo)` 字典序排列，构建期校验全局无重复，运行时二分查找。`ReflectionEntry`/`UniformDesc`/`SamplerDesc`/`AttributeDesc` 的数据来源按后端区分：

- Vulkan/Metal/WebGPU：从各自链路产出的 SPIR-V 做 reflection（如 `spirv-cross` 的 `Compiler::get_shader_resources()`）提取。
- OpenGL：**没有 SPIR-V 中间产物**，因此不走 SPIR-V reflection；构建工具对宏展开后的 GLSL 额外经由 glslang 编译一次（`glslang::TProgram::buildReflection()` / `getUniform()` 等 API）以提取 uniform/sampler/attribute 布局，随后**丢弃该次编译的中间结果，只保留原始 GLSL 文本**作为 Shader Data Pool 的产物。这条提取路径独立于运行时 `UniformHandler`（后者是现有拼接框架在 `emitCode()` 过程中动态收集的机制，预编译方案下不再经过该路径），二者不应混用。

---

## 8. 运行时集成

### 8.1 与现有 `GlobalCache` 的关系

TGFX 现有运行时已存在 Program 级 LRU 缓存：`GlobalCache::findProgram(const BytesKey&)` / `addProgram()`（`src/gpu/GlobalCache.h:44,67`），以 `BytesKey programKey` 为键，`programMap`/`programLRU` 维护生命周期。本方案**不替换该缓存机制**，而是替换"缓存未命中时如何生成 Program"这一步：

- **现状**：`ProgramInfo::getProgram()` 未命中 `GlobalCache` 时，调用 `ProgramBuilder::CreateProgram()` 动态拼接 GLSL 并编译
- **改造后**：未命中时改为 `context->precompiledShaderCache()->find(pipelineKey)` 查表取出构建期产物，再据此创建 `Program` 并调用 `GlobalCache::addProgram()` 写入缓存

即 `PipelineKey` 与 `BytesKey programKey` 的关系是：前者是构建期已知的、结构化的键（`shaderID + permutationIndex + 渲染状态`），后者是现有 `GlobalCache` 缓存层已经在用的通用键类型；运行时可将 `PipelineKey` 序列化为 `BytesKey`（`PipelineKey::toKey()`，见附录 B）以直接复用现有缓存基础设施，不需要新建一套并行的缓存结构。

需要注意：与 `GlobalCache` 共存的新旧编码不是来自 `RuntimeEffect`——`RuntimeEffect`（§8.5）的 `onDraw()` 由用户子类直接调用 `CommandEncoder`/`GPU::createShaderModule()` 实现，完全不经过 `ProgramInfo::getProgram()`/`GlobalCache`，因此不产生任何一种 `programKey` 编码，与本节讨论的缓存层无关。真正需要共存的两种编码来自本方案内部：新路径的 `PipelineKey::toKey()` 与下文"与现有代码的具体嫁接点"所述、继续保留作为过渡期兜底的旧路径 `ProgramBuilder::CreateProgram()` 所拼装的 `programKey`——二者服务同一个 `ProgramInfo::getProgram()` 分支，写入的也是同一个 `GlobalCache`。因此 `PipelineKey::toKey()` 必须在编码中加入一个固定的前缀 tag 字节（旧路径的 `programKey` 编码不使用、也不可能产生该 tag 值），确保两种来源的 key 在 `BytesKey` 空间内不会发生碰撞。

**与现有代码的具体嫁接点**：改造集中在 `ProgramInfo::getProgram()` 内部——现状下该方法在 `GlobalCache` 未命中时无条件调用 `ProgramBuilder::CreateProgram()`；改造后此处新增一个分支：先判断当前 Processor 组合是否可被 `EffectDecomposer::Decompose()`（§8.4）成功分解为一个或多个已注册 Shader 的 `PipelineKey`，可分解则走 `PrecompiledShaderCache`（§8.2）取产物创建 `Program`，不可分解则保留调用 `ProgramBuilder::CreateProgram()` 作为过渡期兜底路径。`ProgramBuilder`/`emitCode()`/`UniformHandler` 均保留，不删除，直到 §5 的 Shader 清单覆盖率达到可以下线动态拼接路径的程度——该下线时机是独立的、需要后续单独评审的里程碑决策，不在本文档中预设时间表。

### 8.2 `PrecompiledShaderCache`

新增组件，每个 `Context` 持有一个实例，在 `Context` 初始化时从嵌入的 Bundle 数据加载：

```cpp
class PrecompiledShaderCache {
 public:
  // Loads an embedded Bundle from in-memory data (the data is compiled into libtgfx.a
  // as a global const array, see implementation plan §13.1). The data pointer must remain
  // valid for the lifetime of this cache — guaranteed since embedded data lives in the
  // .rodata section of the library.
  bool loadBundle(const unsigned char* data, size_t size);

  // Returns nullptr if the key was not found — callers must handle this per §9.3.
  const PrecompiledShaderBlob* find(const PipelineKey& key) const;
};
```

`find()` 返回的是构建期产物（Shader 二进制/文本 + Reflection 布局），而非直接返回 `Program*`——是否创建 `Program`、以及是否命中 `GlobalCache`，仍遵循 §8.1 的既有分层。

**为何是实例方法而非静态方法**：缓存以 `Context`（而非全局单例）为粒度维护——一个进程内同时存在的多个 backend 的 `Context`（如同时使用 OpenGL 与 Vulkan）各自持有并加载自己 backend 对应的 Bundle，互不共享、互不影响。静态方法无法表达这种按 `Context` 隔离的语义，因此 `PrecompiledShaderCache` 是 `Context` 的一个成员（如 `context->precompiledShaderCache()`），调用方总是先取得所属 `Context` 的实例再调用 `find()`/`loadBundle()`。

**调用时机与线程安全**：

- `loadBundle()` 由 `Context` 构造函数内部（`_shaderCaps` 创建之后）按当前 backend/profile 从嵌入的全局数组中选择对应数据并调用一次，不需要用户手动触发；`Context` 创建完成即保证对应 Bundle 已加载完毕。
- `loadBundle()` 必须在同一实例上的任何 `find()` 调用之前完成，且完成后不支持动态重新加载或替换——`PrecompiledShaderCache` 内部存储在加载完成后视为只读，`find()` 因此天然线程安全，不需要额外加锁。

### 8.3 Uniform 填充机制

**Permutation 改造消除的是"生成 shader 代码"这一步，不改变"填充 uniform 数值"这一步**。现有 `FragmentProcessor::setData(UniformData*, UniformData*)`（`src/gpu/processors/FragmentProcessor.h:253`，内部调用子类的虚函数 `onSetData()`）在每次 draw 时把 Processor 实例的具体参数值写入 `UniformData`，这条路径与 shader 代码是运行时拼接还是构建期预编译无关，因此**不需要改造**。

需要改造的是"uniform 的名字 → buffer 偏移量/binding 的映射"这一层。现状下该映射由 `UniformHandler` 在 `emitCode()` 过程中动态收集（因为 uniform 声明本身就是拼接出来的一部分）；预编译后不再走这条路径，映射必须改为从 §7.4 Bundle 的 `ReflectionEntry`/`UniformDesc` 静态读取：

```
DrawOp::execute()
  → context->precompiledShaderCache()->find(pipelineKey) → 取得 UniformDesc[] 列表（来自 Bundle Reflection Data）
  → 按 UniformDesc.name 与 Processor 树中各 Processor 声明的 uniform 名对应
  → Processor::setData(vertexUniformData, fragmentUniformData) 写入数值（不变）
  → 按 UniformDesc.offset/size 把数值搬入按 Bundle 布局分配的 UniformData buffer
  → ProgramInfo::getUniformBuffer() / bindUniformBufferAndUnloadToGPU()（现有机制，不变）
```

`UniformDesc.name` 与 Processor 侧命名的一致性由 §4 的命名约定（`u_{name}`）保证。这一致性**无法在构建期被工具静态检查**：构建工具只链接、调用 `PrecompiledShader` 描述类的静态方法（§6.3），不解析、也不链接 Processor 的运行时源码（`onSetData()` 中的 uniform 名字是 C++ 字符串字面量，对构建工具不可见）；命名不匹配只会表现为运行时查找失败，只能通过 §9.3 的运行时防御检查或截图/集成测试暴露，不纳入 §9.2 的构建期检查项。

### 8.4 `EffectDecomposer`

职责：把一次 draw 的 Processor 树映射为已注册 Shader 的具体 Permutation 取值。这要求 `EffectDecomposer` 先识别 Processor 组合对应哪个已注册 Shader 类，再从各 Processor 实例的当前状态中提取出该 Shader 声明的每个维度的具体值——第二步需要一个新接口，因为现有 Processor 只暴露 `onComputeProcessorKey()` 产出不透明的 `BytesKey`，不产出可按维度下标寻址的取值列表：

```cpp
// Virtual method each FragmentProcessor/GeometryProcessor subclass overrides to
// participate in this scheme: returns this processor's current state as a list of
// permutation dimension values, in the declaration order of the target Shader's
// PermutationDomain (see §6.3's D:: named indices). Distinct from onComputeProcessorKey(),
// whose BytesKey output is opaque and not addressable by dimension.
//
// Default implementation returns an empty vector: Processors that never directly
// contribute a Permutation dimension (e.g. ComposeFragmentProcessor, which only forwards
// to its child FPs) do not need to override this method. EffectDecomposer treats an empty
// return as "this Processor contributes no dimension values to the matched Shader", not
// as an error.
virtual std::vector<int> onComputePermutationValues() const {
  return {};
}
```

```cpp
struct DecomposeResult {
  std::vector<PipelineKey> passes;  // size == 1 表示单 Pass；> 1 表示多 Pass。
};

class EffectDecomposer {
 public:
  static DecomposeResult Decompose(const GeometryProcessor* gp,
                                   const std::vector<const FragmentProcessor*>& fps,
                                   const XferProcessor* xp, BlendMode blendMode);
};
```

`Decompose` 的内部实现分三步：1) 依据匹配表识别 Processor 组合对应哪个已注册 Shader 类；2) 调用各相关 Processor 的 `onComputePermutationValues()` 取得维度取值；3) 用该 Shader 的 `PermutationDomain::encode()` 把取值编码为 `permutationIndex`，与其余渲染状态一并组装为 `PipelineKey`。分解规则见 §5.4；具体的匹配表（哪些 Processor 组合对应哪个已注册 Shader 类）应作为独立的、随 Shader 清单增长而持续维护的数据结构，不在本文档中固化，避免与 §5 的 Shader 清单产生重复维护点。

**递归分解容器节点（`XfermodeFragmentProcessor`/`ComposeFragmentProcessor`）**：这两类 FP 不是匹配表中的叶子条目，而是驱动 `Decompose` 递归下探的分支节点——对应 §2.3.1 事实 3 的具体实现：

1. `Decompose` 遇到容器节点时，先递归处理其每个 child FP（可能是叶子，也可能是另一个容器节点），每次递归返回一个 `DecomposeResult`（可能已经是多 Pass）。
2. 若两个子结果都是单 Pass（`passes.size() == 1`），且父容器是 `ComposeFragmentProcessor`，则不新增 Pass：把子结果的维度取值直接并入父节点对应的复合 Shader 的 Permutation（沿用 §5.4"归入同一 Shader"的判定原则）。
3. 若父容器是 `XfermodeFragmentProcessor`，或子结果本身已是多 Pass，则新增一个 Pass 边界：把两个子结果各自的最后一段 Pass 渲染到临时 `TextureProxy`（生命周期由调用本次 `Decompose` 的 `DrawOp` 持有，随该 draw 完成后释放，不进入 §8.1/§8.2 的任何缓存），再追加一个"合并 Shader"的 Pass，其输入是这两张临时纹理，维度仅为 `BlendMode`（§5.2 已枚举的有限集合）与"是否存在第二个 Child"（用于区分 `ComposeFragmentProcessor` 的无 blend 直接覆盖场景）。
4. 递归的终止条件是子节点为叶子 FP——叶子类型集合有限（§2.3.1 事实 1），因此该递归对任意深度的输入树都会在有限步内终止，产出一个 `DecomposeResult.passes` 列表。

该递归逻辑与其余匹配规则同属 §5.4 所述、构建期可验证的查表体系的一部分，不是运行时临时拼接。

### 8.5 `RuntimeEffect` 隔离的依据

`RuntimeEffect`（`include/tgfx/gpu/RuntimeEffect.h`）已使用独立的 `CommandEncoder → GPU::createShaderModule()` 路径，不经过 `ProgramBuilder` 和 FP 管线，因此本方案不影响其正确性。但 `RuntimeEffect` 仍需要 shaderc（及 Vulkan/Metal/WebGPU 可能需要的 spirv-cross/tint）作为运行时依赖——瘦身构建（移除运行时编译器）与保留 `RuntimeEffect` 能力是互斥的构建选项，两者服务对象不同，具体构建目标划分留待实现阶段确定。

---

## 9. 错误检查体系

区分编译期、构建期、运行时三层，多数错误应在构建期而非运行时暴露。

### 9.1 编译期（C++ 编译阶段）

| 检查项 | 实现方式 |
|--------|---------|
| Shader 类未实现 `info()` | 纯虚函数，直接编译报错 |
| `PermutationEnum::valueNames.size()` 与对应 C++ enum 的值数量不一致 | 无法在纯编译期检查（`initializer_list` 长度是运行时可读的编译期常量，但与外部 enum 值数量的一致性需要显式 `static_assert` 由开发者在每个 Shader 声明处手写，属于约定而非工具强制） |
| `ShouldCompile` 中的下标越界 | `dimValues` 是运行时 `vector`，无法编译期检查，下放至 §9.2 |
| `TGFX_DEFINE_DIMS` 声明的维度数量变化后，依赖该数量的代码（`ShouldCompile` 裁剪逻辑、运行时按位拼装 index 的代码）未同步更新 | 各处配套的 `static_assert(D::COUNT == N)`（见 §6.3–6.5）在维度数量变化时编译失败，强制开发者检视相关代码；该检查只覆盖"数量"是否一致，不覆盖"具体维度语义是否被正确处理"，后者仍依赖代码评审 |

### 9.2 构建期（`shader_build_tool` 运行时检查）

任一项失败使工具以非零退出码结束，令 CMake build 直接失败：

1. **目录扫描 vs 注册表比对**：`info().fragmentFile`/`vertexFile` 指向的文件不存在 → 报错阻断；反之存在但未被任何已注册 Shader 引用 → 警告（不阻断）
2. **维度声明自检**：同一 `PermutationDomain` 内两个维度使用相同 `defineName` → 报错（否则宏互相覆盖）；`PermutationEnum::valueNames` 内部重名 → 报错
3. **`ShouldCompile` 下标安全性**：调用前校验 `dimValues.size() == domain.dimensionCount()`
4. **宏展开后的 GLSL 语法校验**：每个变体展开后调用 glslang 做静态语法检查，出错时报告必须包含 Shader 名、该 permutation 的具体维度取值（通过 `domain.decode()` 人性化展示，而非裸 index）、以及出错行号
5. **全零覆盖检查**：`totalCount() > 0` 但 `ShouldCompile` 排除了全部组合 → 报错（通常意味着裁剪逻辑写反）
6. **变体数膨胀预警**：单个 Shader 编译后的变体数超过参考阈值（256，见 §5.4）→ 警告，提示考虑拆分维度或改用多 Pass
7. **构建报告**：所有检查结果写入 `build/generated/shader_build_report.json`，包含每个 Shader 的裸组合数、被排除数、实际编译数，供人工核对 §5 的变体规模估算
8. **Uniform 名长度校验**：GLSL 源码中声明的 uniform 名长度（UTF-8 字节数，含终止符共需 ≤ 31 字节）超过 §7.4 `UniformDesc.name[32]` 的容量 → 报错阻断，避免序列化时静默截断导致运行时按错误名字查找
9. **Processor 覆盖率检查**：扫描 `src/gpu/processors/` 下全部带 `DEFINE_PROCESSOR_CLASS_ID` 的叶子 FP/GP/XP 子类（不含 `XfermodeFragmentProcessor`/`ComposeFragmentProcessor` 等 §8.4 所述的容器节点，后者走递归分解而非逐条匹配），逐一核对 `EffectDecomposer` 的匹配表是否存在对应规则 → 缺失则报错阻断，防止新增 Processor 后遗漏匹配表更新（对应 §2.3.1 完备性论证第一层机制）

### 9.3 运行时检查

构建期已保证"注册的每个有效 Permutation 均编译成功"；在 §9.2 全部构建期检查通过、且下述第 2 条描述的 Bundle 版本校验通过的前提下，运行时不存在查表未命中的情况。仍需防御，因为上述前提在极端场景（如手工绕过构建步骤、Bundle 文件损坏）下可能不成立：

1. **`PipelineKey` 在 Bundle 中查不到**：说明 `EffectDecomposer` 产生了一个 `ShouldCompile` 已排除、或未在任何 Shader 中声明过的组合——这是逻辑错误。Debug 构建下用断言中止（不使用 C++ 异常，符合项目编码规范）；Release 构建退化为跳过绘制并记录日志，不使 App 崩溃
2. **Bundle 版本与二进制不匹配**：`FileHeader.sourceHash` 与当前源码不一致，说明 Bundle 未随源码同步重新构建。此时**没有可退到的运行时编译器**（消除运行时拼接是 §1.2 的设计目标，瘦身构建下 shaderc/spirv-cross/tint 均不在运行时二进制中，见 §8.5）；因此加载时直接拒绝该 Bundle：Debug 构建下断言中止并给出"Bundle 需重新构建"的明确提示，Release 构建下拒绝启动对应 backend 的渲染路径并记录致命日志。保留运行时编译器作为回退（fallback 到动态拼接）不是本方案的选项，因为这与消除运行时拼接的设计目标直接冲突；若确有需要，应作为独立于本方案的过渡期临时开关单独评审，本文档不预设该开关存在。

---

## 10. 附录

### 附录 A：与既往方案的决策差异

`docs/research/shader-precompile-master-plan.md`（v2.0）提出的方案核心是"全部改为多 Pass 分解 + shader 文件 `//!` 注释声明变体 + Python 构建脚本"。本文档在以下几点做出不同选择：

| 维度 | master-plan v2.0 | 本文档 |
|------|---|---|
| 变种声明位置 | shader 文件 `//!` 注释 | C++ 类声明（§6） |
| 解析方式 | Python 正则解析 GLSL 注释 | C++ 静态自注册，构建工具直接调用 C++ API |
| 架构基线 | 全部改为多 Pass，废弃单 Pass 拼接模型 | 单 Pass 为主，多 Pass 仅作复杂组合兜底（§2.3、§5.4） |
| 原子 Shader 数 | 固定集合（文档估算 ~359，估算方法见其 §5，本文档不重复验证该数字） | 按 §5 已注册 Shader 类的 Permutation 维度动态确定，不预设总数 |

**选择理由**：

1. C++ 类声明可获得编译期类型检查（如 `info()` 纯虚函数强制实现），`//!` 注释的正确性只能在构建期或运行时暴露；
2. 全面多 Pass 改造意味着放弃全部单 Pass 场景的现有性能特征，而 §5.4 的分析表明大多数 Level-1 场景（纹理/渐变/常量色填充等）的 Permutation 空间是可枚举、可控的，不需要以牺牲单 Pass 为代价换取"变体空间封闭"这一目标——两个目标（消除运行时拼接、控制变体规模）可以同时通过"Shader 类粒度的 Permutation"达成，不必绑定"废弃单 Pass"这一决策。

master-plan 中以下内容不因架构选型差异而失效，后续实现阶段仍应参考：

- §2 现有系统分析（Processor 体系、各后端编译链路）——已在本文档 §1.1、§7.2、§8.1 中重新引用相关结论
- §12 关键实现问题（ClassID 跨进程稳定性、纹理格式对 GLSL 文本的实际影响、`computeProcessorKey` 的 Context 参数）——这些是任何预编译方案都需要处理的问题，与单/多 Pass 选型无关
- §9 Bundle 格式的字段设计思路——本文档 §7.4 在其基础上简化裁剪

两份文档目前并存，尚未确认是否需要合并、废弃或保留其中一份作为背景材料；该决策留待与相关方评审后再执行，本文档不单方面声明另一份文档的状态。

### 附录 B：PipelineKey 完整定义

```cpp
struct PipelineKey {
  std::string shaderName;   // 对应 PrecompiledShaderInfo::name，如 "TextureFillShader"
  uint32_t permutationIndex;  // PermutationDomain::encode() 的结果
  uint8_t rtFormat;          // render target format
  uint8_t sampleCount;       // MSAA sample count
  uint8_t blendState;        // hardware blend 配置索引
  uint8_t cullMode;

  // Serializes into a BytesKey so the result can be consumed directly by the existing
  // GlobalCache::findProgram()/addProgram() (see §8.1), without introducing a parallel
  // cache keyed differently from the rest of the runtime.
  BytesKey toKey() const;
};
```

`shaderName` 在此定义中使用字符串而非既往草稿中的数值 `shaderID`：Shader 集合按 §5 是随开发迭代增长的开放列表，不是像 master-plan 中"~359 个固定原子 Shader"那样的预先编号封闭集合，字符串更符合"构建工具扫描注册表得到什么就是什么"的实际情况；性能敏感路径可在 `toKey()` 内部对字符串做 hash 后再写入 `BytesKey`，避免运行时字符串比较开销。

`outputSwizzle` 不作为独立字段：其决定的 fragment shader 输出 swizzle 表达式在构建期已固化进 GLSL 文本，属于 `shaderName + permutationIndex` 唯一确定的源码内容的一部分，不应作为可与任意 `shaderName` 自由组合的正交维度重复编码。

**§7.4 `IndexEntry.pipelineKeyHashHi/Lo` 的哈希算法**：`PipelineKey` → 128-bit hash 必须由构建工具与运行时产出完全一致的结果，否则二分查找永远不命中。做法是在 `PipelineKey`（`ShaderPermutation.h` 所在的共享层，与 §6.5 "运行时复用同一份 ShaderPermutation.h" 同一原则）中提供唯一实现 `PipelineKey::Hash128(shaderName, permutationIndex, rtFormat, sampleCount, blendState, cullMode) -> {hi, lo}`，构建工具与运行时链接同一份实现，一致性由"单一实现、单一链接目标"结构性保证，而非要求两端各自维护同一算法的独立实现。算法本身不引入外部哈希库（符合 C2 约束），采用对字段做确定性小端字节拼接后施加两轮不同种子的 FNV-1a 64-bit（分别产出 hi/lo）。需要说明：该构造本质是两个独立的 64-bit hash 拼接为 128-bit 值，而非原生 128-bit hash（如 CityHash128/MurmurHash3_x64_128）——其碰撞抗性受限于 64-bit 空间的生日界（约 2⁻³² 量级），在变体总数处于 10⁴ 量级时该碰撞概率仍可忽略，但不应将其表述为"128-bit 碰撞抗性"，以避免误导评审。`toKey()` 生成的 `BytesKey` 内部可直接复用 `Hash128` 的 `lo` 部分作为字符串 hash 的替代，避免维护两套不同的哈希逻辑。该实现与一致性要求纳入 §9.2 第 4 项之外新增的构建期自检：构建工具对每个已编译变体额外用运行时同款 API 反向计算一次 hash，与写入 `IndexEntry` 的值比对，不一致则报错阻断（防止未来实现演进时两端意外分叉）。

### 附录 C：术语表

| 术语 | 定义 |
|------|------|
| Processor | GP/FP/XP 的统称，着色器逻辑的最小功能单元 |
| Permutation | 一个 Shader 类在其声明的维度空间中的一个具体取值组合 |
| PermutationDomain | 若干 Permutation 维度的组合，提供 index ↔ 取值组合的双射编解码 |
| PrecompiledShader | 描述一个 Shader 的元数据（维度、裁剪规则、源文件路径）的 C++ 基类 |
| PipelineKey | 唯一决定一次绘制调用完整 Pipeline 状态的键：Shader 名 + Permutation index + 渲染状态 |
| EffectDecomposer | 运行时组件，将 Processor 树映射为一个或多个 `PipelineKey`（单 Pass 或多 Pass） |
| PrecompiledShaderCache | 构建期产物（Bundle）的运行时加载与查找组件，按 `Context` 持有独立实例（见 §8.2） |
| Bundle | 单个 backend/profile 对应的构建产物文件，包含 FileHeader/IndexEntry/Reflection Data/Shader Data 四部分（完整格式见 §7.4） |
| PrecompiledShaderBlob | `PrecompiledShaderCache::find()` 返回的单条查表结果，包含某个 Permutation 对应的 Shader 二进制/文本与 Reflection 布局 |
