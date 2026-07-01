# Analytic 裁剪 FP 覆盖率优化

## 1. 背景

### 问题描述

`OpsCompositor::applyClip(const ClipStack& clipStack)` 将每个裁剪元素落到三条通道之一：

1. **Scissor**：像素对齐的轴对齐矩形（不论 AA），或者未开启 AA 的轴对齐矩形，交由 scissor 硬裁剪
2. **Analytic FP**：开启 AA 的轴对齐矩形走 `AARectEffect`，在 fragment shader 里算平滑覆盖率
3. **Mask 纹理**：其余元素（非轴对齐矩形、任意圆角矩形、一般路径）统一走 `makeClipTexture` 光栅化到离屏纹理，再用 `DeviceSpaceTextureEffect` 采样

> **关于"轴对齐矩形"的判定**：由 `path.isRect()` 决定，实测接受**恒等 / 平移 / 正缩放 / 镜像 / 90° 倍数旋转**（只要变换后顶点仍轴对齐就返回 true）；**任意角度旋转、错切、透视**下返回 false。详见 `PathTest.IsRectUnderTransforms`。因此现状代码对"旋转 90°"、"镜像"这类特殊矩阵下的矩形已经命中 Scissor / Analytic FP 快路径，真正被迫走 mask 纹理的是任意角度旋转、错切、透视的矩形，以及所有非轴对齐的 RRect。

第三条通道开销显著：需要额外申请 RenderTarget、提交离屏 `OpsRenderTask`、对每个元素做一次 shape 光栅化，最终再采一次纹理。对于"几何解析可直接求覆盖率"的场景（旋转矩形、任意 RRect），生成纹理是浪费。

Skia 存在两个 GPU 后端 **Ganesh** 和 **Graphite**，它们处理裁剪区的策略不一样；当前 TGFX 的架构和 **Ganesh 更接近**，本方案以 Ganesh 为参考。Ganesh 对同类场景直接走 analytic FP：

- 轴对齐 RRect → `GrRRectEffect`（按 RRect 子类型分派到圆形或椭圆 FP，详见第 3.2 节）
- 带仿射变换的矩形 → `GrConvexPolyEffect`（用不超过 8 条边线性方程求覆盖率）
- 恒等变换矩形 → `GrFragmentProcessor::Rect`（与当前 `AARectEffect` 等价）

只要命中这几类快路径，就不需要离屏纹理。

> **补充说明：为何不参考 Graphite**
> Graphite 走的是**多层混合路线**：(1) 几何求交把 clip 折进 draw 自身形状（AA 由 draw renderer 处理）；(2) `AnalyticClip` FP（仅 identity + 简单形状），shader 输出 coverage；(3) `AtlasClip` 纹理（覆盖率图集），采样输出 coverage；(4) 兜底深度缓冲 depth-only draw（深度测试做整像素拒绝，AA 边在这条路径上有少量退化）。前三层 AA 正常生效，仅最后兜底路径会丢精度。这套方案依赖 `BoundsManager` 全局 painters order 分配 Z 值。
> TGFX 当前没有 painters order / 深度缓冲统一分配机制，引入 Graphite 路线需要改 DrawList 排序和 Z 值分配，工程量远超当前目标；且 Graphite 的 `AnalyticClip` 比 Ganesh 的 `GrRRectEffect` 砍掉了椭圆角 RRect / 凸多边形的 analytic 能力，直接复刻到 TGFX 反而丢失优化收益。**Graphite 的几何求交、atlas 和深度缓冲组合方案留作后续架构演进方向。**


### 需求

在 `applyClip` 中扩展 analytic FP 判定，让带任意仿射矩阵的矩形 / RRect 走 FP 而不是 mask 纹理。具体的 FP 拆分、shader 变体、退化处理等实现层细节见第 5 章。

**本期范围之外**（仍走 mask 纹理）：

- 带透视的矩形 / RRect。直接原因是 RRect 的圆角投影后参数变了——透视投影通常把椭圆映射为另一个椭圆（极端情况下若灭点穿过原椭圆边界还会退化为抛物线 / 双曲线），但新椭圆的方向 / 半径与原椭圆无固定关系，shader 里基于固定 `radii` 写的 `length(dxy / radii) - 1` 椭圆 SDF 公式不再适用，即使把 fragment 反变换回本地空间也救不回来（因为 fragment 的设备空间分布经过透视除法已经是非线性的）。矩形虽然在透视下仍是凸四边形（4 条线性边）可以解析处理，但相关 FP 移到了二期一并做。
- 一般路径（非 Rect / RRect）

### 后续优化可参考

- **凸多边形 FP（`AAConvexPolyEffect`）**：用线性边方程组表达任意凸多边形覆盖率，可承接"凸 path 裁剪"场景（如八边形、菱形、convex path）。本期不引入——当前 `RectEffect` 扩展后已能覆盖所有仿射变换矩形；凸多边形 path 在 Canvas API 里出现频率低，工程量与收益不匹配，作为二期独立任务。
- **矩形透视裁剪**：透视下矩形仍是凸四边形（4 条线性边），可以走 `AAConvexPolyEffect`（透视下 `gl_FragCoord` 是设备空间坐标，线性边方程仍成立）。依赖上一条 `AAConvexPolyEffect` 的实现，作为它的延伸用例。RRect 透视无法用同一思路（圆角投影后不再是椭圆，shader 的椭圆 SDF 不适用），始终走 mask 纹理。
- **大半径下的 FP16 精度修正**：椭圆 SDF 公式中 `1/r²` 在半径较大（如 r > 256）时落入 FP16 亚正常数区，精度从 0.05% 退化到几个百分点甚至完全归零；详见 3.2 节 EllipticalRRectEffect 段的 FP16 精度问题分析。Skia 的做法是引入 `scale` uniform 把 radii 归一化到 1 附近。TGFX 绘制路径 `EllipseGeometryProcessor` 当前也未做此修正——作为绘制 + 裁剪共同的二期方向。
- **项目内 tagged union 的编译期保护**：本方案在 5.1 节 ClipElement 的 union 后加了 `static_assert(std::is_trivially_destructible_v<...>)` 检查，防止未来 `Rect` / `RRect` 被加非 trivial 成员后析构跳过悄悄泄漏。项目里现有的 `OpaqueContext::Contour`（`src/layers/OpaqueContext.h:124`）和 `PDFUnion`（`src/pdf/PDFUnion.h:69`）都是同款 tagged union，但没有这层保护——建议同步补上同款 static_assert，避免类似的安全隐患。

### 术语定义

| 术语 | 含义 |
|------|------|
| Analytic FP | 在 fragment shader 内直接由几何参数计算覆盖率的 FragmentProcessor |
| Axis-aligned | 轴对齐（矩阵只含平移和非负缩放，不含旋转/错切/翻转引起的轴变化） |
| Convex Poly | 凸多边形，每条边用 `a*x + b*y + c >= 0` 表达 |
| RRect | 四角可独立半径的轴对齐圆角矩形（对应 `tgfx::RRect`） |
| FP16 | IEEE 754 binary16 半精度浮点（5 位指数 + 10 位尾数，normal 范围约 `[6×10⁻⁵, 6.5×10⁴]`）。移动端 GPU fragment shader 默认精度，相对精度约 0.05%；超出 normal 范围下限会落入亚正常数区，精度退化到几个百分点 |

---

## 2. 现有实现分析

### 2.1 ClipStack 数据流

`Canvas::clipPath` 在入栈前就把 `_matrix` 烘焙进 path：

```cpp
// src/core/Canvas.cpp:192
void Canvas::clipPath(const Path& path, bool antiAlias) {
  auto clipPath = path;
  clipPath.transform(_matrix);        // 矩阵直接作用在 path 上
  clipStack->clip(clipPath, antiAlias);
}
```

`ClipElement` 只保存 `Path _path`、`Rect _bounds`、`bool _isRect`（来自 `path.isRect(nullptr)`）：

```cpp
// src/core/ClipStack.h:110-116
Path _path = {};
bool _antiAlias = false;
Rect _bounds = {};
bool _isRect = false;
int _invalidatedByIndex = -1;
```

**关键结论**：矩阵已烘焙进 path，源几何与矩阵不再可分开访问。对于"任意角度旋转/错切/透视的矩形"和"所有非轴对齐矩阵下的 RRect"，`path.isRect` / `path.isRRect` 均返回 false，RRect 的圆角信息也从 path 结构里丢失。这两类元素目前被迫全部走 mask 纹理——正是本次要补齐的场景。

### 2.2 applyClip 现状

`src/gpu/OpsCompositor.cpp:729-786`：

```
Stage 3 元素分派：
  isRect && (pixelAligned || !AA)  → 跳过（scissor 覆盖）
  isRect && AA                     → makeAnalyticFP → AARectEffect
  其他                             → 收集到 elementsForMask
Stage 4:
  elementsForMask 非空 → getClipMaskFP（离屏光栅化）
```

`makeAnalyticFP` 内部注释已标注 "Future extension: RRectEffect, etc."，预留了扩展点。

### 2.3 现有 AARectEffect 着色器

`src/gpu/glsl/processors/GLSLAARectEffect.cpp:29-49`：

```glsl
vec4 dists4 = clamp(vec4(1,1,-1,-1) * (gl_FragCoord.xyxy - Rect), 0, 1);
vec2 dists2 = dists4.xy + dists4.zw - 1.0;
float coverage = dists2.x * dists2.y;
```

CPU 侧把矩形外扩 0.5 像素。基于 `gl_FragCoord`，是**设备空间**的 AA 距离场。

### 2.4 mask 纹理开销分析

`makeClipTexture` (`src/gpu/OpsCompositor.cpp:821-862`)：

- 申请 `width × height` 大小的 `RenderTargetProxy`（可能几百万像素）
- 为每个元素生成 `ShapeDrawOp`，走完整 tessellator 或 mask 光栅化管线
- 通过 `addOpsRenderTask` 异步提交到 drawing manager
- 消费端再通过 `DeviceSpaceTextureEffect` 采样

一次典型的"AA RRect 裁剪"目前走 mask 纹理，成本 ≥ 1 次离屏 pass + 1 次 shape 光栅化 + 1 次纹理采样。优化后降为 1 个 FP 计算。

---

## 3. 参考设计（Skia Ganesh）

### 3.1 全局分派

`src/gpu/ganesh/ClipStack.cpp` 的 `apply()` 主循环按六级优先级处理每个元素，前一级成功就不走后面：

| 优先级 | 通道 | 条件 |
|--------|------|------|
| 1 | scissor / window rectangles | 轴对齐 rect 且 NonAA（rasterizer 硬件路径）|
| 2 | Op 内部裁剪 | Op 支持自裁剪（`op->clipToShape()`），clip 融进 op 几何 |
| 3 | Analytic FP | 形状能被某个 effect 接收，且当前 FP 数 < 4 |
| 4 | Clip atlas | 复杂 path 能塞进共享 atlas，FP 采样 |
| 5 | Stencil mask | **通用兜底**——任意形状、任意叠加，落到 RT 自带 stencil attachment |
| 6 | SW mask | stencil 不可用时的最终兜底（CPU 光栅化 + 上传纹理）|

> **6 级 vs 顶层四档**：1-2 是硬件快路径，3-4 是 shader 内联快路径，5 是通用兜底，6 是最终兜底。每条快路径的形状 / AA 适用范围、成本结构、何时让位给 stencil 见 3.2 节。

核心决策函数 `analytic_clip_fp`（`ClipStack.cpp:256-277`）按以下顺序判定：

```
if (localToDevice 是 identity) {
    if (shape 是 rect)  → GrFragmentProcessor::Rect()
    if (shape 是 rrect) → GrRRectEffect::Make()
}
// 凸多边形分支允许带任意仿射变换：把 path 顶点 mapPoints 到设备空间后再生成边方程
if (shape 仅含直线段 && shape 是凸的) → GrConvexPolyEffect::Make(devicePath)
else → 失败（降级到 atlas / stencil / SW mask）   // 非 identity 的 RRect 含曲线段，会落到这里
```

