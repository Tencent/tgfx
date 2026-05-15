# Analytic 裁剪 FP 覆盖率优化

## 1. 背景

### 问题描述

`OpsCompositor::applyClip(const ClipStack& clipStack)` 将每个裁剪元素落到三条通道之一：

1. **Scissor**：像素对齐且开启 AA 的轴对齐矩形，或者未开启 AA 的轴对齐矩形，交由 scissor 硬裁剪
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

- 带透视的矩形 / RRect。直接原因是 RRect 的圆角投影后不再是椭圆——透视会把椭圆映射成抛物线 / 双曲线 / 椭圆中的某一种（取决于灭点位置），shader 里的 `length(dxy / radii) - 1` 椭圆 SDF 公式不再适用，即使把 fragment 反变换回本地空间也救不回来（几何形状本身就变了）。矩形虽然在透视下仍是凸四边形（4 条线性边）可以解析处理，但相关 FP 移到了二期一并做。
- 一般路径（非 Rect / RRect）

### 后续优化可参考

- **凸多边形 FP（`AAConvexPolyEffect`）**：用线性边方程组表达任意凸多边形覆盖率，可承接"凸 path 裁剪"场景（如八边形、菱形、convex path）。本期不引入——当前 `RectEffect` 扩展后已能覆盖所有仿射变换矩形；凸多边形 path 在 Canvas API 里出现频率低，工程量与收益不匹配，作为二期独立任务。
- **矩形透视裁剪**：透视下矩形仍是凸四边形（4 条线性边），可以走 `AAConvexPolyEffect`（透视下 `gl_FragCoord` 是设备空间坐标，线性边方程仍成立）。依赖上一条 `AAConvexPolyEffect` 的实现，作为它的延伸用例。RRect 透视无法用同一思路（圆角投影后不再是椭圆，shader 的椭圆 SDF 不适用），始终走 mask 纹理。
- **`Complex` RRect 中"圆形角 + 直角"特殊形态走非 Complex 路径**：参考 Skia kComplex 的 `cornerFlags` 分派（详见 3.2 节）——当 RRect 每角是直角或半径相同的圆形角时（如 D 形、tab 形、单边圆），可绕过 Complex shader 直接复用更简单的 `RRectEffect` 类 shader。本期裁剪和绘制两侧都未做该优化（绘制路径目前对此类形态也是退化到 path），作为绘制 + 裁剪共同的二期优化方向。

### 术语定义

| 术语 | 含义 |
|------|------|
| Analytic FP | 在 fragment shader 内直接由几何参数计算覆盖率的 FragmentProcessor |
| Axis-aligned | 轴对齐（矩阵只含平移和非负缩放，不含旋转/错切/翻转引起的轴变化） |
| Convex Poly | 凸多边形，每条边用 `a*x + b*y + c >= 0` 表达 |
| RRect | 四角可独立半径的轴对齐圆角矩形（对应 `tgfx::RRect`） |

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

`src/gpu/ganesh/ClipStack.cpp` 的 `apply()` 主循环按四级优先级处理每个元素：

| 优先级 | 通道 | 条件 |
|--------|------|------|
| 1 | Op 内部裁剪 | Op 支持自裁剪（`op->clipToShape()`） |
| 2 | 硬件 | scissor / window rectangles |
| 3 | Analytic FP | 满足形状条件，且当前 FP 数 < 4 |
| 4 | 软 mask / stencil mask | 其他所有情况 |

核心决策函数 `analytic_clip_fp`（`ClipStack.cpp:256-277`）按以下顺序判定：

```
if (localToDevice 是 identity) {
    if (shape 是 rect)  → GrFragmentProcessor::Rect()
    if (shape 是 rrect) → GrRRectEffect::Make()
}
// 凸多边形分支允许带任意仿射变换：把 path 顶点 mapPoints 到设备空间后再生成边方程
if (shape 仅含直线段 && shape 是凸的) → GrConvexPolyEffect::Make(devicePath)
else → 失败（降级到 mask / stencil）   // 非 identity 的 RRect 含曲线段，会落到这里
```

每个元素独立判定；上限 4 个 FP，超出的元素落到 mask。

### 3.2 关键 FP

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

> **TGFX 这边的 FP 拆分对照**：Skia 按 SkRRect 子类型（kRect / kOval / kSimple / kNinePatch / kComplex）做最细的 5 路分派；TGFX 本方案按"**四角是否独立半径**"做两路分派（`RRectEffect` 处理 Rect / Oval / Simple，`ComplexRRectEffect` 处理 Complex）。这是与 TGFX 绘制路径的 GP 拆分一致——`EllipseGeometryProcessor`（四角同半径）vs `ComplexEllipseGeometryProcessor`（四角独立），两套 shader 的几何处理思路不同，裁剪 FP 直接复用同样的拆分依据。

