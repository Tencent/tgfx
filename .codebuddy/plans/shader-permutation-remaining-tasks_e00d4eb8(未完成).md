---
name: shader-permutation-remaining-tasks
overview: 完成 shader permutation 系统的三个剩余任务：补全 3 个渐变 Colorizer Shader、添加 10 个 GeometryProcessor 级 Shader 声明、以及实现多 Pass 递归分解框架（EffectDecomposer）。
---

## 用户需求

为 tgfx 图形库的 Shader Permutation（预编译 shader）系统补全剩余的 Processor 覆盖、GP 级 shader 声明、以及多 Pass 递归分解框架。

## 产品概述

当前预编译 shader 系统已覆盖 14 个 Shader（896 个变体），能将运行时最常见的绘制路径从动态 GLSL 拼接替换为预编译查表。本次扩展三个方向，进一步提升覆盖率和复杂场景支持。

## 核心功能

1. **渐变 Colorizer 补全**：新增 SingleIntervalGradientShader、DualIntervalGradientShader、TextureGradientShader 三个预编译 shader，覆盖 2-stop、3/4-stop、16+ stop 渐变场景（当前只覆盖了 4-16 stop 的 UnrolledBinaryGradientColorizer）

2. **GeometryProcessor 级 Shader 声明**：为 EllipseGeometryProcessor、ShapeInstancedGeometryProcessor 等 10 个 GP 编写预编译 shader，覆盖椭圆、圆角矩形、Mesh、细线等绘制路径

3. **多 Pass 递归分解框架（EffectDecomposer）**：当单个 DrawOp 的 FP 树无法映射到预编译 shader 时（如 ComposeFragmentProcessor 嵌套），自动将其拆解为多个子 pass——每个子 pass 渲染到临时纹理，再由下一 pass 消费

## Tech Stack

- 语言：C++17
- GLSL：450
- 构建：CMake + Ninja
- 预编译 shader 框架：项目自有 PrecompiledShader / ShaderRegistry / PermutationMatcher 体系

## Implementation Approach

### 任务 1：渐变 Colorizer 补全

**策略**：沿用 GradientFillShader 的架构模式，为三种 colorizer 各建立独立的 PrecompiledShader 子类。它们与 GradientFillShader 共享同一个 vertex shader（gradient_fill.vert），但各自有不同的 fragment shader 逻辑。

**关键设计决策**：

- SingleInterval 和 DualInterval 的 colorize 逻辑非常简单（无条件编译分支），不需要额外的排列维度，只需 `LAYOUT_TYPE`(4 值)
- TextureGradient 使用纹理采样，也只需 `LAYOUT_TYPE`(4 值)，但需要声明一个额外的 texture sampler
- 三者的 vertex shader 与 GradientFillShader 完全相同（相同的 Matrix_P0 + CoordTransformMatrix），可以共享 `gradient_fill.vert`
- PermutationMatcher 扩展 `TryMatchGradientFill()` 函数，增加对三种 colorizer 的识别分支

**变体数**：SingleInterval 4 + DualInterval 4 + TextureGradient 4 = 12 个新变体

### 任务 2：GP 级 Shader 声明

**策略**：现有架构中 PrecompiledShaderInfo 已预留了 `gpDomain`、`gpClassName` 字段但未使用。本任务不修改框架核心，而是为每个 GP 创建**独立的组合 Shader**（GP + 最常见的 FP 组合为一个 Shader），因为不同 GP 有完全不同的 vertex attributes、varying 和 vertex shader 逻辑。

**分批优先级**（按使用频率）：

- **第一批（高频）**：ShapeInstancedDrawOp 使用的 ShapeInstancedGeometryProcessor、EllipseGeometryProcessor、NonAARRectGeometryProcessor
- **第二批（中频）**：RoundStrokeRectGeometryProcessor、ComplexEllipseGeometryProcessor、ComplexNonAARRectGeometryProcessor
- **第三批（低频）**：MeshGeometryProcessor、HairlineLineGeometryProcessor、HairlineQuadGeometryProcessor

**排列维度**（每个 GP shader）：

- GP 内部的 key 组成（如 `stroke` bool、`hasColor` bool）作为排列维度
- Fragment 侧维度最小化，因为 GP shader 主要配对 ConstColor 或简单纹理

### 任务 3：多 Pass 递归分解框架

**策略**：参考已有的 `GaussianBlurImage