每个元素独立判定；FP 链上限 4 个，超出的元素落到 atlas / stencil / SW mask。

#### 入栈归一化（`RawElement::simplify`）

Skia 在元素入栈时跑一次 `simplify`（`ClipStack.cpp:564`）：若 `localToDevice.preservesAxisAlignment()`（identity / 平移 / 缩放 / 镜像 / 90°倍数旋转），**直接把矩阵烘焙进 Rect / RRect 几何，`localToDevice` 重置为 identity**；否则保持原矩阵。

所以等到 `analytic_clip_fp` 跑的时候，**"isIdentity 的 RRect" 已经覆盖了所有 `preservesAxisAlignment` 矩阵下的 RRect**——非 identity 的 RRect 一定是真正的旋转/错切矩阵，`SkRRect::transform` 无法把它折叠成新 RRect，只能降级 mask。这是 Skia 选择"RRect + 非 identity 就 fail"的根本原因。

#### 几何级合并（`RawElement::combine` + `SkRRectPriv::ConservativeIntersect`）

入栈 simplify 后，Skia 还会尝试把新元素与栈顶元素求交合并为单个元素（`ClipStack.cpp:648`）。触发条件：两元素 `localToDevice` 相同、AA 一致、形状是 Rect / RRect。

合并算法 `ConservativeIntersect`（`SkRRect.cpp:882-993`）按四角独立判定：每角根据交集角点是来自 a / b / 两者重合 / 新生成，决定该角半径；半径互相交叉或 `scaleRadii` 失败时合并失败、保留两个元素。

入栈归一化把多数 stayrect 矩阵下的元素归一为 identity，使"localToDevice 相同"这一条天然命中——几何合并因此频繁触发，是 Skia analytic 路径减少 FP 数量的关键设计。

### 3.2 关键 FP

#### 设计哲学：通用兜底 + 多条收益明确的快路径

Ganesh 的 clip 体系不是"为每种形状找最优路径"，而是 **"一条通用兜底 + 多条收益明确的快路径"**：stencil mask 是唯一覆盖任意形状 / 任意叠加 / intersect 与 difference 的通用实现，scissor / FP / atlas / SW mask 都是针对特定形状或场景的快速旁路，旁路接不下就 fallback 回 stencil。新增快路径的标准是：

```
专用路径净收益 = (性能差距 × 触发频率) - (shader 变体成本 + 维护成本 + program cache 压力)
```

只有净收益为正才写。这条原则直接解释了 `GrRRectEffect` 为什么只接 AA、不接 NonAA（详见后文成本分析）。

#### 完整选路（六级优先级）

`ClipStack::apply` 对每个 element 按下列顺序尝试，前面成功就不走后面（`ClipStack.cpp:Apply`）：

| 级别 | 通道 | 适用形状 | AA 要求 | 矩阵要求 |
|------|------|---------|--------|---------|
| 1 | **scissor** | 轴对齐 rect | NonAA | 任意（preservesAxisAlignment）|
| 2 | **op inline clip** | op 能吃下的形状 | 任意 | 任意 |
| 3 | **analytic FP** | rect / oval / RRect / 凸多边形 | 看 effect | 看 effect |
| 4 | **clip atlas** | 复杂 path（能塞进 atlas）| 任意 | 任意 |
| 5 | **stencil mask** | **任意（通用兜底）** | 任意 | 任意 |
| 6 | **SW mask** | 任意（最后兜底，stencil 不可用时）| 任意 | 任意 |

每条 FP 各自决定接不接 BW edge type：

- `GrFragmentProcessor::Rect` / `GrConvexPolyEffect`：AA / BW 都接
- `CircularRRectEffect` / `EllipticalRRectEffect`：**只接 AA**，BW 直接 `GrFPFailure` → fallback 到 stencil（`GrRRectEffect.cpp:98、435`）

**关键认识**：stencil 是通用兜底，所有形状（rect / RRect / Path）、所有 AA 状态都能落到 stencil。FP 不是 stencil 的"AA 平替"，而是**针对单 draw 简单形状的 shader 内联优化**——FP 把 clip 计算融进内容 draw 的 fragment shader，省掉一次 stencil mask draw，但代价是 clip 不能跨 draw 摊销。

#### 成本结构

| 路径 | 固定开销 | per-fragment 开销 | 摊销特性 | 最适场景 |
|------|---------|------------------|---------|---------|
| scissor | ≈ 0 | 0 | — | 轴对齐 NonAA rect |
| op inline | 0（融进内容 draw）| clip ALU 计入内容 FP | — | op 几何能吃下的简单形状 |
| analytic FP | 0（融进内容 draw）| clip ALU per fragment | **不能跨 draw 摊销** | 单次小 draw / 简单形状 / AA 曲线 |
| atlas | 一次 mask 绘制 + atlas 采样 | atlas 采样 + filter | 同 atlas 多次复用 | 复杂 path 能塞进 atlas |
| stencil | 一次 stencil draw + stencil 写入带宽 | per-sample stencil compare（ROP 接近免费）+ FP 可被 early-stencil 跳过 | **一次 mask 摊到多次内容 draw** | 通用，特别是覆盖像素多 / 多次复用 |
| SW mask | CPU 光栅化 + 纹理上传 | 纹理采样 | 中 | stencil 也不合适时兜底 |

**stencil 比 FP 便宜的三条机制**（解释 NonAA RRect 为什么走 stencil）：

1. **early-stencil 自动开启**：stencil mask 写完后，内容 draw 启动硬件 early-stencil 测试，外部 fragment 在 FP 之前就被剔除，**连内容 paint 的 shading 都省**。这是默认行为，无需任何 API 调用——只要内容 FP 不 discard / 不写 depth / 不改 sample mask
2. **stencil compare 走 ROP 专用电路**：per-sample 1 cycle 级，**不占 shader core 资源**；FP 内联的 clip 计算占用通用 ALU
3. **一次 mask 摊到多次 draw**：clip 被多个内容 draw 共用时，stencil 的固定开销 `C_s` 被摊薄

总账：

- `Stencil ≈ C_s + K · N · C_cmp ≈ C_s`（C_cmp ≈ 0）
- `FP ≈ K · N · C_fp`
- `K` = 同 mask 复用次数，`N` = 覆盖 fragment 数，`C_fp` = per-fragment clip FP ALU

`K · N` 大（clip 被多 draw 共用 / 覆盖像素多）+ `C_s` 低（NonAA RRect 的 stencil draw 是简单几何）→ stencil 胜。

**为什么 AA RRect 反而走 FP**：AA 下 stencil 路径需要 AA fill 扩边三角化 + 多 pass coverage 写入，`C_s` 大幅上升；FP 路径 `C_fp` 不变（一次 `clamp(0.5 - dist, 0, 1)` 内联）。`C_s_AA > K · N · C_fp_AA` 时 FP 胜出。**同一形状（RRect）的 AA / NonAA 选路方向相反，原因不在 AA 标签本身，而在两边 stencil 路径的成本结构不同。**

#### 把工作放在合适的硬件单元上

Ganesh 在选路上反复体现一个偏好：**能交给固定功能硬件做的，就不要塞给 shader core**：

- 轴对齐 NonAA rect → scissor（rasterizer 硬件路径，几乎零成本）
- 通用 clip → stencil（ROP 专用 stencil compare，不占 shader core）
- 只有曲线 AA 这种 ROP / rasterizer 给不出正确结果的需求，才挪到 FP（shader ALU）做解析

选路优先级与 GPU 各单元的吞吐量层级一致（rasterizer > ROP > 通用 ALU > 显存往返）。

#### 关键代码位置

- `ClipStack.cpp:Apply` — 六级选路主循环
- `GrRRectEffect.cpp:98、435` — RRectEffect Make 拒绝 BW edge type
- `ClipStack.cpp:1519-1544` — stencil vs SW mask 二级分流（stencil 不可用或单采样需要 AA 时才走 SW）
- `StencilMaskHelper.cpp:491-501` — RRect 在 stencil 路径里 `shape.asPath()` 退化为 path 再光栅化
- `GrSWMaskHelper.cpp:84-118` — RRect 在 SW mask 路径里走 `fDraw.drawRRect`

#### 对 TGFX 的启示

TGFX **没有 stencil 裁剪通道也没有 atlas 通道**——不可解析形状无论 AA / NonAA 都只能落到 SW mask（`makeClipTexture`）。Skia 的"NonAA RRect 走 stencil"在 TGFX 没有可让位的对象，备选仅 SW mask（一次纹理分配 + 一次 CPU 光栅化 + 一次 driver 上传 + 一次 GPU 采样），比 FP shader SDF 贵很多。

权衡：

- **Skia 选项**：NonAA RRect → stencil（用 early-stencil + ROP + 多 draw 摊销，比 FP 划算），AA RRect → FP（AA stencil 的 `C_s` 翻倍，FP 反胜）。FP 集合保持只服 AA，shader 变体数减半
- **TGFX 选项**：NonAA / AA RRect 都走 FP（备选 SW mask 太贵，工程复杂度可控）。这是**没有 stencil 通道情况下的次优兜底**，不是普世最优——若未来补上 stencil 裁剪通道，NonAA 应改回走 stencil

> **TGFX 后续优化空间**：补 stencil 裁剪通道是显著收益的工程方向——early-stencil 自动剔除 + ROP 专用电路 + 多 draw 摊销三条机制叠加，常见 UI 场景（同 layer 多次内容 draw 共用 clip）下 stencil 比 FP 内联便宜很多。但工程量较大（涉及 RT stencil attachment 配置、ClipStack apply 阶段的二级分流、与本方案 FP 路径的协同），作为后续独立任务。本方案的 FP 路径在补 stencil 后仍是 AA 场景的快路径，不会被推翻。

这是本方案 `RRectEffect` / `RectEffect` 都同时支持 AA / NonAA 行为的根本依据，差异源于下游通道结构（缺 stencil / atlas）不同，不是设计缺陷。

#### AA 标签的层次：每条 effect 内部决定

Ganesh **不在选路顶层统一处理 AA**——`fForceAA` 只改 element 的 AA 标签，FP / atlas / stencil 是否接受某个标签是每条 effect 的内部决定（如 `GrRRectEffect` 拒收 BW，`GrFragmentProcessor::Rect` 全接）。AA 标签变化间接改变某些 effect 是否接收（典型例子：MSAA RT 上 `forceAA` 把 RRect 升级 AA → `GrRRectEffect` 接住 → 从 stencil 拉到 FP），但顶层选路框架不变。

本方案的 `RRectEffect` / `RectEffect` 同时支持 AA / NonAA 行为，是**单 effect 内部的设计选择**（用 `antiAlias` uniform 在 shader 内动态切换 coverage 公式，不增加 program 变体数），与 Skia 顶层选路无冲突——只是每条 effect 自己的策略。

#### 旁注：MSAA / DMSAA 下的 forceAA 升级

Skia 在 `RawElement::simplify`（`ClipStack.cpp:594`）有一条上游升级规则：当目标 RT 是 MSAA 或 DMSAA 时，把所有非轴对齐的元素 AA 标签强制升为 `kYes`：

```cpp
if (forceAA && !(fShape.isRect() && fLocalToDevice.preservesAxisAlignment())) {
    fAA = GrAA::kYes;
}
```

这个升级发生在选路之前，作用是把 stack 内部所有元素折叠到单一 AA 维度，避免 mixed-AA 状态下每个判定（包含关系、bounds 计算、op inline clip、与 draw 的 AA 一致性）都要带 mixedAA 分支。注释原话 "keeps the entire stack as simple as possible" 是软件复杂度理由。

理论上"统一"有两个方向，**只能选 AA**：