#### GrConvexPolyEffect

接收一组线性边方程（最多 8 条），每条表达为 `(a, b, c)`，满足 `a² + b² = 1`（归一化法线）。shader 内对每条边计算 `signed distance = a*x + b*y + c`，取所有边的最小距离做 AA saturate：

```
float distance = minAllEdges(a * gl_FragCoord.x + b * gl_FragCoord.y + c);
float coverage = clamp(distance + 0.5, 0, 1);
```

适用于：
- 仿射变换后的矩形（4 条边）
- 任意凸多边形（例如 RRect 的直边部分 + 切角近似，但 Skia 仅对纯凸多边形使用）

### 3.3 TGFX 差异

| 方面 | Skia | TGFX 现状 | 本方案目标 |
|------|------|-----------|----------|
| ClipElement 保存源几何 | 是（SkRRect + 矩阵分离） | 否（只存烘焙后的 Path） | 是（Rect/RRect + `localToDevice`）|
| 仿射变换矩形 FP | GrConvexPolyEffect（凸多边形通用解法）| 无（走 mask 纹理）| `RectEffect` transform 变体（专用解法，比通用凸多边形便宜）|
| 仿射变换 RRect FP | 无（非 identity 直接降级 mask）| 无（走 mask 纹理）| `RRectEffect` / `ComplexRRectEffect` transform 变体（比 Skia 激进，复用绘制路径已验证的 `ApplyScales` 分解）|
| RRect FP 拆分依据 | 5 路（SkRRect 子类型）| - | 2 路（四角是否独立半径，对齐 TGFX 绘制路径 GP 拆分）|
| Analytic FP 上限 | 4 | 无显式上限（仅 AARectEffect 一种）| 4 |

#### 矩阵处理策略：入栈归一化 vs FP 端处理

Skia 和本方案在"何时把 `localToDevice` 折进几何"上选了不同的时机，直接影响 `analytic_clip_fp` 看到的输入形态以及支持范围：

**Skia 的入栈归一化**（`ClipStack::RawElement::simplify`）

每个元素入栈时立即跑一次 `simplify`：

- 若 `localToDevice.preservesAxisAlignment()`（identity / 平移 / 缩放 / 镜像 / 90°倍数旋转），**直接把矩阵烘焙进 Rect / RRect 几何，`localToDevice` 重置为 identity**
- 否则保持矩阵不变，几何照原样

所以等到 `analytic_clip_fp` 那段简短代码跑的时候，**"isIdentity 的 RRect" 已经覆盖了所有 `preservesAxisAlignment` 矩阵下的 RRect**——非 identity 的 RRect 一定是真正的旋转/错切矩阵，`SkRRect::transform` 无法把它折叠成新 RRect，只能降级 mask。这是 Skia 选择"RRect + 非 identity 就 fail"的根本原因。

**本方案的 FP 端处理**

`ClipElement` 原样保留 `localToDevice`，不在入栈时折叠：

- `AARectEffect` / `AARRectEffect` 自身识别 identity / transform 两种变体
- transform 变体借鉴 `RRectDrawOp::ApplyScales` 的分解，shader 端用 `deviceToLocal` 反变换坐标——绕过了"`RRect::transform` 必须输出有效 RRect"这个限制，从而支持**任意仿射 RRect**

| 维度 | Skia 入栈归一化 | 本方案 FP 端处理 |
|------|---------------|----------------|
| ClipElement 复杂度 | 简单（多数 identity）| 保留任意仿射矩阵 |
| 分派判定 | 看 `isIdentity` 即可 | FP 内部按变体走 |
| 几何合并（`tryCombine`）| identity 下 RRect 直接做几何级求交 | 不同矩阵难做几何级合并，回落到 path 比较 |
| 支持范围 | 仅 `preservesAxisAlignment` 的 RRect | 任意仿射 RRect |
| 着色器复杂度 | 低（仅 identity 一种）| 多一个 transform 变体 |

权衡：本方案选 FP 端处理换"任意仿射 RRect"的覆盖范围，代价是几何级合并机会变少（5.4 节）；多出的 transform 变体 shader 复杂度可控（一次 mat2 反变换 + 现有 SDF 公式）。

---

## 4. 对比分析