1. **统一成 AA**：曲线段走解析 FP，配合 MSAA resolve 得到真曲线软边；直边由 rasterizer per-sample coverage + MSAA 给软边；scissor 兼容的轴对齐 rect 例外保留 NonAA 标签以保住 scissor 优化。
2. **统一成 NonAA**：直边在 MSAA 下尚可（rasterizer per-sample 等价于 AA fill 的软边），但 RRect 圆角 / Path 曲线在 NonAA 路径下会被预折线化（每段 ~1 像素的折线扇）。折线本身是一阶不连续几何，亚像素精度上限就是折线精度，MSAA 的 4× / 8× 亚像素采样救不回几何输入的精度损失，圆角必然出折线锯齿。

**对 TGFX 的影响**：当前 TGFX 没有 MSAA 裁剪栈也没有 mixed-AA 复杂度问题（每个 ClipElement 直接保留用户传入的 AA 标志），不需要这条上游升级。本方案 `RRectEffect` / `RectEffect` 各自支持 AA / NonAA 两种行为即可，AA 标签作为 uniform 在 shader 内动态切换 coverage 公式（不进 ProcessorKey，不增加 program 变体数）。

#### 关键 FP 概览

> **裁剪 FP 与绘制 GP 是两套独立实现**：本节列出的 FP 都是 `GrFragmentProcessor`，作为 coverage 修饰挂在宿主 draw 的 paint FP 链里，依附宿主的 `gl_FragCoord` 算"该像素是否在裁剪区内"，不产生几何。同样形状用于绘制时走的是 `GrGeometryProcessor` + 专属 op，自己产顶点并画几何。两套抽象层不同，代码不复用。
>
> 以椭圆角 RRect 为例：
> - 裁剪用 `EllipticalRRectEffect`（`src/gpu/ganesh/effects/GrRRectEffect.cpp`，FP）
> - 绘制用 `EllipticalRRectOp` + `EllipseGeometryProcessor`（`src/gpu/ganesh/ops/GrOvalOpFactory.cpp`，op + GP）

#### GrFragmentProcessor::Rect

恒等变换矩形，shader 与 TGFX `AARectEffect` 一致（距离场 saturate）。

#### GrRRectEffect

轴对齐 RRect 的入口。核心是按 SkRRect 子类型把 RRect 分派到两个具体 FP：`CircularRRectEffect` 和 `EllipticalRRectEffect`。`kRect` / `kOval` 会被转交给独立的 `GrFragmentProcessor::Rect` / `GrOvalEffect`（位于其他文件），不在本入口产出。

##### SkRRect 子类型分派（代码入口 `GrRRectEffect::Make`）

| SkRRect 子类型 | 特征 | 走向 |
|---------------|------|------|
| `kRect` | 无圆角 | 转交 `GrFragmentProcessor::Rect`（独立 FP） |
| `kOval` | 整体椭圆（半径 = 半宽/半高） | 转交 `GrOvalEffect`（独立 FP） |
| `kSimple` | 四角半径全部相同 | 若 `rx == ry` → `CircularRRectEffect(kAll)`；否则 → `EllipticalRRectEffect` |
| `kNinePatch` | 左右/上下对称（两对半径） | 若所有非零角半径相等且为圆形 → `CircularRRectEffect`；否则若四角半径 ≥ 0.5 → `EllipticalRRectEffect` |
| `kComplex` | 四角可各异 | 仅当每角形态满足"完全直角 / 半径相同的圆形角 / 被 squash 的小角"之一，且 `cornerFlags` 命中 9 种支持组合时走 `CircularRRectEffect`；否则一律 fail |

**`kComplex` 走 CircularRRectEffect 的具体条件**：每个角必须是下列之一——(1) 完全直角 `(0, 0)`；(2) 半径 < 0.5 像素被 squash 成直角；(3) 圆形角（`rx == ry`）且**所有圆形角共用同一个半径**。满足条件后，`cornerFlags` 必须命中以下 9 种组合之一才能进 shader：

| `cornerFlags` 名 | 圆形角 | 直角 | 形状直观 |
|------------------|--------|------|----------|
| `kAll` | 全 4 角 | 无 | 退化为 Simple，已被前面分支处理 |
| `kTopLeft` / `kTopRight` / `kBottomLeft` / `kBottomRight` | 任一单角 | 其余 3 角 | 单 tab 形 |
| `kLeft` | 左上 + 左下 | 右上 + 右下 | 左侧圆右侧直角的"D 形" |
| `kTop` | 左上 + 右上 | 左下 + 右下 | 上方圆下方直角 |
| `kRight` | 右上 + 右下 | 左上 + 左下 | 右侧圆左侧直角 |
| `kBottom` | 左下 + 右下 | 左上 + 右上 | 下方圆上方直角 |

**不支持**：对角圆 + 对角直角（左上 + 右下圆，右上 + 左下直角），任一角是椭圆（`rx != ry` 且都不为 0），多个圆形角半径不同——这些情况一律 fail 到 mask。

> **TGFX 后续优化空间**：当前 TGFX 的**绘制路径**对上述"D 形 / tab 形 / 单边圆"也走的是 path 退化（`Canvas::drawRRect` 走 `UseDrawPath` 退化到 path），没复用 `EllipseGeometryProcessor` 的 shader。Skia 这套"按 cornerFlags 分派"的思路对绘制和裁剪都适用——在裁剪侧实施代价低（`makeAnalyticFP` 加一层 cornerFlags 检测，把符合条件的 Complex 转发到 `RRectEffect`），绘制侧改造成本更高但同样可行。本期暂不做，作为绘制 + 裁剪共同的二期优化方向记录。

##### CircularRRectEffect（圆形角）

适用场景：**若干角是相同半径的圆形角**，其余是直角。支持 9 种 `cornerFlags` 组合（单角 / 一条边的两个相邻角 / 全部四角）——覆盖了移动端常见的 "tab 形"、"卡片顶部圆底直" 等布局。

Shader 用九宫格策略：

```glsl
// 关键 uniform：innerRect（内缩后的矩形）、radiusPlusHalf = (r + 0.5, 1/(r + 0.5))
// 对有圆角的象限：算到内矩形角点的距离
vec2 dxy0 = innerRect.xy - gl_FragCoord.xy;   // 左/上方向正
vec2 dxy1 = gl_FragCoord.xy - innerRect.zw;   // 右/下方向正
vec2 dxy = max(max(dxy0, dxy1), 0.0);
half alpha = clamp(radiusPlusHalf.x - length(dxy), 0.0, 1.0);
// 对直角一侧：叠加线性 alpha 实现边界过渡
```

##### EllipticalRRectEffect（椭圆角）

适用场景：**kSimple 椭圆角**（四角相同但 `rx ≠ ry`）或 **kNinePatch**（左右/上下对称的椭圆角，上下半径可不同）。**不支持 kComplex 四角椭圆**。

Shader 用椭圆隐函数近似有向距离：

```glsl
// 关键 uniform：innerRect、invRadiiSqd = (1/rx², 1/ry²)（kSimple 用 vec2，kNinePatch 用 vec4）
vec2 Z = dxy * invRadiiSqd;
half implicit = dot(Z, dxy) - 1.0;                // 椭圆隐函数
half gradDot  = max(4.0 * dot(Z, Z), 1e-4);       // 梯度平方
half dist     = implicit * inversesqrt(gradDot);  // 近似像素距离
half alpha    = clamp(0.5 - dist, 0.0, 1.0);
```

FP16 设备或极大半径时启用一个 `scale` uniform 做归一化防止精度丢失。

> **FP16 精度问题的本质**：移动端 GPU 的 fragment shader 默认浮点精度往往是 16 位（half / mediump）。IEEE 754 binary16 normal 数下限是 `2⁻¹⁴ ≈ 6×10⁻⁵`，比这小的数落到亚正常数区，相对精度从 0.05% 退化到几个百分点；超过 normal 上限 `65504` 则直接溢出为 +inf。
>
> 椭圆 SDF 中 `invRadiiSqd = 1/r²` 与半径平方反相关——半径越大，`1/r²` 越接近 0，**越深入亚正常数区**。例如：
> - `r = 64`：`1/r² ≈ 2.4×10⁻⁴`（normal，精度 0.05%）
> - `r = 1024`：`1/r² ≈ 1×10⁻⁶`（亚正常数，精度约 6%）
>
> 在大半径下，FP16 的量化步长 `2⁻²⁴` 把相邻几十像素半径对应的 `1/r²` 塌缩到同一编码点。后续 `Z = dxy × invRadiiSqd`、`dot(Z, Z)`、`inversesqrt(gradDot)` 一路链式放大这个误差，最终 AA 边可能偏离 0.5~1 像素甚至崩溃。Skia 的 `scale` uniform 把 radii 整体除以 `max(rx, ry)` 拉到 1 附近，让所有中间量留在 normal 区，规避这个问题。

> **TGFX 这边的 FP 拆分对照**：Skia 按 SkRRect 子类型（kRect / kOval / kSimple / kNinePatch / kComplex）做最细的 5 路分派；TGFX 本方案做 2 路分派——`RRect::Type::Rect` → `RectEffect`，其余（Oval / Simple / Complex） → `RRectEffect`。`RRectEffect` 的 shader 用 arc box 分派 + safeRadii 半径保护，自然兼容三种 RRect 形态——Simple / Oval 时 4 个 radii 退化为相同值，Complex 时各角独立判定。这种合并比 Skia 更简洁，因为 FP 没有顶点 quad 自相交问题，不必为退化场景拆出独立分支。

#### GrConvexPolyEffect

接收一组线性边方程（最多 8 条），每条表达为 `(a, b, c)`，满足 `a² + b² = 1`（归一化法线）。shader 内对每条边计算 `signed distance = a*x + b*y + c`，取所有边的最小距离做 AA saturate：

```
float distance = minAllEdges(a * gl_FragCoord.x + b * gl_FragCoord.y + c);
float coverage = clamp(distance + 0.5, 0, 1);
```

适用于：
- 仿射变换后的矩形（4 条边）
- 任意凸多边形（例如 RRect 的直边部分 + 切角近似，但 Skia 仅对纯凸多边形使用）

---

## 4. 对比分析

按场景维度对比 Skia / TGFX 现状 / 本方案目标三档处理方式。

**机制层差异说明**

部分差异不属于具体场景而是机制层抽象，先在此简短列出，便于阅读下表时理解 TGFX 现状为什么大量场景走 mask：

- **ClipElement 保存源几何**：Skia 保留 SkRRect + 矩阵分离；TGFX 现状只存烘焙后的 Path——所以即使原本是 RRect，入栈后也无法识别
- **Canvas 入栈入口**：Skia 的 `clipRect` / `clipRRect` / `clipPath` 三入口各自保留 shape 语义；TGFX `clipPath` 把所有形状（含 rect / RRect）压成 Path 入栈，丢失类型
- **入栈归一化**：Skia 在入栈时把 `preservesAxisAlignment` 矩阵烘焙进几何（`RawElement::simplify`），重置 `localToDevice = identity`；TGFX 现状无此机制
- **几何级合并**：Skia 在 identity 矩阵下对 rect / rrect 做 `ConservativeIntersect` 求交合并；TGFX 现状只有基于 path 等价性的兜底合并，几乎无效
- **Analytic FP 上限**：Skia 4 个；TGFX 现状无显式上限（但只有 `AARectEffect` 一种 FP 可走）

本方案的设计同时补齐前四条机制层差异（详见 5.1 节 ClipElement / 5.2 节合并）；FP 上限按 Skia 同款设为 4。

**场景通道对比**

> 表中"轴对齐 / 仿射"按**源空间形态**描述场景，便于和现状对比；本方案 5.1 节 simplify 把 `preservesAxisAlignment` 矩阵（含 90° 倍数旋转、镜像）烘焙进几何并重置 `_matrix = identity`，所以代码运行时实际走 identity 还是 transform 变体由"simplify 后的 `_matrix` 是否 identity"决定——多数"轴对齐"场景命中 identity 变体，"任意仿射（旋转 / 错切）"才命中 transform 变体。

| 场景 | Skia | TGFX 现状 | 本方案目标 |
|------|------|-----------|----------|
| 像素对齐矩形（AA）| `GrFragmentProcessor::Rect`（FP 输出全 1）| scissor | scissor |
| 像素对齐矩形（NonAA）| scissor | scissor | scissor |
| 非 AA 轴对齐矩形 | scissor | scissor | scissor |
| AA 轴对齐矩形 | `GrFragmentProcessor::Rect` | `AARectEffect` | `RectEffect`（identity 路径 + AA uniform）|
| AA 任意仿射矩形（旋转 / 错切 / 非均匀缩放）| `GrConvexPolyEffect`（凸多边形通用解法）| mask 纹理 | `RectEffect`（transform 路径 + AA uniform）|
| 非 AA 任意仿射矩形 | `GrConvexPolyEffect` | mask 纹理 | `RectEffect`（transform 路径 + NonAA uniform）|
| AA 轴对齐 RRect（Oval / Simple）| `GrRRectEffect` 系（按 SkRRect 子类型分派）| mask 纹理 | `RRectEffect`（identity 路径 + AA uniform）|
| AA 任意仿射 RRect（Oval / Simple）| mask 纹理（`GrRRectEffect::Make` 在非 identity 下 fail）| mask 纹理 | `RRectEffect`（transform 路径 + AA uniform）|
| 非 AA RRect（Oval / Simple，identity 或仿射）| mask 纹理 | mask 纹理 | `RRectEffect`（NonAA uniform）|
| AA Complex RRect（identity）| `CircularRRectEffect`（仅特定 cornerFlags 形态）/ 否则 mask | mask 纹理 | `RRectEffect`（identity 路径 + AA uniform）|
| AA Complex RRect（仿射）| mask 纹理 | mask 纹理 | `RRectEffect`（transform 路径 + AA uniform）|
| 非 AA Complex RRect（identity 或仿射）| mask 纹理 | mask 纹理 | `RRectEffect`（NonAA uniform）|
| Simple RRect 半径 < 0.5 设备像素 | 退化为 Rect FP（`GrFragmentProcessor::Rect`） | mask 纹理 | `RRectEffect`（shader 内 `safeRadii = max(r, 0.5)` 自动按直角处理）|
| Complex RRect 含直角 / 半径 < 0.5 / 对角圆心盒重叠 | 半径 < 0.5 的角 squash 为直角，仍走 `CircularRRectEffect`（仅当 cornerFlags 命中支持组合，否则 mask）| mask 纹理 | `RRectEffect`（shader 内 arc box 分派 + safeRadii 自动处理，无需降级）|
| 带透视矩形 / RRect | mask 纹理 | mask 纹理 | mask 纹理 |
| 一般路径 | mask 纹理 / stencil mask | mask 纹理 | mask 纹理 |

> **从"现状"到"目标"省掉了什么**：mask 通道的离屏 pass 由所有走 mask 的元素共享，不是每个元素一个 pass。一个元素从 mask 挪到 FP 通道，省的是"在那个共享 pass 里它自己那一次 shape 光栅化 + 一份 coverage 写入"。当某个 frame 里所有原本走 mask 的裁剪元素都被 FP 接走时，整个离屏 pass 才能被消除（节省 1 次 RenderTarget 申请 + 1 次纹理采样）。
>
> 此外入栈归一化和几何级合并能减少 FP 数量本身（多个连续裁剪合并成一个），收益独立于上表的"通道分类"维度。

---

---

## 5. 重构/实现方案

### 5.1 ClipElement 扩展

为了在 `applyClip` 拿到"源几何 + 矩阵"而非烘焙后的 path，`ClipElement` 增加分类和源几何字段。源几何用 tagged union 共享存储——参考项目内现成的 `OpaqueContext::Contour`（`src/layers/OpaqueContext.h:83-144`）和 `PDFUnion`（`src/pdf/PDFUnion.h`）。

```cpp
class ClipElement {
 public:
  enum class Type {
    Rect,     // 矩形（源空间，本地坐标系）
    RRect,    // RRect（源空间，本地坐标系），具体子类型保留在 RRect::type()
    Path      // 一般路径（device-space）
  };

  ClipElement(const Rect& rect, const Matrix& matrix, bool antiAlias);
  ClipElement(const RRect& rRect, const Matrix& matrix, bool antiAlias);
  ClipElement(const Path& path, bool antiAlias);

  // union 含非 trivial 成员（Path），需要显式声明拷贝 / 析构 / 赋值
  ClipElement(const ClipElement& other);
  ClipElement& operator=(const ClipElement& other);
  ~ClipElement();

  Type type() const;
  // Valid when type() == Type::Rect.
  const Rect& rect() const;
  // Valid when type() == Type::RRect.
  // RRect::type() 区分 Rect / Oval / Simple / Complex，由 makeAnalyticFP 端按 type 分派到具体 FP。
  const RRect& rRect() const;
  // Valid when type() == Type::Path.
  const Path& path() const;
  // 把源几何（Rect / RRect）映射到设备空间。Type::Path 时为 identity（path 已是设备空间）。
  // 构造和 transform 时会内部自动归一化：preservesAxisAlignment 矩阵下把矩阵烘焙
  // 进几何、`_matrix` 重置为 identity（参考 Skia ClipStack::RawElement::simplify）。
  const Matrix& matrix() const;

  const Rect& bounds() const;            // device-space bounds
  bool isAntiAlias() const;
  bool isValid() const;
  bool isPixelAligned() const;

  // ... 其余接口保持不变
 private:
  // 入栈归一化：preservesAxisAlignment 矩阵下把 _matrix 烘焙进几何。
  // 由构造函数和 transform() 内部自动调用，对外不可见。
  void simplify();

  // tagged union：三种 shape 共享存储。访问前必须用 _type 判定当前激活成员。
  // 参考 OpaqueContext::Contour 的实现模板。
  union {
    Rect _rect;
    RRect _rRect;
    Path _path;
  };
  Type _type = Type::Path;
  Matrix _matrix = Matrix::I();
  Rect _bounds = {};
  bool _antiAlias = false;
  // 现状字段：当后续 clip 元素完全覆盖该元素时，记录覆盖者 index 以便标记失效
  int _invalidatedByIndex = -1;

  // 编译期保护：Rect / RRect 必须保持 trivially destructible，否则
  // 析构 / operator= 里跳过它们的析构会导致资源泄漏。未来若给这两个类加非
  // trivial 成员（如 std::vector / shared_ptr），编译器会在此处报错，
  // 提示同步更新 ~ClipElement() / operator= 等 union 管理代码。
  static_assert(std::is_trivially_destructible_v<Rect>,
                "Rect must remain trivially destructible; "
                "otherwise ~ClipElement() must call ~Rect() explicitly");
  static_assert(std::is_trivially_destructible_v<RRect>,
                "RRect must remain trivially destructible; "
                "otherwise ~ClipElement() must call ~RRect() explicitly");
};
```

**union 成员的生命周期管理**（参考 `OpaqueContext::Contour` 的 `~Contour()` / `operator=`）：

```cpp
// 构造时通过初始化列表激活对应 union 成员（编译器自动 placement new），
// 然后立即归一化矩阵。参考 OpaqueContext::Contour 同款风格。
ClipElement::ClipElement(const Rect& rect, const Matrix& matrix, bool antiAlias)
    : _rect(rect), _type(Type::Rect), _matrix(matrix), _antiAlias(antiAlias) {
  // ... 计算 _bounds 等
  simplify();          // preservesAxisAlignment 矩阵下烘焙进 _rect、_matrix 重置为 identity
}

ClipElement::ClipElement(const RRect& rRect, const Matrix& matrix, bool antiAlias)
    : _rRect(rRect), _type(Type::RRect), _matrix(matrix), _antiAlias(antiAlias) {
  // ...
  simplify();
}

ClipElement::ClipElement(const Path& path, bool antiAlias)
    : _path(path), _type(Type::Path), _antiAlias(antiAlias) {
  // Path 类型不需要归一化（_matrix 永远是 identity）
}

// 析构时按 _type 显式析构非 trivial 成员（Rect / RRect 是 trivial 类型，无需显式析构）
ClipElement::~ClipElement() {
  if (_type == Type::Path) {
    _path.~Path();
  }
}

// 拷贝赋值：先析构当前激活成员，再 placement new 新成员
ClipElement& ClipElement::operator=(const ClipElement& other) {
  if (this == &other) return *this;
  if (_type == Type::Path) {
    _path.~Path();
  }
  _type = other._type;
  _matrix = other._matrix;
  _bounds = other._bounds;
  _antiAlias = other._antiAlias;
  _invalidatedByIndex = other._invalidatedByIndex;
  switch (_type) {
    case Type::Rect:  new (&_rect)  Rect(other._rect);   break;
    case Type::RRect: new (&_rRect) RRect(other._rRect); break;
    case Type::Path:  new (&_path)  Path(other._path);   break;
  }
  return *this;
}
```

**ClipElement::simplify 实现要点**

```cpp
void ClipElement::simplify() {
  if (_type == Type::Path) return;
  if (!_matrix.rectStaysRect()) return;            // 非 stayrect 保留原矩阵

  if (_type == Type::Rect) {
    _rect = _matrix.mapRect(_rect);
    _matrix.setIdentity();
  } else {
    // _type == Type::RRect
    // RRect::makeTransform 处理 preservesAxisAlignment 矩阵下的几何 + radii 重排
    // （含 90° 倍数旋转、镜像，对应 Skia SkRRect::transform 的算法）
    auto rr = _rRect.makeTransform(_matrix);
    if (rr.has_value()) {
      _matrix.setIdentity();
      // 退化检查：变换后若 RRect 已无圆角，reshape 到 Type::Rect
      if (rr->type() == RRect::Type::Rect) {
        Rect deviceRect = rr->rect();
        _rRect.~RRect();              // 显式析构旧 union 成员（即便 trivial 也保持模板一致）
        new (&_rect) Rect(deviceRect);
        _type = Type::Rect;
      } else {
        _rRect = *rr;                 // 仍是 RRect，直接赋值
      }
    }
    // makeTransform 返回 nullopt 的场景：mapRect 后 rect 为空或非有限。
    // 此时保留原 _matrix，让 FP 走 transform 路径（CPU 端 ApplyScales 分解）。
  }
  // _bounds 不变（已经是 device-space）
}
```

`Canvas::clipRect` / `Canvas::clipPath` 的对应改造：

> **关于 `path.isRect` / `path.isRRect`**：这里判定的是**源空间 path 形态**——`Canvas::clipPath` 拿到的 path 还没经过 `_matrix` 烘焙，仍是用户传入的本地坐标系几何。命中后保留 shape 信息和 `_matrix`，由 ClipElement 构造函数内部归一化决定要不要把矩阵烘焙进几何。

```cpp
void Canvas::clipRect(const Rect& rect, bool antiAlias) {
  clipStack->clipRect(rect, _matrix, antiAlias);
}
void Canvas::clipRRect(const RRect& rRect, bool antiAlias) {     // 新增（如没有）
  clipStack->clipRRect(rRect, _matrix, antiAlias);
}
void Canvas::clipPath(const Path& path, bool antiAlias) {
  if (Rect r; path.isRect(&r)) {
    clipStack->clipRect(r, _matrix, antiAlias);
    return;
  }
  if (RRect rr; path.isRRect(&rr)) {
    clipStack->clipRRect(rr, _matrix, antiAlias);
    return;
  }
  auto clipPath = path;
  clipPath.transform(_matrix);
  clipStack->clipPath(clipPath, antiAlias);
}
```

`ClipStack::clipRect` / `clipRRect` 内部流程：

1. 用源 `Rect` / `RRect` + `_matrix` 构造 `ClipElement`（构造函数内部自动归一化矩阵）
2. 调用 `tryCombine` 尝试与栈顶元素几何合并（参考 5.2 节）
3. 合并失败或不可合并，元素入栈，同步维护 `_path` 和 `_bounds`