| 场景 | 现状通道 | 目标通道 | 节省 |
|------|---------|---------|------|
| 像素对齐矩形 | scissor | scissor | - |
| 非 AA 轴对齐矩形 | scissor | scissor | - |
| AA 轴对齐矩形 | AARectEffect | RectEffect（identity + AA 变体）| - |
| AA 任意仿射矩形（旋转/错切/非均匀缩放）| mask 纹理 | RectEffect（transform + AA 变体）| 1 离屏 pass + 1 采样 |
| 非 AA 任意仿射矩形 | mask 纹理 | RectEffect（transform + noAA 变体）| 1 离屏 pass + 1 采样 |
| AA 轴对齐 RRect（Oval / Simple）| mask 纹理 | RRectEffect（identity + AA 变体）| 1 离屏 pass + 1 采样 |
| AA 任意仿射 RRect（Oval / Simple）| mask 纹理 | RRectEffect（transform + AA 变体）| 1 离屏 pass + 1 采样 |
| AA Complex RRect（四角独立半径，任意仿射）| mask 纹理 | ComplexRRectEffect（identity / transform + AA 变体）| 1 离屏 pass + 1 采样 |
| 半径 < 0.5 设备像素 / 对角圆心盒在某轴上重叠 | mask 纹理 | mask 纹理（与绘制路径退化条件对齐）| - |
| 带透视矩形 / RRect | mask 纹理 | mask 纹理 | - |
| 一般路径 | mask 纹理 | mask 纹理 | - |

---

## 5. 重构/实现方案

### 5.1 ClipElement 扩展

为了在 `applyClip` 拿到"源几何 + 矩阵"而非烘焙后的 path，`ClipElement` 增加分类和源几何字段。

```cpp
enum class ClipShape {
  Rect,     // 矩形（源空间，本地坐标系）
  RRect,    // RRect（源空间，本地坐标系），具体子类型保留在 RRect::type()
  Path      // 一般路径
};

class ClipElement {
 public:
  ClipElement(const Rect& rect, const Matrix& matrix, bool antiAlias);
  ClipElement(const RRect& rRect, const Matrix& matrix, bool antiAlias);
  ClipElement(const Path& path, bool antiAlias);

  ClipShape shape() const;
  // Valid when shape() == Rect.
  const Rect& rect() const;
  // Valid when shape() == RRect.
  // RRect::type() 区分 Rect / Oval / Simple / Complex，由 makeAnalyticFP 端按 type 分派到具体 FP。
  const RRect& rRect() const;
  // localToDevice brings the source shape (Rect / RRect) into device space.
  // For shape() == Path, this is identity (path already stored in device space).
  const Matrix& localToDevice() const;

  const Path& path() const;              // always available (lazy materialized)
  const Rect& bounds() const;            // device-space bounds
  bool isAntiAlias() const;
  bool isValid() const;
  bool isPixelAligned() const;

  // ... 其余接口保持不变
 private:
  ClipShape _shape = ClipShape::Path;
  Rect _rect = {};
  RRect _rRect = {};
  Matrix _localToDevice = Matrix::I();
  Path _path = {};             // 源几何烘焙矩阵后的 device-space path
  Rect _bounds = {};
  bool _antiAlias = false;
  int _invalidatedByIndex = -1;
};
```

`Canvas::clipRect` / `Canvas::clipPath` 的对应改造：

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

`ClipStack::clipRect` / `clipRRect` 内部：
- 存源 `Rect`/`RRect` + `_matrix`，并用该矩阵把 path/bounds 烘焙到设备空间供后续 `tryCombine` / `tightContains` 等使用
- `ClipElement::transform(matrix)` 被 `ClipStack::transform` 调用时，同步更新 `_localToDevice = matrix * _localToDevice`，并作用到 `_path` 和 `_bounds`

> **注意**：如果后续经过 `ClipStack::transform(matrix)` 后，`_localToDevice` 包含透视分量，`applyClip` 判定时需回退到 Path 分类处理。

### 5.2 重命名 / 扩展 FP

三个 FP（`RectEffect` / `RRectEffect` / `ComplexRRectEffect`）共同遵守以下设计原则：

- **两个变体维度**通过 ProcessorKey 表达：
  - `useLocalTransform`：identity 时 shader 直接用 `gl_FragCoord` 做 SDF；非 identity 时 CPU 端 `ApplyScales` 分解，shader 用 `deviceToLocal` 反变换回本地坐标系再算
  - `antiAlias`：AA 用 `clamp` 做 0.5 像素过渡；非 AA 用 `step` 硬切
- **不处理 stroke**：裁剪只输出 coverage 标量；filled 退化条件与绘制路径对齐（半径 < 0.5 设备像素、对角圆心盒在某轴上和超过边长），由 `Make` 返回 nullptr，调用方降级 mask
- **shader 几何思路对齐 GP**：`RRectEffect` 对照 `EllipseGeometryProcessor`；`ComplexRRectEffect` 对照 `ComplexEllipseGeometryProcessor`——这两套绘制 GP 的几何处理思路在本期裁剪 FP 上直接复用

> **CPU 侧的 ApplyScales 复用**：`getAxisScales` 返回 `localToDevice` 两列范数 `(αx, αy)`。把它从 `localToDevice` 中抠掉得到 `M' = localToDevice · diag(1/αx, 1/αy)`，同时把 rect / radii 在本地空间放大到 `αx × αy` 倍，作为 shader 的等效"设备空间半径"，使 AA 半像素带在设备空间近似 0.5 像素。这与 `RRectDrawOp` 处理 viewMatrix 的方式一致，绘制路径已验证几何正确。

#### RectEffect（任意仿射矩形）

由现有 `AARectEffect` 改名扩展。承接所有"非 scissor 路径"的矩形裁剪（含 AA / 非 AA、identity / 仿射）。

```cpp
class RectEffect : public FragmentProcessor {
 public:
  // localRect: 本地空间矩形（identity 路径下即设备空间矩形）
  // localToDevice: 把 localRect 映射到设备空间的仿射矩阵；identity 时走快路径
  // antiAlias: 是否做 0.5 像素 AA 过渡；非 AA 用 step 硬切
  // 返回 nullptr 表示当前形态不被支持（如透视、退化矩阵）
  static PlacementPtr<RectEffect> Make(BlockAllocator* allocator,
                                        const Rect& localRect,
                                        const Matrix& localToDevice = Matrix::I(),
                                        bool antiAlias = true);
  std::string name() const override { return "RectEffect"; }

 private:
  Rect localRect = {};
  Matrix localToDevice = Matrix::I();
  bool useLocalTransform = false;   // ProcessorKey
  bool antiAlias = true;            // ProcessorKey
};
```

着色器（**identity + AA 变体**，与现状一致）：

```glsl
// uniform: Rect = (left, top, right, bottom)，已外扩 0.5 像素
vec4 dists4 = clamp(vec4(1,1,-1,-1) * (gl_FragCoord.xyxy - Rect), 0.0, 1.0);
vec2 dists2 = dists4.xy + dists4.zw - 1.0;
float coverage = dists2.x * dists2.y;
```

着色器（**transform + AA 变体**）：

```glsl
// uniform: DeviceToLocal、LocalHalfSize (vec2)
vec2 local = DeviceToLocal * vec3(gl_FragCoord.xy, 1.0);
vec2 dxy = abs(local) - LocalHalfSize;          // 内部为负，外部为正
float coverage = clamp(0.5 - max(dxy.x, dxy.y), 0.0, 1.0);
```

着色器（**transform + 非 AA 变体**）：

```glsl
// 非 AA：用 step 硬切，省 0.5 像素外扩
vec2 local = DeviceToLocal * vec3(gl_FragCoord.xy, 1.0);
vec2 dxy = abs(local) - LocalHalfSize;
float coverage = step(max(dxy.x, dxy.y), 0.0);
```

CPU 侧（伪代码）：

```cpp
PlacementPtr<RectEffect> RectEffect::Make(allocator, localRect, localToDevice, antiAlias) {
  if (localToDevice.hasPerspective()) return nullptr;
  if (localToDevice.isIdentity()) {
    Rect deviceRect = localRect;
    if (antiAlias) deviceRect.outset(0.5f, 0.5f);
    return new RectEffect(deviceRect, Matrix::I(),
                          /*useLocalTransform=*/false, antiAlias);
  }
  auto scales = localToDevice.getAxisScales();
  if (scales.x == 0 || scales.y == 0) return nullptr;   // 退化矩阵
  Rect adjustedRect = localRect;
  adjustedRect.scale(scales.x, scales.y);
  Matrix adjustedMatrix = localToDevice;
  adjustedMatrix.preScale(1/scales.x, 1/scales.y);
  Matrix deviceToLocal;
  if (!adjustedMatrix.invert(&deviceToLocal)) return nullptr;
  return new RectEffect(adjustedRect, deviceToLocal,
                        /*useLocalTransform=*/true, antiAlias);
}
```

#### RRectEffect（四角同半径 RRect，对照 `EllipseGeometryProcessor`）

处理 `RRect::Type::Oval` / `Simple`。`RRect::Type::Rect`（无圆角）由调用方转发到 `RectEffect`。

```cpp
class RRectEffect : public FragmentProcessor {
 public:
  // 仅接受 rRect.type() ∈ {Oval, Simple}；其他类型由调用方分派
  static PlacementPtr<RRectEffect> Make(BlockAllocator* allocator,
                                         const RRect& rRect,
                                         const Matrix& localToDevice = Matrix::I(),
                                         bool antiAlias = true);
  std::string name() const override { return "RRectEffect"; }

 private:
  Rect localRect = {};
  Point radii = {};
  Matrix localToDevice = Matrix::I();
  bool useLocalTransform = false;
  bool antiAlias = true;
};
```