`ClipElement::transform(matrix)` 被 `ClipStack::transform` 调用时，同步更新 `_matrix = matrix * _matrix`，并作用到 `_path` 和 `_bounds`；累积后矩阵可能变成 `preservesAxisAlignment`，故内部再调一次归一化。

> **注意**：如果后续经过 `ClipStack::transform(matrix)` 后，`_matrix` 包含透视分量，`applyClip` 判定时需回退到 Path 分类处理。

### 5.2 几何级合并（参考 Skia ConservativeIntersect）

`ClipElement::tryCombine` 在入栈阶段（构造完 ClipElement 之后、入栈之前）尝试把新元素与栈顶元素求交合并为单个元素，减少 FP 数量。算法直接移植 Skia `SkRRectPriv::ConservativeIntersect`（`src/core/SkRRect.cpp:882-993`）。

**触发条件**

栈顶元素 `a` 与新元素 `b` 需同时满足：

1. `a._matrix == b._matrix`（5.1 节的 simplify 把多数 stayrect 矩阵归一为 identity，使这一条天然命中）
2. `a.isAntiAlias() == b.isAntiAlias()`
3. `a.type()` 和 `b.type()` 都是 `Rect` 或 `RRect`

任一不满足，跳过几何合并，回落到现状基于 `_path` 的兜底合并（仅在 path 等价时生效）。

**支持的组合**

| 组合 | 处理 |
|------|------|
| Rect ∩ Rect | 矩形求交，结果是 Rect（可能 empty）|
| Rect ∩ RRect / RRect ∩ Rect | 把 Rect 看作 0 半径 RRect，按 RRect ∩ RRect 处理 |
| RRect ∩ RRect | 按 `ConservativeIntersect` 算法（见下）|

**ConservativeIntersect 算法要点**（参考 Skia 实现）：

1. 先用包围盒求交得到候选交集 rect；若 rect 为空，直接 empty
2. 对四个角独立判定，根据交集角点 `test` 是来自 `a` / `b` / 两者重合 / 新生成的四种情况，分别决定该角半径
3. 合并失败的几种情况：
   - 两 RRect 在同一角点上的半径"互相交叉大小"（一个 X 大、一个 Y 大）
   - 半径不同时保守的"角点包含检查"失败
   - 合并后 `scaleRadii` 失败（半径之和超过边长）
4. 合并失败时 `tryCombine` 返回 false，两个元素都保留

**与绘制路径退化条件的协同**

合并算法对四角独立判定，能产出**任意 `RRect::Type::Complex`** 结果（哪怕输入是两个 Simple）。FP 端的 `RRectEffect` 用 arc box 分派 + `safeRadii = max(r, 0.5)` 自动处理任意半径形态（包括含直角、对角圆心盒重叠等），合并产出的 RRect 直接交给 FP 即可，不需要合并算法本身防御退化。

**未参与几何合并的元素**

- 非 stayrect 矩阵下的 Rect / RRect 元素：触发条件 1（`_matrix` 相同）天然难以命中，会形成 FP 链，受 `kMaxAnalyticFPs = 4` 上限约束
- `ClipElement::Type::Path` 元素：始终走 path 逻辑，不参与几何合并

> **几何合并的工程价值**：UI 实际场景里同帧的多次 clipRect / clipRRect 经常共享同一矩阵（同 layer 内的多次裁剪），simplify 把它们都归一为 identity，几何合并因此频繁触发。本方案相比 Skia 多覆盖了"任意仿射 RRect"那一档场景，相比纯 FP 端方案多拿到了"常见场景下的合并优化"——双层方案让两端优势同时落地。

### 5.3 重命名 / 扩展 FP

两个 FP（`RectEffect` / `RRectEffect`）共同遵守以下设计原则：

- **变体维度按"shader 形状差异"分配**：
  - `needTransform` **进 ProcessorKey 作为编译期变体**——identity 路径直接 `local = gl_FragCoord.xy`，transform 路径走 `local = (deviceToLocal * vec3(gl_FragCoord.xy, 1.0)).xy / .z`，两段 shader 形状差异显著（一整段矩阵乘 + 透视除法）。让 GLSL 编译器在编译期消掉 identity 路径的矩阵运算，运行时省一段 ALU
  - `antiAlias` **作为 uniform 在 shader 内动态切换**——AA / NonAA 用 `mix(nonAACov, aaCov, antiAlias)` 切换，避免 program 切换 + 不增加 program 数。具体两路公式各 FP 不同：`RRectEffect` 共用 `dist` 中间量，AA / NonAA 只差末尾的 `clamp(0.5 - dist, 0, 1)` 与 `step(dist, 0)`，per-fragment 多 ~3-4 ALU；`RectEffect` 两路是独立的 separable 公式（box filter 乘积 vs 四 step 相乘），per-fragment 多 ~7-8 ALU。同帧不同 clip 调用经常 AA / NonAA 混用，uniform 切换的 ALU 成本远低于 program 切换
- **CPU 端按 identity / transform 双路径处理**：identity 时不走 ApplyScales 分解，shader 走 identity 变体；transform 时 ApplyScales 分解后设 `deviceToLocal = adjustedMatrix.invert()`，shader 走 transform 变体
- **不处理 stroke**：裁剪只输出 coverage 标量
- **CPU 端 `Make` 的退化策略两 FP 一致**：仅在两条退化条件下返回 `nullptr` 降级 mask——(1) 矩阵含透视；(2) 矩阵不可逆（`getAxisScales` 任一分量为 0）。其他形态不在 CPU 端拒绝，交给 shader 处理：与绘制路径不同，FP 没有顶点 quad 自相交问题，不需要 anySmall / 对角圆心盒重叠等退化判断。`RRectEffect` shader 用 `safeRadii = max(r, 0.5)` 让"半径接近 0"自动按直角处理；arc box 分派 + one-hot collapse 让对角圆心盒重叠场景也能正确选中一个角。
- **shader 几何思路对齐绘制 GP**：`RRectEffect` shader 借鉴绘制路径 `GLSLComplexNonAARRectGeometryProcessor.cpp` 的 arc box 分派思路和 `GLSLComplexEllipseGeometryProcessor.cpp` 的 Sampson 距离场
- **shader 精度策略与绘制 GP 对齐**：本期不引入 Skia 的 `scale` uniform 归一化（详见 3.2 节 EllipticalRRectEffect 的 FP16 精度问题分析）。TGFX 当前 `EllipseGeometryProcessor` 没有做这层修正，裁剪 FP 保持一致行为——绘制有什么精度边界裁剪也有什么边界。该优化作为绘制 + 裁剪共同的二期方向，见第 1 节"后续优化可参考"

**ProcessorKey 编排**

两个 FP 都用 1 bit 编码 `needTransform`，**`antiAlias` 不进 key**（运行期 uniform，AA / NonAA 共用同一 program）：

```cpp
void RectEffect::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(needTransform ? 1u : 0u);
  // antiAlias 不写入：运行期通过 uniform 传入，shader 内 mix 切换
}
```

每个 FP 各自有 2 个 program 变体（identity / transform），两个 FP 共 4 个变体。

> **变体维度选择的依据**：把 `needTransform` 放编译期是因为两条路径 shader 形状差异大（整段矩阵乘 + 透视除法 vs 直接读 `gl_FragCoord`），编译期消掉零成本；把 `antiAlias` 放运行期是因为 AA / NonAA 切换的 ALU 成本（RRectEffect ~3-4 ALU、RectEffect ~7-8 ALU，详见上文）远低于 program 切换，且同帧 AA / NonAA 经常混用。整体取舍：把"形状几何路径"放编译期、"边缘软硬"放运行期。
>
> **变体数收益**：8 个原始变体（2 transform × 2 AA × 2 类型）→ **4 个 program 变体**（仅按 type × needTransform 区分）。clip FP 链长度 1~4 时宿主 draw 的 program key 最多 4 + 16 + 64 + 256 = **340 种** clip 段组合（远小于"8 变体"方案的 4680 种）。实际命中受 ClipStack 入栈顺序唯一性约束，冷启动后基本全命中 cache。

> **CPU 侧的 ApplyScales 复用**：`getAxisScales` 返回 `matrix` 两列范数 `(αx, αy)`。把它从 `matrix` 中抠掉得到 `M' = matrix · diag(1/αx, 1/αy)`，同时把 rect / radii 在本地空间放大到 `αx × αy` 倍，作为 shader 的等效"设备空间半径"，使 AA 半像素带在设备空间近似 0.5 像素。这与 `RRectDrawOp` 处理 viewMatrix 的方式一致，绘制路径已验证几何正确。

#### RectEffect（任意仿射矩形）

由现有 `AARectEffect` 改名扩展。承接所有"非 scissor 路径"的矩形裁剪（含 AA / NonAA、identity / 仿射）。2 个 program 变体（identity / transform），AA / NonAA 在 shader 内通过 uniform 切换。

```cpp
class RectEffect : public FragmentProcessor {
 public:
  // localRect: 本地空间矩形（identity 路径下即设备空间矩形）
  // matrix: 把 localRect 映射到设备空间的仿射矩阵；identity 时走 identity 变体（shader 不做矩阵乘）
  // antiAlias: 是否做 0.5 像素 AA 过渡（uniform 切换，不影响 program 变体）
  // 返回 nullptr 表示当前矩阵不被支持（透视、不可逆退化矩阵）
  static PlacementPtr<RectEffect> Make(BlockAllocator* allocator,
                                        const Rect& localRect,
                                        const Matrix& matrix = Matrix::I(),
                                        bool antiAlias = true);
  std::string name() const override { return "RectEffect"; }

 private:
  Rect localRect = {};                            // 本地空间矩形（原始，未外扩）
  Matrix deviceToLocal = Matrix::I();             // transform 变体下为 adjustedMatrix.invert()
  bool needTransform = false;                     // ProcessorKey：编译期选择 shader 变体
  float antiAlias = 1.0f;                         // 1.0 = AA, 0.0 = NonAA（uniform）
};
```

着色器采用 separable 1D 覆盖率乘积（对照 `GLSLAARectEffect`）：x、y 两个方向各自把 "到最近边的有向距离" clamp 到 `[-0.5, 0.5]`，两端相加得到 [0, 1] 区间的 1D 覆盖率，再相乘得到面积近似覆盖率。`LocalRect = (Lx, Ly, Rx, Ry)` **始终是原矩形**，外扩半像素的逻辑折进 shader 端 clamp 的上下界；NonAA 直接用 `step` 判 `local ∈ LocalRect`，与 AA 互不耦合，运行期由 `AntiAlias` uniform 选择。

着色器（**identity 变体**，需 ProcessorKey `needTransform=0`）：

```glsl
// uniform: LocalRect (vec4) = (Lx, Ly, Rx, Ry)、AntiAlias (float)
// identity 路径直接以 gl_FragCoord 作为本地坐标，shader 不做矩阵乘
vec2 local = gl_FragCoord.xy;
vec4 dAA = clamp(vec4(local - LocalRect.xy, LocalRect.zw - local), -0.5, 0.5);
vec2 aaCovXY = dAA.xy + dAA.zw;                   // 各方向 1D 覆盖率（原矩形边线处 = 0.5）
// AA 覆盖率 = x、y 方向 1D 覆盖率乘积（box filter：1x1 像素方块与矩形求交的面积积分）。
// 矩形顶点处像素 1/4 落在内部，aaCov = 0.5 * 0.5 = 0.25，作为可接受 AA 误差保留，
// 与 GLSLAARectEffect 行为一致。
float aaCov = aaCovXY.x * aaCovXY.y;
float nonAACov = step(LocalRect.x, local.x) * step(local.x, LocalRect.z)
               * step(LocalRect.y, local.y) * step(local.y, LocalRect.w);
float coverage = mix(nonAACov, aaCov, AntiAlias);
```

着色器（**transform 变体**，需 ProcessorKey `needTransform=1`）：

```glsl
// uniform: DeviceToLocal (mat3)、LocalRect (vec4)、AntiAlias (float)
// 通过 mat3 反变换回本地空间再算 separable 覆盖率
vec3 hl = DeviceToLocal * vec3(gl_FragCoord.xy, 1.0);
vec2 local = hl.xy / hl.z;
vec4 dAA = clamp(vec4(local - LocalRect.xy, LocalRect.zw - local), -0.5, 0.5);
vec2 aaCovXY = dAA.xy + dAA.zw;
// AA 覆盖率 = x、y 方向 1D 覆盖率乘积（box filter：1x1 像素方块与矩形求交的面积积分）。
// 矩形顶点处像素 1/4 落在内部，aaCov = 0.5 * 0.5 = 0.25，作为可接受 AA 误差保留，
// 与 GLSLAARectEffect 行为一致。
float aaCov = aaCovXY.x * aaCovXY.y;
float nonAACov = step(LocalRect.x, local.x) * step(local.x, LocalRect.z)
               * step(LocalRect.y, local.y) * step(local.y, LocalRect.w);
float coverage = mix(nonAACov, aaCov, AntiAlias);
```

> **公式与 `GLSLAARectEffect` 等价**：把原式 `clamp(t, 0, 1) + clamp(s, 0, 1) - 1` 整体平移 `-0.5`，即 `clamp(t-0.5, -0.5, 0.5) + clamp(s-0.5, -0.5, 0.5)`，两项各贡献 `-0.5` 抵消原来的 `-1`。CPU 侧外扩 0.5 与 shader 端 clamp 端点 `±0.5` 是同一事的两种参数化方式。本方案选 shader 端，使 `LocalRect` uniform 始终是原矩形，AA / NonAA 共用同一份 `LocalRect`。
>
> **`aaCovXY` 取值参考**：原矩形边线处为 0.5、内缩半像素处为 1、外扩半像素处为 0，与 `GLSLAARectEffect` 完全一致。
>
> **角点覆盖率 = 0.25 是 separable 面积近似的固有特性**：像素中心压在原矩形顶点时，1×1 像素方块只有 1/4 落在矩形内，公式给出 `aaCov = 0.5 × 0.5 = 0.25`。这是 separable 面积积分的真实结果（边线中段半压 → 0.5、顶点 1/4 压 → 0.25），与 `GLSLAARectEffect` 一致，也与 Skia `GrFillRectOp` 等业界实现一致，作为可接受的 AA 误差保留。实现时 shader 注释需说明这一行为，避免后续 review 误判为 bug。
>
> **NonAA 分支独立于 `aaCovXY`**：用 `step` 直接判 `local ∈ LocalRect`，阈值即原矩形边线，角点不会因 separable 面积乘积的双曲等高线而缺块。
>
> **AA 宽度的设备空间约定**：clamp 端点 `±0.5` 是 local 空间的半像素。identity 变体下 local = 设备空间，AA 宽度即设备半像素。transform 变体下，若 `deviceToLocal` 含缩放，过渡带宽度会被缩放成"设备空间 0.5 / scale"；沿用 CPU 侧把 `axisScales` 折进 `adjustedRect` 的做法（见下方 Make 伪代码），可以保证 `deviceToLocal` 内只剩旋转/平移，AA 宽度仍是设备空间半像素。

CPU 侧（伪代码）：

```cpp
PlacementPtr<RectEffect> RectEffect::Make(allocator, localRect, matrix, antiAlias) {
  if (matrix.hasPerspective()) return nullptr;
  Rect adjustedRect = localRect;
  Matrix deviceToLocal = Matrix::I();
  bool needTransform = false;
  if (!matrix.isIdentity()) {
    auto scales = matrix.getAxisScales();
    if (scales.x == 0 || scales.y == 0) return nullptr;   // 退化矩阵
    adjustedRect.scale(scales.x, scales.y);
    Matrix adjustedMatrix = matrix;
    adjustedMatrix.preScale(1/scales.x, 1/scales.y);
    if (!adjustedMatrix.invert(&deviceToLocal)) return nullptr;
    needTransform = true;
  }
  return new RectEffect(adjustedRect, deviceToLocal, needTransform, antiAlias ? 1.0f : 0.0f);
}
```

#### RRectEffect（任意 RRect，对照绘制路径的 ComplexRRect GP）

承接 `RRect::Type::Oval` / `Simple` / `Complex` 三种形态。**`RRect::Type::Rect`（无圆角）由调用方转发到 `RectEffect`。** 2 个 program 变体（identity / transform），AA / NonAA 在 shader 内通过 uniform 切换。shader 不区分 Simple / Complex——通过统一的"按 arc box 分派"思路自然兼容（Simple 时四个角的 radii 相同）。

```cpp
class RRectEffect : public FragmentProcessor {
 public:
  // 接受 rRect.type() ∈ {Oval, Simple, Complex}；Type::Rect 由调用方分派到 RectEffect
  // 返回 nullptr 表示当前矩阵不被支持（透视、不可逆退化矩阵），与 RectEffect::Make 一致
  static PlacementPtr<RRectEffect> Make(BlockAllocator* allocator,
                                         const RRect& rRect,
                                         const Matrix& matrix = Matrix::I(),
                                         bool antiAlias = true);
  std::string name() const override { return "RRectEffect"; }

 private:
  Rect localRect = {};                            // 本地空间矩形（原始，未外扩）
  std::array<Point, 4> radii = {};                // TL / TR / BR / BL（Simple/Oval 时四个相同值）
  Matrix deviceToLocal = Matrix::I();             // transform 变体下为 adjustedMatrix.invert()
  bool needTransform = false;                     // ProcessorKey：编译期选择 shader 变体
  float antiAlias = 1.0f;                         // 1.0 = AA, 0.0 = NonAA（uniform）
};
```

**着色器思路：按 arc box 分派 + 半径保护**（对照绘制路径 `GLSLComplexNonAARRectGeometryProcessor` / `GLSLComplexEllipseGeometryProcessor`）

绘制 GP 在 vertex 端预先标记"是否角区"，FP 没有顶点必须 fragment 端自己判定。**核心 SDF 计算两个变体共用**，唯一差别是 step 1（如何拿到本地空间坐标）。

着色器（**identity 变体**，需 ProcessorKey `needTransform=0`）：

```glsl
// uniform: LocalRect (vec4)、RadiiX (vec4)、RadiiY (vec4)、AntiAlias (float)
//   RadiiX / RadiiY 在 CPU 侧由 std::array<Point, 4> radii 拆分上传：
//     RadiiX = (radii[0].x, radii[1].x, radii[2].x, radii[3].x)
//     RadiiY = (radii[0].y, radii[1].y, radii[2].y, radii[3].y)
//   顺序 [TL, TR, BR, BL] 与成员 radii 一致

// 1. identity 路径直接以 gl_FragCoord 作为本地坐标，shader 不做矩阵乘
vec2 local = gl_FragCoord.xy;

// 2~8 同后续 transform 变体的步骤 2~8
```

着色器（**transform 变体**，需 ProcessorKey `needTransform=1`）：

```glsl
// uniform: DeviceToLocal (mat3)、LocalRect (vec4)、RadiiX (vec4)、RadiiY (vec4)、AntiAlias (float)

// 1. transform 路径：通过 mat3 反变换回本地空间
vec3 hl = DeviceToLocal * vec3(gl_FragCoord.xy, 1.0);
vec2 local = hl.xy / hl.z;

// 2. 测试 fragment 落在哪个角的 arc box 内（参考 GLSLComplexNonAARRectGeometryProcessor）
//    顺序：[TL, TR, BR, BL]
vec4 cornersX = vec4(LocalRect.x, LocalRect.z, LocalRect.z, LocalRect.x);
vec4 cornersY = vec4(LocalRect.y, LocalRect.y, LocalRect.w, LocalRect.w);
vec4 signsX   = vec4(1.0, -1.0, -1.0, 1.0);
vec4 signsY   = vec4(1.0, 1.0, -1.0, -1.0);
vec4 dx = (vec4(local.x) - cornersX) * signsX;
vec4 dy = (vec4(local.y) - cornersY) * signsY;
vec4 inBox = step(dx, RadiiX) * step(dy, RadiiY);
// one-hot collapse：相邻 arc box 在共享边缘可能同时命中，保留第一个
vec4 prefixSum = vec4(0.0, inBox.x, inBox.x + inBox.y, inBox.x + inBox.y + inBox.z);
inBox *= step(prefixSum, vec4(0.5));
float inAnyCorner = dot(inBox, vec4(1.0));

// 3. 选中那个角的 arcCenter / radii
vec4 arcCentersX = cornersX + signsX * RadiiX;
vec4 arcCentersY = cornersY + signsY * RadiiY;
vec2 arcCenter = vec2(dot(inBox, arcCentersX), dot(inBox, arcCentersY));
vec2 r = vec2(dot(inBox, RadiiX), dot(inBox, RadiiY));

// 4. 半径保护：< 0.5 设备像素时按直角处理（视觉等价）
//    避免 r=0 时除法出 NaN，同时让亚像素半径自动退化为直角效果
//    注：transform 变体下 CPU 已把 axisScales 折进 radii，r 已是设备等效尺寸；
//    identity 变体下 local = 设备空间，r 本身即设备像素，0.5 阈值在两个变体下都是设备半像素
vec2 safeRadii = max(r, vec2(0.5));

// 5. 角区椭圆 SDF（Sampson 距离）
vec2 offset = (local - arcCenter) / safeRadii;
vec2 safeOffset = max(abs(offset), vec2(1.0/4096.0));   // 防 inversesqrt(0)，常数沿用 GLSLComplexEllipseGeometryProcessor.cpp:101
float test = dot(safeOffset, safeOffset) - 1.0;
vec2 grad = 2.0 * safeOffset / safeRadii;
float gradDot = max(dot(grad, grad), 1.1755e-38);       // FP32 最小正规数，防 inversesqrt(0)
float cornerDist = test * inversesqrt(gradDot);

// 6. 边/中心区到 rect 边界的有向距离
vec2 d0 = local - LocalRect.xy;
vec2 d1 = LocalRect.zw - local;
float edgeDist = -min(min(d0.x, d0.y), min(d1.x, d1.y));   // 内部为负，外部为正

// 7. 选距离值：角区用椭圆 SDF，非角区用 edge dist
float dist = mix(edgeDist, cornerDist, inAnyCorner);

// 8. AA / NonAA 通过 uniform 切换 coverage 公式
float aaCov = clamp(0.5 - dist, 0.0, 1.0);            // AA：半像素线性过渡
float nonAACov = step(dist, 0.0);                     // NonAA：dist <= 0 → 1, dist > 0 → 0
float coverage = mix(nonAACov, aaCov, AntiAlias);
```

> **transform 变体 AA 半像素带说明**：transform 变体下，`Make` 已经把 radii 通过 ApplyScales 放大到设备空间等效尺寸（见下文 CPU 侧伪代码），`safeRadii` 的 `0.5` 阈值仍然以"设备像素"为单位，AA 的 `±0.5` 过渡带也是设备像素——所以本地坐标系里的距离场公式产出的覆盖率在设备空间近似正确。

CPU 侧（伪代码）：

```cpp
PlacementPtr<RRectEffect> RRectEffect::Make(allocator, rRect, matrix, antiAlias) {
  DEBUG_ASSERT(rRect.type() != RRect::Type::Rect);   // Rect 应由调用方走 RectEffect
  if (matrix.hasPerspective()) return nullptr;

  RRect adjusted = rRect;
  Matrix deviceToLocal = Matrix::I();
  bool needTransform = false;
  if (!matrix.isIdentity()) {
    // transform 路径：ApplyScales 分解（与绘制路径同款）
    auto scales = matrix.getAxisScales();
    if (scales.x == 0 || scales.y == 0) return nullptr;   // 退化矩阵
    adjusted.scale(scales.x, scales.y);                   // 把缩放烘焙进 rect / radii
    Matrix adjustedMatrix = matrix;
    adjustedMatrix.preScale(1/scales.x, 1/scales.y);      // 矩阵抠掉缩放
    if (!adjustedMatrix.invert(&deviceToLocal)) return nullptr;
    needTransform = true;
  }
  return new RRectEffect(adjusted, deviceToLocal, needTransform, antiAlias ? 1.0f : 0.0f);
}
```