着色器思路（identity + AA 变体）：用绘制路径 `EllipseGeometryProcessor` 同款椭圆 SDF——单位圆方程 `dot(offset, offset) - 1` + 梯度归一化得近似像素距离：

```glsl
// uniform: Rect、Radii (vec2)
vec2 dxy0 = Rect.xy + Radii - gl_FragCoord.xy;  // 左/上方向正
vec2 dxy1 = gl_FragCoord.xy - (Rect.zw - Radii);
vec2 dxy  = max(max(dxy0, dxy1), 0.0);
float d   = (1.0 - length(dxy / Radii)) * min(Radii.x, Radii.y);
float coverage = clamp(d + 0.5, 0.0, 1.0);
```

transform 变体把上面的 `gl_FragCoord.xy` 替换为 `DeviceToLocal * vec3(gl_FragCoord.xy, 1)`，rect / radii 用 `ApplyScales` 放大后的本地值。非 AA 变体把 `clamp(... + 0.5, 0, 1)` 替换为 `step(0, d)`。

CPU 侧（伪代码）：

```cpp
PlacementPtr<RRectEffect> RRectEffect::Make(allocator, rRect, localToDevice, antiAlias) {
  DEBUG_ASSERT(rRect.type() == RRect::Type::Oval || rRect.type() == RRect::Type::Simple);
  if (localToDevice.hasPerspective()) return nullptr;
  auto scales = localToDevice.getAxisScales();
  if (scales.x == 0 || scales.y == 0) return nullptr;
  // 半径退化（< 0.5 设备像素）由调用方降级到 RectEffect
  if (rRect.radii()[0].x * scales.x < 0.5f && rRect.radii()[0].y * scales.y < 0.5f) {
    return nullptr;
  }
  if (localToDevice.isIdentity()) {
    return new RRectEffect(rRect, Matrix::I(),
                           /*useLocalTransform=*/false, antiAlias);
  }
  RRect adjusted = rRect;
  adjusted.scale(scales.x, scales.y);
  Matrix adjustedMatrix = localToDevice;
  adjustedMatrix.preScale(1/scales.x, 1/scales.y);
  Matrix deviceToLocal;
  if (!adjustedMatrix.invert(&deviceToLocal)) return nullptr;
  return new RRectEffect(adjusted, deviceToLocal,
                         /*useLocalTransform=*/true, antiAlias);
}
```

#### ComplexRRectEffect（四角独立半径 RRect，对照 `ComplexEllipseGeometryProcessor`）

处理 `RRect::Type::Complex`。

```cpp
class ComplexRRectEffect : public FragmentProcessor {
 public:
  // 仅接受 rRect.type() == Complex
  static PlacementPtr<ComplexRRectEffect> Make(BlockAllocator* allocator,
                                                 const RRect& rRect,
                                                 const Matrix& localToDevice = Matrix::I(),
                                                 bool antiAlias = true);
  std::string name() const override { return "ComplexRRectEffect"; }

 private:
  Rect localRect = {};
  std::array<Point, 4> radii = {};       // TL / TR / BR / BL
  Matrix localToDevice = Matrix::I();
  bool useLocalTransform = false;
  bool antiAlias = true;
};
```

着色器思路（对照 `GLSLComplexEllipseGeometryProcessor`）：FP 没有顶点，无法用绘制端的"vertex 属性传 offset、判区域"那套机制，需要 shader 内部在 fragment 端自己判定所在象限。流程：

```glsl
// uniform: Rect、Radii[4] (TL, TR, BR, BL)
// 1. 反变换（仅 transform 变体）：vec2 local = DeviceToLocal * vec3(gl_FragCoord.xy, 1);
// 2. 按象限选择对应半径
vec2 center = (Rect.xy + Rect.zw) * 0.5;
int q = (local.x < center.x ? 0 : 1) + (local.y < center.y ? 0 : 2);
vec2 r = Radii[remap_quadrant_to_TL_TR_BR_BL(q)];
// 3. 椭圆 SDF（与 RRectEffect 同公式，只是 r 按象限取）
vec2 dxy0 = Rect.xy + r - local;
vec2 dxy1 = local - (Rect.zw - r);
vec2 dxy  = max(max(dxy0, dxy1), 0.0);
float d   = (1.0 - length(dxy / r)) * min(r.x, r.y);
float coverage = clamp(d + 0.5, 0.0, 1.0);
```

CPU 侧（伪代码）：与 `RRectEffect::Make` 流程相同，区别仅在于：

- `DEBUG_ASSERT(rRect.type() == RRect::Type::Complex)`
- 退化条件检查"**任一**角半径 < 0.5 设备像素"（绘制路径 `anySmall` 同款），不再用 RRectEffect 那条"所有角半径都 < 0.5"的退化条件
- 退化条件再加一条 `HasDiagonalCornerAxisOverlap`（对角圆心盒在某轴上和超过边长）：返回 nullptr 让调用方降级 mask