> CPU 端退化策略与 RectEffect 一致（仅透视 / 不可逆矩阵返回 nullptr，详见 5.3 节"CPU 端 `Make` 的退化策略两 FP 一致"）。
### 5.4 applyClip 改造

> **重要分工**：矩阵归一化和 `tryCombine` 都发生在 **入栈阶段**（`ClipStack::clipRect` / `clipRRect`，见 5.1 / 5.2 节），不是 `applyClip`。`applyClip` 拿到的 `elements` 已经是入栈归一化 + 几何合并完成后的结果——本节只关注"如何把每个 element 映射到 FP"。

`applyClip` Stage 3 元素分派扩展（`src/gpu/OpsCompositor.cpp:729-786`）：

> **关于 FP 上限**：`kMaxAnalyticFPs = 4` 与 Skia 一致——FP 链越长 shader 越复杂，4 个上限能覆盖绝大多数实际场景；超出的元素降级 mask 纹理。

```cpp
constexpr size_t kMaxAnalyticFPs = 4;
size_t analyticFPCount = 0;

for (...) {
  auto& element = elements[i];
  if (!element.isValid()) continue;

  // 像素对齐矩形 / 非 AA 矩形：scissor 已覆盖
  if (element.type() == ClipElement::Type::Rect &&
      element.matrix().isIdentity() &&
      (element.isPixelAligned() || !element.isAntiAlias())) {
    continue;
  }

  if (analyticFPCount < kMaxAnalyticFPs) {
    auto fp = makeAnalyticFP(element, clipFP);
    if (fp) {
      clipFP = std::move(fp);
      ++analyticFPCount;
      continue;
    }
  }
  elementsForMask.push_back(&element);
}
```

改造后的 `makeAnalyticFP`（返回 nullptr 表示不支持，由调用方降级）：

```cpp
PlacementPtr<FragmentProcessor> OpsCompositor::makeAnalyticFP(
    const ClipElement& element, PlacementPtr<FragmentProcessor>& inputFP) {
  const auto& m = element.matrix();
  if (m.hasPerspective()) {
    return nullptr;                          // 透视降级到 mask
  }
  const bool aa = element.isAntiAlias();

  PlacementPtr<FragmentProcessor> clipFP = nullptr;
  switch (element.type()) {
    case ClipElement::Type::Rect:
      clipFP = RectEffect::Make(drawingAllocator(), element.rect(), m, aa);
      break;
    case ClipElement::Type::RRect: {
      const auto& rRect = element.rRect();
      if (rRect.type() == RRect::Type::Rect) {
        // 无圆角，直接走 RectEffect
        clipFP = RectEffect::Make(drawingAllocator(), rRect.rect(), m, aa);
      } else {
        // Oval / Simple / Complex 都走同一个 RRectEffect（shader 内统一按 arc box 分派）
        clipFP = RRectEffect::Make(drawingAllocator(), rRect, m, aa);
      }
      break;
    }
    case ClipElement::Type::Path:
      return nullptr;                        // 任意 path（非 Rect / RRect）走 mask
  }

  if (!clipFP) return nullptr;
  if (!inputFP) return clipFP;
  return FragmentProcessor::Compose(drawingAllocator(),
                                    std::move(inputFP), std::move(clipFP));
}
```

> Y 翻转 / 渲染目标朝向修正在 FP 内部处理（或在调用前对 `matrix` 做一次性 `postConcat`），不再需要在 `makeAnalyticFP` 入口对 device-space rect 调用现有的 `FlipYIfNeeded` helper（参见现状代码 `OpsCompositor.cpp:808`）。

> `Canvas::clipPath` 里如果输入 path 是 rect / RRect，应在入栈前构造对应的 `ClipElement::Type::Rect` / `ClipElement::Type::RRect`，以命中 FP 路径（见 5.1 节 `Canvas::clipPath` 改造）。

### 5.5 处理流程图

**入栈阶段**（`ClipStack::clipRect / clipRRect / clipPath` → 入栈队列）：

```
Canvas::clipRect/clipRRect/clipPath
└─ ClipStack::clipRect/clipRRect/clipPath
   ├─ 构造 ClipElement（5.1 节）
   │   ├─ clipPath 入栈前先识别 path.isRect / isRRect，命中则转发到 clipRect / clipRRect
   │   ├─ Type 三选一：Rect / RRect / Path
   │   └─ 内部自动归一化（rectStaysRect 矩阵 → 烘焙进几何、_matrix = identity；
   │       且若烘焙后 RRect 退化为 Type::Rect，reshape ClipElement::Type 到 Rect。
   │       非 rectStaysRect 矩阵下保留原 _matrix，不烘焙也不 reshape）
   ├─ tryCombine 与栈顶元素求交（5.2 节 ConservativeIntersect）
   │   ├─ _matrix 相同 + AA 一致 + 均为 Rect/RRect → 几何合并
   │   └─ 否则保留两个元素
   └─ 入栈
```

**应用阶段**（`OpsCompositor::applyClip`）：

```
applyClip
├─ Stage 1-2：Empty/WideOpen/scissor 与现状一致
├─ Stage 3：遍历 elements
│  ├─ Rect + identity + (非AA | 像素对齐) → skip (scissor 覆盖)
│  ├─ 未被上一行命中且无透视：
│  │   ├─ shape==Rect                              → RectEffect    ┐
│  │   ├─ shape==RRect, rRect.type()==Rect         → RectEffect    ├─ analyticFPCount < 4
│  │   ├─ shape==RRect, 其他 type                  → RRectEffect   ┘
│  │   │         （shader 内 arc box 分派 + safeRadii 自动处理含直角 / 亚像素半径 / 对角重叠等）
│  │   └─ 注：stayrect 矩阵下 simplify 已把 `Type::RRect + rRect.type()==Rect` reshape 为
│  │       `Type::Rect`，所以中间分支主要捕获非 stayrect 矩阵下用户传入的 0 半径 RRect
│  ├─ 透视 / Path / 上限满 / 构造失败    → elementsForMask
└─ Stage 4：elementsForMask 非空 → getClipMaskFP（现状逻辑）
```

---

## 6. 测试计划

### 6.1 单元测试（逻辑）

**ClipStack 结构 & simplify**

| 测试 | 场景 | 预期 |
|------|------|------|
| ClipStack_ClipRectPreservesShape | `clipRect` 后 `elements[0].type() == ClipElement::Type::Rect` | 通过 |
| ClipStack_TranslateScaleSimplifiedToIdentity | 平移 + 缩放下 `clipRect` 后 simplify，`_matrix == identity`，rect 烘焙到设备空间 | 通过 |
| ClipStack_Rotate90SimplifiedToIdentity | 90° 旋转下 `clipRRect`，simplify 后 `_matrix == identity`，RRect 角已按方向重排 | 通过 |
| ClipStack_MirrorSimplifiedToIdentity | 镜像（scale 含负值）下 simplify，结果几何 + identity | 通过 |
| ClipStack_Rotate45PreservesMatrix | 旋转 45° 不是 preservesAxisAlignment，simplify 后保留原矩阵，FP 走 transform 变体 | 通过 |
| ClipStack_ShearPreservesMatrix | 错切矩阵 simplify 后保留原矩阵 | 通过 |
| ClipStack_ClipPathWithRect | `clipPath` 传入 rect path，自动归类为 `Rect` | 通过 |
| ClipStack_ClipPathWithRRect | `clipPath` 传入 RRect path，自动归类为 `RRect`，type 保留 | 通过 |

**几何级合并（参考 Skia ConservativeIntersect）**

| 测试 | 场景 | 预期 |
|------|------|------|
| Combine_RectIntersectRect | 同 identity 矩阵下两个轴对齐 rect 合并为单个 rect | 通过 |
| Combine_RectInsideRRect | rect 完全在 RRect 内部，合并取 rect | 通过 |
| Combine_RRectIntersectRRect_SameRadii | 两 RRect 半径相同、嵌套，合并为内层 RRect | 通过 |
| Combine_RRectIntersectRRect_DifferentRadii | 两 RRect 角半径不同，但通过保守包含检查能合并 | 通过 |
| Combine_FailOnCrossingRadii | 两 RRect 同角点：a (10, 5)、b (5, 10) 互不包含，合并失败保留两个元素 | 通过 |
| Combine_FailOnScaleRadiiOverflow | 合并后某边两端半径之和超过边长，合并失败保留两个元素 | 通过 |
| Combine_DifferentLocalToDevice | 两个矩阵不同的 clipRect，几何合并跳过，走 path 兜底 | 通过 |
| Combine_AAMismatchSkip | AA 不一致跳过几何合并 | 通过 |

**FP 行为分派**

> 本方案两个 FP 各产出 2 个 program 变体（identity / transform，由 ProcessorKey `needTransform` 控制）；AA / NonAA 通过 uniform 在 shader 内动态切换。下表用例验证 4 种输入组合下 shader 输出正确（IdentityAA / IdentityNoAA 命中 identity 变体；TransformAA / TransformNoAA 命中 transform 变体）。

| 测试 | 场景 | 预期 |
|------|------|------|
| RectEffect_IdentityAA | identity + AA：`deviceToLocal = I`、`antiAlias = 1.0`，shader 输出半像素 AA 过渡 | 通过 |
| RectEffect_IdentityNoAA | identity + NonAA：`deviceToLocal = I`、`antiAlias = 0.0`，shader 输出硬边 | 通过 |
| RectEffect_TransformAA | 旋转 / 错切 + AA：`deviceToLocal` 为反变换矩阵、`antiAlias = 1.0`，shader 输出仿射后的 AA 过渡 | 通过 |
| RectEffect_TransformNoAA | 旋转 / 错切 + NonAA：`deviceToLocal` 为反变换矩阵、`antiAlias = 0.0`，shader 输出仿射后的硬边 | 通过 |
| AnalyticFP_DispatchByType | Rect / Oval / Simple / Complex 四类正确分派（Rect → RectEffect；其他 → RRectEffect）| 通过 |
| RRectEffect_OvalRadiiOnEdge | type==Oval 时 `radii == (width/2, height/2)` 边界条件下 shader 输出与 Simple RRect 同构 | 通过 |
| RRectEffect_OvalCircleEquivalence | type==Oval 且 width == height 时与同等圆形 Simple RRect 输出一致 | 通过 |
| RRectEffect_RadiusUnderHalfPixel | 半径 < 0.5 设备像素时 shader 内 `safeRadii` 让该角按直角渲染（视觉等价直角，无 NaN 输出）| 通过 |
| RRectEffect_ComplexAllSharpDegenerate | Complex RRect 任一角半径 < 0.5 时 shader 仍正确（不返回 nullptr，那一角按直角处理）| 通过 |
| RRectEffect_ComplexDiagonalOverlap | Complex RRect 对角圆心盒在某轴上和超过边长时 shader 输出仍正确（arc box 分派 + one-hot collapse 兜底）| 通过 |
| AnalyticFP_RejectPerspective | matrix 含透视时 `makeAnalyticFP` 返回 nullptr | 通过 |
| AnalyticFP_MaxLimit | 连续 clip 5 个**不同 matrix** 的 RRect（防止被合并），第 5 个落入 mask 纹理 | 通过 |

### 6.2 截图测试

在 `ClipTest`（或新建 `AnalyticClipTest`）中新增用例。Surface 512×512，四边边距 ≥ 50 像素，坐标整数。