> **退化条件与绘制路径对齐**：见 `src/core/Canvas.cpp:274-318`（`HasSharpCorner` / `HasDiagonalCornerAxisOverlap`）。本方案在 FP 端复用同样的判定逻辑，确保"绘制 RRect 退化到 path 的形态"在裁剪侧同样不进 analytic 路径。


### 5.3 applyClip 改造

`applyClip` Stage 3 元素分派扩展（`src/gpu/OpsCompositor.cpp:729-786`）：

> **关于 FP 上限**：`kMaxAnalyticFPs = 4` 与 Skia 一致——FP 链越长 shader 越复杂，4 个上限能覆盖绝大多数实际场景；超出的元素降级 mask 纹理。

```cpp
constexpr size_t kMaxAnalyticFPs = 4;
size_t analyticFPCount = 0;

for (...) {
  auto& element = elements[i];
  if (!element.isValid()) continue;

  // 像素对齐矩形 / 非 AA 矩形：scissor 已覆盖
  if (element.shape() == ClipShape::Rect &&
      element.localToDevice().isIdentity() &&
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
  const auto& m = element.localToDevice();
  if (m.hasPerspective()) {
    return nullptr;                          // 透视降级到 mask
  }
  const bool aa = element.isAntiAlias();

  PlacementPtr<FragmentProcessor> clipFP = nullptr;
  switch (element.shape()) {
    case ClipShape::Rect:
      clipFP = RectEffect::Make(drawingAllocator(), element.rect(), m, aa);
      break;
    case ClipShape::RRect: {
      const auto& rRect = element.rRect();
      switch (rRect.type()) {
        case RRect::Type::Rect:
          // 无圆角，直接走 RectEffect
          clipFP = RectEffect::Make(drawingAllocator(), rRect.rect(), m, aa);
          break;
        case RRect::Type::Oval:
        case RRect::Type::Simple:
          clipFP = RRectEffect::Make(drawingAllocator(), rRect, m, aa);
          // 半径退化时 RRectEffect::Make 返回 nullptr，降级到 RectEffect
          if (!clipFP) {
            clipFP = RectEffect::Make(drawingAllocator(), rRect.rect(), m, aa);
          }
          break;
        case RRect::Type::Complex:
          clipFP = ComplexRRectEffect::Make(drawingAllocator(), rRect, m, aa);
          // 退化时（anySmall / 对角圆心盒重叠）返回 nullptr 降级 mask
          break;
      }
      break;
    }
    case ClipShape::Path:
      return nullptr;                        // 一般 path 走 mask
  }

  if (!clipFP) return nullptr;
  if (!inputFP) return clipFP;
  return FragmentProcessor::Compose(drawingAllocator(),
                                    std::move(inputFP), std::move(clipFP));
}
```

> **关于 Complex RRect 的退化为何不降级 RectEffect**：Complex 退化的两个条件（任一角 < 0.5 / 对角重叠）都意味着 RRect 几何本身在 shader 里算不准，且这类形态把它当成普通矩形画也会有视觉错——只能交给 mask 纹理处理。

> Y 翻转 / 渲染目标朝向修正在 FP 内部处理（或在调用前对 `localToDevice` 做一次性 `postConcat`），不再需要外部 `FlipYIfNeeded` / `MatrixWithYFlip` 辅助函数。

> `Canvas::clipPath` 里如果输入 path 是 rect / RRect，应在入栈前构造对应的 `ClipShape::Rect` / `ClipShape::RRect`，以命中 FP 路径（见 5.1 节 `Canvas::clipPath` 改造）。

### 5.4 几何合并兼容性

`ClipElement::tryCombine` / `tightContains` / `looseIntersects` 目前基于 `_path` + `_bounds`。新结构保留了烘焙后的 `_path` 和设备空间 `_bounds`，**现有合并逻辑无需改动**。只需在构造和 `transform` 时同步维护 `_path` 和 `_localToDevice` 一致性。

### 5.5 处理流程图

```
applyClip
├─ Stage 1-2：Empty/WideOpen/scissor 与现状一致
├─ Stage 3：遍历 elements
│  ├─ Rect + identity + (非AA | 像素对齐) → skip (scissor 覆盖)
│  ├─ shape==Rect  + 无透视              → RectEffect             ┐
│  ├─ shape==RRect + 无透视：                                       ├─ analyticFPCount < 4
│  │   ├─ type==Rect                     → RectEffect              │
│  │   ├─ type==Oval/Simple              → RRectEffect             │
│  │   │     （半径 < 0.5 设备像素时降级为 RectEffect）              │
│  │   └─ type==Complex                  → ComplexRRectEffect      │
│  │         （anySmall / 对角圆心盒重叠 → elementsForMask）        ┘
│  ├─ 透视 / Path / 上限满 / 构造失败    → elementsForMask
└─ Stage 4：elementsForMask 非空 → getClipMaskFP（现状逻辑）
```