| 用例 | 内容 |
|------|------|
| `AnalyticClipTest/RotatedRectClip` | 45° 旋转矩形（AA）裁剪一个填满的彩色大矩形 |
| `AnalyticClipTest/NoAARotatedRectClip` | 45° 旋转矩形（非 AA）裁剪，验证 step 硬切路径 |
| `AnalyticClipTest/ShearedRectClip` | 错切矩阵下的矩形裁剪 |
| `AnalyticClipTest/AxisAlignedSimpleRRectClip` | type==Simple 的轴对齐 RRect 裁剪 |
| `AnalyticClipTest/EllipseOvalClip` | type==Oval 且 width != height 的椭圆裁剪（验证整椭圆形态在 RRectEffect 下正确）|
| `AnalyticClipTest/CircleOvalClip` | type==Oval 且 width == height 的正圆裁剪（验证 radii.x == radii.y 时椭圆 SDF 退化为圆形 SDF 的边界）|
| `AnalyticClipTest/RotatedOvalClip` | 旋转 Oval 裁剪（验证 transform 变体下 Oval 形态仍正确）|
| `AnalyticClipTest/RotatedSimpleRRectClip` | 旋转 Simple RRect 裁剪，验证 transform 变体几何与 AA |
| `AnalyticClipTest/ScaledSimpleRRectClip` | 非均匀缩放 Simple RRect 裁剪 |
| `AnalyticClipTest/AxisAlignedComplexRRectClip` | 四角独立半径的 Complex RRect 裁剪 |
| `AnalyticClipTest/NoAAComplexRRectClip` | 四角独立半径的 Complex RRect 非 AA 裁剪，验证 NonAA 通道下硬边输出（覆盖本方案新增的"非 AA Complex RRect"通道）|
| `AnalyticClipTest/RotatedComplexRRectClip` | 旋转 Complex RRect 裁剪 |
| `AnalyticClipTest/StackedAnalyticClips` | 连续 3 个 analytic clip 叠加（轴对齐矩形 + 旋转矩形 + 旋转 Simple RRect）|
| `AnalyticClipTest/OverflowFallbackToMask` | 5 个 RRect 叠加（超过上限），验证第 5 个走 mask 仍正确 |
| `AnalyticClipTest/PerspectiveRectClip` | 透视矩形裁剪，验证走 mask 通道结果正确 |
| `AnalyticClipTest/ComplexRRectAnySmall` | 任一角半径 < 0.5 设备像素的 Complex RRect，验证 shader 内 `safeRadii` 让该角按直角渲染、其他角仍是圆角 |
| `AnalyticClipTest/ComplexRRectDiagonalOverlap` | 对角圆心盒在某轴上和超过边长的 Complex RRect，验证 arc box 分派 + one-hot collapse 让所有 fragment 仍正确归属到某个角 |
| `AnalyticClipTest/MergeRectIntersectRect` | 两个轴对齐 rect 连续 clipRect，合并后仅 1 个 FP，视觉与未合并的逐元素裁剪一致 |
| `AnalyticClipTest/MergeRRectInsideRRect` | 大 RRect + 嵌套小 RRect 连续 clipRRect，合并为小 RRect |
| `AnalyticClipTest/MergeFailedFallbackToChain` | 两 RRect 角半径互相交叉无法合并，视觉等价于两个 FP 链式 |

**反例验证用例**（验证"任意仿射 RRect"在数学边界情况下的实际表现）：

| 用例 | 内容 | 验证点 |
|------|------|-------|
| `AnalyticClipTest/ExtremeNonUniformScaleRRect` | `S(8, 1) · R(30°)` 极端长短轴比 + 非 90° 倍数旋转的 RRect 裁剪 | `getAxisScales` 在两列范数差悬殊时是否仍给出可视觉接受的形状 |
| `AnalyticClipTest/ShearedRRect` | 错切矩阵下的 RRect 裁剪（`ApplyScales` 分解后 `adjustedMatrix` 含残余错切，不是单纯 R·S 形式）| 错切下顶点位置仍正确（恒等式保证），AA 边是否可见偏差 |
| `AnalyticClipTest/RotateThenScaleRRect_vs_ScaleThenRotateRRect` | 同一组参数，先 rotate(45°) 再 scale(4,1) 与先 scale(4,1) 再 rotate(45°) 两种 RRect 裁剪并排对比 | 两种调用顺序下的视觉结果均符合预期，无视觉错乱 |
| `AnalyticClipTest/DrawRRect_vs_ClipRRect_AffineParity` | 同一仿射 RRect：一边用 `drawRRect` 画填充，一边用 `clipRRect` 裁背景；并排对比 | 裁剪 FP 与绘制路径几何语义对齐——若一致则证明本方案不引入新的视觉差异 |

每个用例先用 `shape->getPath().getBounds()` 打印取值，调整位置使内容居中。

### 6.3 边界情况

- **半径 < 0.5 设备像素（任意 RRect 形态）**：`RRectEffect` shader 用 `safeRadii = max(r, 0.5)` 把这类亚像素半径自动按直角渲染，CPU 端不需要退化判断。Complex RRect 含直角的角、其他角仍是圆角的混合形态也能正确处理。
- **对角圆心盒在某轴上和超过边长（Complex）**：FP 没有顶点 quad 自相交问题，shader 用 arc box 分派 + one-hot collapse 仍能正确选中一个角，CPU 端不需要降级 mask。
- **非 AA 的仿射矩形 / RRect**：两个 FP 都通过 `antiAlias` uniform 在同一 program 内切换硬边 / 软边公式，CPU 侧统一不做 0.5 像素外扩（外扩语义折进 shader 端 clamp 端点）。具体公式各 FP 不同——`RectEffect` 用 `mix(step×4 相乘, box filter 乘积, AntiAlias)`；`RRectEffect` 用 `mix(step(dist, 0), clamp(0.5 - dist, 0, 1), AntiAlias)`。详见 5.3 节各 shader 段落。
- **`Matrix::mapRect` 对非轴对齐矩阵返回外接包围盒**，不能用于 device-space 几何判定；本方案只在 identity 路径下使用，transform 路径直接通过 `matrix` + shader 反变换处理。
- **任意仿射 RRect 的视觉验证**：本方案对仿射 RRect 走 transform 路径，借鉴 `RRectDrawOp` 的 `ApplyScales` 分解；数学上顶点位置严格正确，AA 边在非正交矩阵下有亚像素级偏差。该结论已通过绘制路径间接验证，**新增 6.2 节末尾的反例用例直接验证**。若反例用例发现可见偏差，回退方案是把 transform 路径的 RRect 降级到 mask 纹理（仅几行代码改动），不影响轴对齐 RRect / 仿射矩形等已确认正确的路径。

---

## 7. 实现步骤

| 阶段 | 任务 | 涉及文件 |
|------|------|---------|
| 0 | 新增 `RRect::makeTransform(const Matrix&) const -> std::optional<RRect>` 公开 API（移植 Skia `SkRRect::transform` 的 radii 重排算法，对应 axis-aligned 变换下的几何 + 角顺序映射），含单元测试覆盖 identity / 平移 / 缩放 / 镜像 / 90° / 270° / 复合 / 错切 / 透视 / 极端缩放 / Oval / Rect 各路径 | `include/tgfx/core/RRect.h`、`src/core/RRect.cpp`、`test/src/RRectTest.cpp` |
| 1 | 扩展 `ClipElement` 保存 `Type` 分类、源几何 union、`matrix` | `src/core/ClipStack.h`、`src/core/ClipStack.cpp` |
| 2 | 实现 `ClipElement::simplify`（preservesAxisAlignment 矩阵烘焙进几何，依赖阶段 0 的 `RRect::makeTransform`）| `src/core/ClipStack.{h,cpp}` |
| 3 | 新增 `Canvas::clipRRect(const RRect&, bool)` 公开 API（项目当前不存在）；`ClipStack::clipRect` / `clipRRect` 接口与 `Canvas` 对接，入栈时构造 `ClipElement`（构造函数内部自动归一化）→ tryCombine | `src/core/ClipStack.{h,cpp}`、`src/core/Canvas.cpp`、`include/tgfx/core/Canvas.h` |
| 4 | 移植 `SkRRectPriv::ConservativeIntersect` 到 `src/core/RRectPriv.{h,cpp}`（私有 helper，对照 Skia `SkRRectPriv` 分层），并在 `tryCombine` 中接入（rect∩rect / rect∩rrect / rrect∩rrect 三种组合 + scaleRadii 兜底）| `src/core/RRectPriv.{h,cpp}`（新增）、`src/core/ClipStack.cpp` |
| 5 | `AARectEffect` 重命名 `RectEffect`，扩展为支持任意仿射 + AA / NonAA。2 个 program 变体（`needTransform` 进 ProcessorKey），AA / NonAA 走 `antiAlias` uniform | `src/gpu/processors/AARectEffect.*` → `src/gpu/processors/RectEffect.*`、`src/gpu/glsl/processors/GLSLAARectEffect.cpp` 对应改名 |
| 6 | 实现 `RRectEffect`（承接 Oval / Simple / Complex 三种 type，shader 用 arc box 分派 + safeRadii 保护，2 个 program 变体 identity / transform；AA / NonAA 走 `antiAlias` uniform）| `src/gpu/processors/RRectEffect.{h,cpp}`、`src/gpu/glsl/processors/GLSLRRectEffect.{h,cpp}` |
| 7 | 改造 `OpsCompositor::applyClip` / `makeAnalyticFP`：Rect / RRect 两路分派（rRect.type()==Rect 转发到 RectEffect、其他走 RRectEffect）；加 `kMaxAnalyticFPs` 上限；CPU 端透视检查在 `makeAnalyticFP` 入口拦下，不可逆退化矩阵由各 FP 内部 `Make` 兜底（详见 5.3 节"CPU 端 `Make` 的退化策略两 FP 一致"）| `src/gpu/OpsCompositor.{h,cpp}` |
| 8 | 单元测试（ClipStack 结构、simplify、几何合并、shader 含直角 / 亚像素半径 / 对角重叠的容错、上限）| `test/src/ClipStackTest.cpp`（或新增） |
| 9 | 截图测试（AnalyticClipTest 系列，含 Complex / 合并 / 反例验证） | `test/src/ClipTest.cpp`（或新增），`test/baseline/version.json` |
| 10 | 基准验证 + 调优（半径阈值、transform 路径精度抽样、program cache 体积评估、合并命中率统计）| - |

---

## 8. 参考资料

- Skia Ganesh ClipStack：`src/gpu/ganesh/ClipStack.cpp`（`apply`、`analytic_clip_fp`、`RawElement::simplify`、`RawElement::combine`）
- Skia RRect 几何合并：`src/core/SkRRect.cpp`（`SkRRectPriv::ConservativeIntersect` 第 882-993 行）
- Skia GrRRectEffect：`src/gpu/ganesh/effects/GrRRectEffect.cpp`
- Skia GrFragmentProcessor::Rect：`src/gpu/ganesh/GrFragmentProcessor.cpp:615-656`
- TGFX 裁剪现状：
  - `src/gpu/OpsCompositor.cpp:729-879`
  - `src/core/ClipStack.{h,cpp}`
  - `src/gpu/processors/AARectEffect.h`
  - `src/gpu/glsl/processors/GLSLAARectEffect.cpp`
  - `src/core/Canvas.cpp:186-195`
- TGFX 绘制路径 `ApplyScales` 分解参考：
  - `src/gpu/RRectsVertexProvider.cpp`（`ApplyScales`、`getAxisScales` 用法、isComplex 分派）
  - `src/gpu/glsl/processors/GLSLEllipseGeometryProcessor.cpp`（四角同半径的椭圆 SDF shader）
  - `src/gpu/glsl/processors/GLSLComplexEllipseGeometryProcessor.cpp`（四角独立半径的 offset 分区 shader）
  - `src/core/Canvas.cpp:274-318`（`HasSharpCorner` / `HasDiagonalCornerAxisOverlap` 退化判定）