---

## 6. 测试计划

### 6.1 单元测试（逻辑）

| 测试 | 场景 | 预期 |
|------|------|------|
| ClipStack_ClipRectPreservesShape | `clipRect` 后 `elements[0].shape() == Rect` | 通过 |
| ClipStack_RotatedRectStillRectShape | 旋转 45° 的 `clipRect` 后 shape 仍为 Rect，localToDevice 含旋转 | 通过 |
| ClipStack_ClipPathWithRect | `clipPath` 传入 rect path，自动归类为 `Rect` | 通过 |
| ClipStack_ClipPathWithRRect | `clipPath` 传入 RRect path，自动归类为 `RRect`，type 保留 | 通过 |
| RectEffect_IdentityAA | identity + AA 走 identity-AA shader 变体 | 通过 |
| RectEffect_IdentityNoAA | identity + 非 AA 走 identity-noAA shader 变体（step）| 通过 |
| RectEffect_TransformAA | 旋转 / 错切 + AA 走 transform-AA shader 变体 | 通过 |
| RectEffect_TransformNoAA | 旋转 / 错切 + 非 AA 走 transform-noAA shader 变体 | 通过 |
| RRectEffect_DispatchByType | Rect / Oval / Simple 三类正确分派（Rect → RectEffect；Oval/Simple → RRectEffect）| 通过 |
| RRectEffect_OvalRadiiOnEdge | type==Oval 时 `radii == (width/2, height/2)` 边界条件下 shader 输出与 Simple RRect 同构 | 通过 |
| RRectEffect_OvalCircleEquivalence | type==Oval 且 width == height 时与同等圆形 Simple RRect 输出一致 | 通过 |
| RRectEffect_RadiusUnderHalfPixel | 半径 < 0.5 设备像素时 `Make` 返回 nullptr，调用方降级 RectEffect | 通过 |
| ComplexRRectEffect_DispatchByType | type==Complex 的 RRect 分派到 ComplexRRectEffect | 通过 |
| ComplexRRectEffect_AnySmallDegenerate | 任一角半径 < 0.5 设备像素时 `Make` 返回 nullptr，调用方降级 mask | 通过 |
| ComplexRRectEffect_DiagonalOverlapDegenerate | 对角圆心盒在某轴上和超过边长时 `Make` 返回 nullptr，降级 mask | 通过 |
| AnalyticFP_RejectPerspective | localToDevice 含透视时 `makeAnalyticFP` 返回 nullptr | 通过 |
| AnalyticFP_MaxLimit | 连续 clip 5 个 RRect，第 5 个落入 mask 纹理 | 通过 |

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
| `AnalyticClipTest/RotatedComplexRRectClip` | 旋转 Complex RRect 裁剪 |
| `AnalyticClipTest/StackedAnalyticClips` | 连续 3 个 analytic clip 叠加（轴对齐矩形 + 旋转矩形 + 旋转 Simple RRect）|
| `AnalyticClipTest/OverflowFallbackToMask` | 5 个 RRect 叠加（超过上限），验证第 5 个走 mask 仍正确 |
| `AnalyticClipTest/PerspectiveRectClip` | 透视矩形裁剪，验证走 mask 通道结果正确 |
| `AnalyticClipTest/ComplexRRectAnySmallFallback` | 任一角半径 < 0.5 设备像素的 Complex RRect，验证降级 mask 后结果正确 |
| `AnalyticClipTest/ComplexRRectDiagonalOverlapFallback` | 对角圆心盒在某轴上和超过边长的 Complex RRect，验证降级 mask 后结果正确 |

**反例验证用例**（验证"任意仿射 RRect"在数学边界情况下的实际表现）：

| 用例 | 内容 | 验证点 |
|------|------|-------|
| `AnalyticClipTest/ExtremeNonUniformScaleRRect` | `S(8, 1) · R(30°)` 极端长短轴比 + 非 90° 倍数旋转的 RRect 裁剪 | `getAxisScales` 在两列范数差悬殊时是否仍给出可视觉接受的形状 |
| `AnalyticClipTest/ShearedRRect` | 错切矩阵下的 RRect 裁剪（不属于 `R·S` 也不属于 `S·R`，独立矩阵形态）| 错切下顶点位置仍正确（恒等式保证），AA 边是否可见偏差 |
| `AnalyticClipTest/RotateThenScaleRRect_vs_ScaleThenRotateRRect` | 同一组参数，先 rotate(45°) 再 scale(4,1) 与先 scale(4,1) 再 rotate(45°) 两种 RRect 裁剪并排对比 | 两种调用顺序下的视觉结果均符合预期，无视觉错乱 |
| `AnalyticClipTest/DrawRRect_vs_ClipRRect_AffineParity` | 同一仿射 RRect：一边用 `drawRRect` 画填充，一边用 `clipRRect` 裁背景；并排对比 | 裁剪 FP 与绘制路径几何语义对齐——若一致则证明本方案不引入新的视觉差异 |

每个用例先用 `shape->getPath().getBounds()` 打印取值，调整位置使内容居中。

### 6.3 边界情况

- **半径 < 0.5 设备像素（Oval / Simple）**：`RRectEffect::Make` 返回 nullptr，`makeAnalyticFP` 自动降级到 `RectEffect`（半径忽略，视觉上几乎不可见）。逻辑在 `makeAnalyticFP` 的 switch 分支里实现。
- **半径 < 0.5 设备像素或对角圆心盒重叠（Complex）**：`ComplexRRectEffect::Make` 返回 nullptr，降级到 mask 纹理。不再尝试 `RectEffect`——这类形态把它当成普通矩形画会有视觉错。
- **非 AA 的仿射矩形 / RRect**：三个 FP 都通过 `antiAlias` ProcessorKey flag 表达，shader 内 `clamp` 替换为 `step`，CPU 侧不外扩 0.5 像素。
- **`Matrix::mapRect` 对非轴对齐矩阵返回外接包围盒**，不能用于 device-space 几何判定；本方案只在 identity 变体下使用，transform 变体直接走 `localToDevice` + shader 反变换。
- **任意仿射 RRect 的视觉验证**：本方案对仿射 RRect 走 transform 变体，借鉴 `RRectDrawOp` 的 `ApplyScales` 分解；数学上顶点位置严格正确，AA 边在非正交矩阵下有亚像素级偏差。该结论已通过绘制路径间接验证，**新增 6.2 节末尾的反例用例直接验证**。若反例用例发现可见偏差，回退方案是把 transform 变体的 RRect 降级到 mask 纹理（仅几行代码改动），不影响轴对齐 RRect / 仿射矩形等已确认正确的路径。

---

## 7. 实现步骤

| 阶段 | 任务 | 涉及文件 |
|------|------|---------|
| 1 | 扩展 `ClipElement` 保存 shape 分类、源几何、`localToDevice` | `src/core/ClipStack.h`、`src/core/ClipStack.cpp` |
| 2 | `ClipStack::clipRect` / `clipRRect` 接口与 `Canvas` 对接 | `src/core/ClipStack.{h,cpp}`、`src/core/Canvas.cpp`、`include/tgfx/core/Canvas.h` |
| 3 | `AARectEffect` 重命名 `RectEffect`，加 `useLocalTransform` + `antiAlias` 两个 ProcessorKey flag，新增 transform 与 noAA 变体 | `src/gpu/processors/AARectEffect.*` → `src/gpu/processors/RectEffect.*`、`src/gpu/glsl/processors/GLSLAARectEffect.cpp` 对应改名 |
| 4 | 实现 `RRectEffect`（Oval / Simple，identity / transform × AA / noAA 四变体） | `src/gpu/processors/RRectEffect.{h,cpp}`、`src/gpu/glsl/processors/GLSLRRectEffect.{h,cpp}` |
| 5 | 实现 `ComplexRRectEffect`（按象限选半径的 shader 思路，与 4 同样四变体） | `src/gpu/processors/ComplexRRectEffect.{h,cpp}`、`src/gpu/glsl/processors/GLSLComplexRRectEffect.{h,cpp}` |
| 6 | 改造 `OpsCompositor::applyClip` / `makeAnalyticFP`：按 `rRect.type()` 三路分派；加 `kMaxAnalyticFPs` 上限；退化条件（半径过小、对角圆心盒重叠）判定与绘制路径对齐 | `src/gpu/OpsCompositor.{h,cpp}` |
| 7 | 单元测试（ClipStack 结构、FP 变体选择、按 type 分派、退化条件、上限） | `test/src/ClipStackTest.cpp`（或新增） |
| 8 | 截图测试（AnalyticClipTest 系列，含 Complex / 反例验证） | `test/src/ClipTest.cpp`（或新增），`test/baseline/version.json` |
| 9 | 基准验证 + 调优（半径阈值、transform 变体精度抽样、四个 FP 变体的 program cache 体积评估） | - |

---

## 8. 参考资料

- Skia Ganesh ClipStack：`src/gpu/ganesh/ClipStack.cpp`（`apply`、`analytic_clip_fp`）
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
