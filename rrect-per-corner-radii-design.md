# RRect 四角独立半径扩展 — 技术设计文档

## 1. 背景与目标

### 1.1 背景

TGFX 当前的 `RRect` 结构仅支持 **Simple 模式**——四个角共用一组 `(radiusX, radiusY)` 半径。然而在实际业务中，四角不同圆角半径是非常常见的需求（如对话气泡、卡片 UI），目前业务方只能将其转为 Path 后再提交，导致：

1. **无法走 GPU RRect 快速路径**：`Path::isRRect()` 要求 `skRRect.isSimple()` 才返回 true，四角不同半径的 RRect Path 只能走通用的 Shape 光栅化/三角化流程。
2. **无法合批**：通用 Shape 的合批能力远弱于 RRect 专用的合批（最多 1024 个 RRect/draw call）。
3. **API 不完整**：相比 Skia 的 `SkRRect`（支持 Empty/Rect/Oval/Simple/NinePatch/Complex 六种类型），TGFX 做了过度简化。

### 1.2 目标

- 扩展 `RRect` 数据结构，支持四角独立椭圆半径
- `Path::isRRect()` 能识别 NinePatch/Complex 类型的 RRect
- GPU 渲染管线（AA 和 Non-AA）适配四角独立半径，保持 GPU 加速
- `Rectangle` VectorElement 的 `roundness` 支持四值独立设置
- 实现 W3C CSS 规范的半径缩放算法

---

## 2. 现有实现分析

### 2.1 TGFX 当前 RRect 数据结构

```cpp
// include/tgfx/core/RRect.h
struct RRect {
  Rect rect = {};
  Point radii = {};  // radii.x = xRadius, radii.y = yRadius (四角统一)

  bool isRect() const;    // radii == (0,0)
  bool isOval() const;    // radii >= rect.size()/2
  void setRectXY(const Rect& rect, float radiusX, float radiusY);
  void setOval(const Rect& oval);
  void scale(float scaleX, float scaleY);
};
```

- **数据量**：6 个 float（rect 4 + radii 2）
- **当前能力**：对 Simple 类型（四角统一半径）完备——`isRect()` / `isOval()` 判断、`setRectXY()` 内含简化版半径缩放（`2 * radius > side` 时按比例缩小）
- **扩展四角独立半径后需要新增/升级的能力**：
  - Type 枚举（区分 Simple/NinePatch/Complex，用于 GPU 路径选择）
  - `std::array<Point, 4> radii`（四角独立半径数据）
  - W3C 完整半径缩放（检查四条边各自相邻角半径之和，当前简化版仅适用于统一半径）
  - `setRectRadii()` / `setNinePatch()` 等新 API

### 2.2 调用路径与分发逻辑

```
Canvas::drawRRect(rRect, paint)
  → Canvas::drawPath → path.isRRect(&rRect) → drawContext->drawRRect()  [Path 反向优化]
  → drawContext->drawRRect()  [直接绘制]
    → OpsCompositor::drawRRect()  [收集到 pendingRRects 批处理]
      → RRectsVertexProvider::MakeFrom()  [生成顶点]
      → RRectDrawOp::Make()  [创建 GPU 绘制操作]
```

TGFX 只有**一个 Op**——`RRectDrawOp`，内部根据 `AAType` 选择不同的 `GeometryProcessor`：

```
RRectDrawOp
  ├─ AAType::Coverage → EllipseGeometryProcessor（16 顶点 9-patch）
  └─ AAType::None     → NonAARRectGeometryProcessor（4 顶点 quad）
```

这不是两个独立的 Op，而是同一个 Op 内的两条渲染路径。`AAType` 在 `OpsCompositor` 收集阶段由 `Surface` 的 AA 配置决定，同一个 draw call 内所有 RRect 使用相同的路径。

此外，`OpsCompositor::drawRRect()` 中的合批条件会区分 Fill 和 Stroke：

```cpp
static bool ShouldFlushRectOps(const std::vector<PlacementPtr<Stroke>>& pendingStrokes,
                               const Stroke* stroke) {
  if (pendingStrokes.empty()) {
    return stroke != nullptr;    // 之前是 fill，来了 stroke → flush
  }
  return stroke == nullptr || pendingStrokes.front()->join != stroke->join;
  // 之前是 stroke，来了 fill → flush；或 join 不同 → flush
}
```

因此 **Fill 和 Stroke 不能合批**（不同 `GeometryProcessor` key → 不同 pipeline），连续 fill → stroke 会产生两个独立的 draw call。

### 2.3 RRectDrawOp — 唯一的 RRect GPU Op

#### 2.3.1 AA 路径（EllipseGeometryProcessor，16 顶点 9-patch）

**适用条件**：`AAType::Coverage`

**几何布局**：4×4 网格 = 16 顶点，9 个 quad

```
    ____________
   |_|________|_|    ← row 0
   | |        | |    ← row 1
   | |        | |
   |_|________|_|    ← row 2
   |_|________|_|    ← row 3
   col0  1  2  col3
```

每个顶点携带 `reciprocalRadii[4]` = `{1/xRadius, 1/yRadius, 1/innerXRadius, 1/innerYRadius}` 和 `EllipseOffset(float2)`（归一化到单位椭圆坐标或绝对像素距离，取决于 Fill/Stroke）。

**Fragment shader 算法**：椭圆隐式方程 + Sampson 距离近似

```glsl
float test = dot(offset, offset) - 1.0;          // f(x,y) = x²+y²-1
float2 grad = 2.0 * offset * EllipseRadii.xy;    // ∇f
float invlen = inversesqrt(dot(grad, grad));      // 1/|∇f|
float edgeAlpha = clamp(0.5 - test * invlen, 0.0, 1.0);  // d ≈ f/|∇f|
```

9-patch 分区让 fragment shader 在不同区域有不同的计算量：
- **角区域**（四角 2×2）：`EllipseOffset` 非零，需要完整的椭圆距离计算
- **边区域**（上下左右各 1×2）：一个分量接近 0，距离退化为线性
- **中心区域**（中间 2×2）：`EllipseOffset ≈ 0`，`test ≈ -1`，`edgeAlpha` 直接饱和为 1.0（几乎零计算开销）

**关键限制**：所有顶点共用同一组 `reciprocalRadii`，即四个角使用相同的椭圆方程，无法表达四角独立半径。

**Fill 处理**：

- `EllipseOffset` 归一化到单位圆坐标（`xMaxOffset /= xRadius`）
- `reciprocalRadii[2,3]` 填充为 `1e6`（因为 `innerRadius = 0`，`FloatInvert(0) = 1e6`），但 shader 中 `stroke == false` 使内椭圆代码段在编译期被移除，这两个分量不会被读取
- 索引：54（9 个 quad = 18 个三角形，含中心 quad）
- Fragment shader 只算外边缘：`edgeAlpha = clamp(0.5 - test * invlen, 0.0, 1.0)`

**Stroke 处理**：

外/内半径计算：
```
外半径 = cornerRadius + halfStrokeWidth
内半径 = cornerRadius - halfStrokeWidth
```
当 `cornerRadius < halfStrokeWidth` 时，内半径为负，`stroked = false`，退化为填充处理（TGFX 无 overstroke 处理，参见 3.2.1 节 Skia 的 overstroke 方案）。

- `EllipseOffset` 保持绝对像素距离（`xMaxOffset = xOuterRadius`）
- `reciprocalRadii` 四个分量都有效（外 + 内半径倒数）
- 索引：48（比 Fill 少 6 = 去掉中心 quad，因为描边中心是空的）
- Fragment shader 外边缘 × 内边缘：
  ```glsl
  float edgeAlpha = clamp(0.5 - test * invlen, 0.0, 1.0);  // 外椭圆
  offset = EllipseOffsets.xy * EllipseRadii.zw;              // 用内半径倒数缩放
  test = dot(offset, offset) - 1.0;
  edgeAlpha *= clamp(0.5 + test * invlen, 0.0, 1.0);        // 注意 +test（取反）
  ```
  内椭圆用 `0.5 + test` 而非 `0.5 - test`，因为内边缘需要在椭圆**外部**保留 coverage、内部裁掉。

`stroke` 是编译期常量（`GeometryProcessor` 构造时确定），shader 不存在运行时分支。

#### 2.3.2 Non-AA 路径（NonAARRectGeometryProcessor，4 顶点 quad）

这是 TGFX 自行设计的方案，不来源于 Skia Ganesh（Ganesh 无专用 Non-AA RRect 快速路径，Non-AA 时走 `FillRRectOp` 的 `kFakeNonAA` 模式或 Path）。

**适用条件**：`AAType::None`

**几何布局**：4 顶点 quad = 2 个三角形 = 6 个索引

每个顶点携带 `localCoord(Float2)` + `radii(Float2)` + `rectBounds(Float4)`，Stroke 额外携带 `strokeWidth(Float2)`。

**Fragment shader 算法**：圆角矩形 SDF + `step()` 硬边判断

```glsl
vec2 q = abs(localCoord - center) - halfSize + radii;
float d = min(max(q.x/radii.x, q.y/radii.y), 0.0) + length(max(q/radii, 0.0)) - 1.0;
float coverage = step(d, 0.0);
```

SDF 在本地坐标空间计算距离值 `d`，`step(d, 0.0)` 只关心正负号，不需要设备空间单位。4 顶点覆盖整个 RRect，每个 fragment 都执行完整的 SDF 计算（包括 `abs`、`max`、`length`、除法）。

**关键限制**：SDF 公式 `abs(p - center)` 隐式假设四角对称，无法表达四角独立半径。

**Fill 处理**：

- 每顶点 10 floats（position 2 + localCoord 2 + radii 2 + bounds 4）
- Fragment shader 只算外 SDF：`coverage = step(d, 0.0)`

**Stroke 处理**：

- 每顶点 12 floats（额外 + strokeWidth 2），`radii` 和 `bounds` 已在 CPU 侧扩展了半描边宽度
- Fragment shader 计算外 SDF - 内 SDF：
  ```glsl
  float outerCoverage = step(d, 0.0);
  vec2 innerHalfSize = halfSize - 2.0 * sw;
  vec2 innerRadii = max(radii - 2.0 * sw, vec2(0.0));
  if (innerHalfSize.x > 0.0 && innerHalfSize.y > 0.0) {
      float di = ...; // 同样的 SDF 公式，用 innerRadii
      innerCoverage = step(di, 0.0);
  }
  float coverage = outerCoverage * (1.0 - innerCoverage);
  ```
  Non-AA stroke 在 shader 中动态计算内矩形/内半径，有 `if` 分支处理内矩形退化。`stroke` 同样是编译期常量。

#### 2.3.3 AA 与 Non-AA 使用不同算法的原因

Non-AA 的 SDF 在本地坐标空间计算距离值 `d`，用 `step(d, 0.0)` 做硬边判断，只关心正负号，不需要知道 `d` 的设备空间单位。

AA 需要平滑的 coverage 过渡（`clamp(0.5 - d, 0, 1)`），这要求距离值 `d` 是**设备空间像素单位**——即需要知道"本地坐标 1 单位 = 屏幕多少像素"。获取设备空间距离有两种方式：

1. **`fwidth(d)`**：GPU 硬件前向差分，但跨三角形边缘不准确，且 TGFX 需兼容不支持 `fwidth` 的平台
2. **解析梯度计算**：椭圆隐式方程 `f/|∇f|`（Sampson 距离）自带梯度信息，直接给出像素级距离

TGFX AA 路径选择了方式 2（椭圆距离场），配合 9-patch 把形状分区：中心和边缘区域 `EllipseOffset ≈ 0`，`edgeAlpha` 直接饱和为 1.0（几乎零计算开销），只在角落区域才执行椭圆距离计算。而 4 顶点 SDF 方案每个 fragment 都需要完整计算圆角矩形距离（包括 `abs`、`max`、`length`、除法），中心区域也不例外。

#### 2.3.4 两条路径的对比总结

| | AA 路径 | Non-AA 路径 |
|---|---|---|
| **GeometryProcessor** | `EllipseGeometryProcessor` | `NonAARRectGeometryProcessor` |
| **顶点数/RRect** | 16（4×4 九宫格） | 4（quad） |
| **索引数（Fill / Stroke）** | 54 / 48 | 6 / 6 |
| **距离算法** | 椭圆 Sampson 距离 `f/|∇f|` | 圆角矩形 SDF + `step()` |
| **距离单位** | 预归一化到单位椭圆（设备空间近似） | 本地坐标空间 |
| **中心区域开销** | 几乎为零（`edgeAlpha` 直接饱和） | 完整 SDF 计算 |
| **Fill 每顶点 floats** | 12（position 2 + offset 2 + recipRadii 4 + color 4） | 10（position 2 + localCoord 2 + radii 2 + bounds 4） |
| **Stroke 每顶点 floats** | 12（同 Fill，内半径编码在 recipRadii[2,3]） | 12（额外 strokeWidth 2） |
| **内边缘数据位置** | 顶点属性 `reciprocalRadii[2,3]` | Fragment shader 动态计算 |
| **coverage 合成（Stroke）** | `outerAlpha * innerAlpha`（乘法） | `outerCoverage * (1.0 - innerCoverage)` |
| **退化处理（Overstroke）** | `stroked = false`，退化为 fill | `if (innerHalfSize > 0)` 运行时分支 |
| **Fill/Stroke 合批** | ❌ 不同 pipeline | ❌ 不同 pipeline |
| **四角独立半径** | ❌ 共用 reciprocalRadii | ❌ SDF 假设对称 |

---

## 3. 参考设计

Skia 有两套 GPU 后端（Ganesh 和 Graphite），采用完全不同的 RRect 渲染策略。两者都基于 `SkRRect` 数据结构。

### 3.1 Skia SkRRect 数据结构

```cpp
class SkRRect {
  enum Type { kEmpty, kRect, kOval, kSimple, kNinePatch, kComplex };
  enum Corner { kUpperLeft, kUpperRight, kLowerRight, kLowerLeft };

  SkRect fRect;
  SkVector fRadii[4];  // 8 个 float，顺序：UL, UR, LR, LL
  int32_t fType;

  void setRectRadii(const SkRect&, const SkVector radii[4]);
  void scaleRadii();    // W3C CSS 半径缩放
  void computeType();   // 根据数据推断类型
};
```

- **数据量**：12 个 float + 1 个 int32（13 个 4 字节值）
- **半径缩放**：使用 double 精度，遵循 W3C CSS Background 5.5 节

**六种 Type 的含义**

| Type | 含义 | 半径条件 | 图示 |
|---|---|---|---|
| `kEmpty` | 空矩形 | `rect.isEmpty()` | （无） |
| `kRect` | 纯矩形，无圆角 | 四角半径全为 0 | `┌──┐ └──┘` |
| `kOval` | 椭圆（半径 = 半边长） | `radii[i].x == width/2 && radii[i].y == height/2` | `╭──╮ ╰──╯` |
| `kSimple` | 四角统一半径 | 四角 `(x,y)` 完全相同 | `╭──╮ ╰──╯`（角较小） |
| `kNinePatch` | 轴对齐半径 | 左两角 X 相同、右两角 X 相同、上两角 Y 相同、下两角 Y 相同 | 左右/上下对称但四角不同 |
| `kComplex` | 完全任意的四角半径 | 不满足以上任何条件 | 四角各不相同 |

**NinePatch 的具体判定条件**：
```
radii[UL].x == radii[LL].x   // 左侧两角的 X 半径相同
radii[UR].x == radii[LR].x   // 右侧两角的 X 半径相同
radii[UL].y == radii[UR].y   // 上侧两角的 Y 半径相同
radii[LL].y == radii[LR].y   // 下侧两角的 Y 半径相同
```

NinePatch 意味着水平方向可以用左 X / 右 X 两个值描述，垂直方向可以用上 Y / 下 Y 两个值描述，形成天然的九宫格切分。这在 GPU 渲染中是重要的优化点——Ganesh 的 `FillRRectOp` 可以利用这个特性。

**Type 对 GPU 路径选择的影响**：
- `kSimple`：可以进入所有 GPU 快速路径（CircularRRectOp、EllipticalRRectOp、FillRRectOp）
- `kNinePatch` / `kComplex`：只有 FillRRectOp（Ganesh）和 AnalyticRRectRenderStep（Graphite）支持
- TGFX 当前只支持 `kSimple`

### 3.2 Skia Ganesh（老后端，Android/Chrome 生产在用）

Ganesh 对 RRect 提供了 **3 种 GPU 快速路径（Op）**，通过一个有优先级的 fallback 链逐级尝试，全部失败则回退 Path 渲染。TGFX 当前的 AA 路径来源于其中的 `EllipticalRRectOp`。

**分发入口**：`SurfaceDrawContext::drawRRect()`

```
drawRRect(rrect, style, viewMatrix, aaType)
    │
    │ 第 1 优先级
    ├─ CircularRRectOp ← 条件最严格
    │
    │ 第 2 优先级
    ├─ FillRRectOp ← 仅 fill，但 RRect 类型最宽松
    │
    │ 第 3 优先级
    ├─ EllipticalRRectOp ← 支持 stroke，但要求 Simple
    │
    │ 全部失败
    └─ drawShapeUsingPathRenderer（多级 PathRenderer 选择）
```

#### 3.2.1 Op 1：`CircularRRectOp`（16/24 顶点 4×4 网格）

适用条件（全部满足）：
- `aaType == Coverage`（必须 coverage AA，不能是 MSAA）
- `rrect.isSimple()`（四角统一半径）
- `radii.fX == radii.fY`（X/Y 半径相等，即圆角非椭圆角）
- `viewMatrix.isSimilarity()`（等比缩放 + 旋转，不能有非均匀缩放）
- `viewMatrix.rectStaysRect()`（轴对齐）

使用 `CircleGeometryProcessor`，fragment shader 用**圆的距离函数**：
```glsl
float d = length(circleEdge.xy);
half distanceToOuterEdge = half(circleEdge.z * (1.0 - d));
half edgeAlpha = saturate(distanceToOuterEdge);
// stroke 时额外计算内边缘：
half distanceToInnerEdge = half(circleEdge.z * (d - circleEdge.w));
edgeAlpha *= saturate(distanceToInnerEdge);
```

**三种子模式**

通过 `RRectType` 枚举区分：
- `kFill`：纯填充，16 顶点，54 索引（9 个 quad）
- `kStroke`：常规描边（`innerRadius > 0`），16 顶点，48 索引（去掉中心 quad）
- `kOverstroke`：过度描边（`strokeWidth > 2 * cornerRadius`），24 顶点，66 索引

类型判定逻辑：
```
devStrokeWidth <= 0                              → kFill
strokeOnly && cornerR - halfStroke >= 0          → kStroke
strokeOnly && cornerR - halfStroke < 0           → kOverstroke
strokeOnly && strokeWidth > min(width, height)   → kFill（描边完全覆盖矩形，退化）
```

**顶点坐标计算**

设 `R` = `devRect`（设备空间矩形），`r` = `devRadius`，`h` = `halfStrokeWidth`，`a` = `aaBloat`（通常 0.5）：

```
outerRadius = r + h + a
bounds = R.outset(h + a, h + a)
```

4 列 × 4 行 = 16 个顶点位置：
```
col0 = R.left  - h - a          row0 = R.top    - h - a
col1 = R.left  + r              row1 = R.top    + r
col2 = R.right - r              row2 = R.bottom - r
col3 = R.right + h + a          row3 = R.bottom + h + a
```

注意 col1/col2/row1/row2 化简后 h 和 a 被抵消——**9-patch 的内边界线恰好穿过圆角的圆心**，不受描边宽度和 AA bloat 影响。

每个顶点携带 `circleEdge(float4)` = `(offset.x, offset.y, outerRadius, innerRadius)`：
```
offset.x:  col0 → -1,  col1 → 0,  col2 → 0,  col3 → +1
offset.y:  row0 → -1,  row1 → 0,  row2 → 0,  row3 → +1
```

varying 插值后在各区域的行为：
- **角区域**（四角 2×2）：`length(xy)` 从 0→√2，`d=1` 处是圆弧外边缘 → 实现圆角
- **边区域**（上下左右 1×2）：一个分量 = 0，`length = |另一分量|` → 线性 AA
- **中心区域**（中间 2×2）：`xy = (0,0)`，`length = 0` → 全覆盖

**Fill vs Stroke 的数据差异**

关键在 `innerRadius` 的值：
- **Fill**：`innerRadius = -1/outerRadius`（负值），shader 中 `d - circleEdge.w` 始终 > 0 → `innerAlpha = 1.0` → 不裁剪
- **Stroke**：`innerRadius = (cornerR - halfStroke) / outerRadius`（正值），`d < circleEdge.w` 的区域被裁掉

**Fill 和 Stroke 可以合批**（与 TGFX 不同）：
```cpp
GrGeometryProcessor* gp = CircleGeometryProcessor::Make(arena, !fAllFill, ...);
// fAllFill = fAllFill && that->fAllFill（合批时计算）
```
只要有一个 stroke，整个 Op 使用 stroke shader。Fill 的负 `innerRadius` 使 stroke shader 的内边缘裁剪无效化——同一个 shader 对 fill 和 stroke 都能产生正确结果，通过顶点数据（而非 pipeline）区分行为。

TGFX 的 Fill 和 Stroke 使用不同的 `GeometryProcessor` key → 不同 pipeline → 不能合批。不过这在实际业务中影响有限（连续交替 fill/stroke 且 brush 相同的场景罕见），如果未来 TGFX 实现事后 Op 合并机制，可参考 Skia 的这个 `innerRadius` 负值技巧实现 Fill/Stroke 合批。

**Overstroke 为什么需要额外 8 个顶点**

Overstroke 时 `strokeWidth > 2 * cornerRadius`，圆角被描边完全吃掉。但如果 `strokeWidth < min(width, height)`，矩形中间仍有一个直角矩形空洞。

问题在于 fragment shader 的距离函数 `length(xy)` 只能产生**圆弧形等值线**。正常 stroke 时角的内边缘是圆弧，`length(xy)` 正确。但 overstroke 时角的内边缘应该是**直线**（直角矩形洞的边缘），`length(xy)` 在角区域内侧会产生错误的弧形 AA 裁剪：

```
overstroke 的内边缘应该是直角：     length(xy) 实际产生的：
   ┌──────────                        ╭──────────
   │█████████                         │████████╱  ← 弧形裁剪（错误）
   │█████────   ← 直线内边缘          │██████╱
```

额外 8 个顶点覆盖角区域的内侧，使用**不同的 `outerRadius` 和恒定的 offset**（`y = 0`），让 `length(xy) ≈ |x|` 退化为线性距离，替换掉错误的圆弧距离场，确保直角内边缘的 AA 正确。

TGFX 没有 overstroke 处理——当 `innerRadius <= 0` 时直接 `stroked = false`，退化为 fill（实心填充扩展了半描边宽度的 RRect），不画内部空洞。在 `strokeWidth > 2 * cornerRadius` 且 `strokeWidth < min(width, height)` 的场景下，TGFX 的渲染结果与 Skia 不同。

#### 3.2.2 Op 2：`FillRRectOp`（40 顶点静态模板 + 实例化渲染）

适用条件：
- `style.isSimpleFill()`（纯填充，无描边）
- `caps->drawInstancedSupport()`（硬件支持实例化渲染）
- `!viewMatrix.hasPerspective()`（不支持透视）
- RRect 尺寸 < 1e6（防数值溢出）
- **不要求 `isSimple()`**，任意 RRect 类型（Simple / NinePatch / Complex）都接受

几何布局：40 个静态顶点 = 8 内八边形（全覆盖）+ 8 外八边形（线性 AA）+ 24 角弧（每角 6 个）
Instance data 包含 `radii_x(float4)` + `radii_y(float4)`，**天然支持四角独立半径**。
Fragment shader 用 `x² + y² - 1` 单位圆方程 + `fwidth()` 硬件导数做 AA。

#### 3.2.3 Op 3：`EllipticalRRectOp`（16 顶点 4×4 九宫格）

适用条件：
- `aaType == Coverage` 或 `dynamicMSAA`
- `rrect.isSimple()`（四角统一半径）
- `viewMatrix.rectStaysRect()`（轴对齐）
- 半径 ≥ 0.5px（太小时 9-patch 内部会有 fractional coverage）

使用 `EllipseGeometryProcessor`，fragment shader 用椭圆隐式方程 + Sampson 距离：
```glsl
float test = dot(offset, offset) - 1.0;
float2 grad = 2.0 * offset * ellipseRadii.xy;
float edgeAlpha = saturate(0.5 - test * inversesqrt(dot(grad, grad)));
```
支持 fill 和 stroke，支持椭圆角（X/Y 半径不同）。**这就是 TGFX AA 路径的来源。**
参考论文：[VanVerth GDC 2015 - Drawing Antialiased Ellipse](https://www.essentialmath.com/GDC2015/VanVerth_Jim_DrawingAntialiasedEllipse.pdf)

#### 3.2.4 各 Op 对不同 RRect 类型的覆盖情况

| RRect 类型 + 样式 | CircularRRectOp | FillRRectOp | EllipticalRRectOp | Path 回退 |
|---|---|---|---|---|
| Simple + 圆角 + Fill | ✅ 最优 | ✅ 次选 | ✅ | |
| Simple + 圆角 + Stroke | ✅ 最优 | ❌ 仅 fill | ✅ | |
| Simple + 椭圆角 + Fill | ❌ 需要圆角 | ✅ | ✅ | |
| Simple + 椭圆角 + Stroke | ❌ | ❌ | ✅ 唯一 | |
| NinePatch/Complex + Fill | ❌ 需要 Simple | ✅ 唯一 | ❌ 需要 Simple | |
| NinePatch/Complex + Stroke | ❌ | ❌ | ❌ | ✅ 唯一 |

#### 3.2.5 Path 回退的完整逻辑（`drawShapeUsingPathRenderer`）

当所有 RRect 快速路径 Op 都失败时，调用：
```cpp
this->drawShapeUsingPathRenderer(clip, std::move(paint), aa, viewMatrix,
                                 GrStyledShape(rrect, style, DoSimplify::kNo));
```

`GrStyledShape` 内部的 `GrShape` **直接存储 `SkRRect` 对象**（类型 `Type::kRRect`），此时并不转成 Path。RRect → Path 的转换是按需发生的，不是入口处立即转换。

`drawShapeUsingPathRenderer` 内部是一个多级 PathRenderer 选择链：

```
drawShapeUsingPathRenderer(GrStyledShape{rrect, style})
    │
    │ 第 1 步：Stroke 直接走硬件 tessellation
    ├─ if stroke → TessellationPathRenderer.canDrawPath()?
    │  ✅ → 硬件 tessellation shader 直接细分曲线绘制
    │
    │ 第 2 步：simplify() 尝试简化 + 重试简单形状路径
    ├─ shape.simplify() → drawSimpleShape()
    │  ✅ → 可能回到 FillRRectOp 等快速路径
    │
    │ 第 3 步：原始 shape 直接找 PathRenderer
    ├─ getPathRenderer(shape with style) → 各种 renderer 尝试
    │  ✅ → 选中的 renderer 直接绘制（shape 内部还是 RRect，
    │       renderer 需要时调用 GrShape::asPath() 按需转 Path）
    │
    │ 第 4 步：apply path effect
    ├─ if pathEffect → shape.applyStyle(kPathEffectOnly)
    │  shape 可能变成 Path，再次查找 renderer
    │
    │ 第 5 步：apply stroke → 轮廓化
    ├─ if style.applies() → shape.applyStyle(kPathEffectAndStrokeRec)
    │  stroke 通过 SkStrokeRec::applyToPath() 转为填充轮廓 Path
    │  再次查找 renderer
    │
    │ 第 6 步：最终兜底
    └─ SoftwarePathRenderer（CPU 光栅化 → 上传为纹理）
```

其中 `GrShape::asPath()` 被 PathRenderer 按需调用时，通过 `SkPathBuilder::addRRect(fRRect)` 生成路径——Skia 的 `addRRect` 会将 SkRRect 转为一系列 `moveTo` + `conicTo`（二次锥曲线）构成的封闭路径。

Ganesh 中可能参与的 PathRenderer：

| PathRenderer | 处理方式 | 适用场景 |
|---|---|---|
| `TessellationPathRenderer` | 硬件 tessellation shader 直接细分曲线 | Stroke 优先，需要 HW tessellation 支持 |
| `AAConvexPathRenderer` | 凸路径的解析 AA | 凸的 fill 路径（Complex RRect fill 是凸的） |
| `TriangulatingPathRenderer` | CPU 三角化 → 上传三角形 mesh | 填充路径的通用方案 |
| `StencilAndCoverPathRenderer` | stencil 模板缓冲 + cover pass | 复杂路径的标准 GPU 方案 |
| `SoftwarePathRenderer` | CPU 光栅化 → 上传为纹理 | 最终兜底，所有形状都能画 |

因此 **Complex RRect + Stroke** 的实际路径通常是：先尝试硬件 tessellation → 不支持则 stroke 轮廓化为填充 Path → 三角化或 stencil 绘制 → 最终兜底 CPU 光栅化。"Path 回退"是一个多级选择过程，不是简单的"转成 Path 然后三角化"。

**TGFX drawPath 与 Ganesh Path 回退的对比**

TGFX 当前不支持四角独立半径的 RRect 快速路径，业务方将此类 RRect 转为 Path 提交后走 `drawPath` → `PathTriangulator` 三角化。与 Ganesh 的 Path 回退相比：

| | TGFX drawPath | Ganesh drawShapeUsingPathRenderer |
|---|---|---|
| **策略数** | 基本 1 种（CPU 三角化） | 5 种 PathRenderer 按优先级选择 |
| **GPU tessellation** | ❌ 无 | ✅ `TessellationPathRenderer`（需硬件支持） |
| **GPU stencil** | ❌ 无 | ✅ `StencilAndCoverPathRenderer` |
| **CPU 三角化** | ✅ `PathTriangulator::ToTriangles` | ✅ `TriangulatingPathRenderer` |
| **CPU 光栅化为纹理** | ✅（复杂路径备选） | ✅ `SoftwarePathRenderer`（兜底） |
| **凸路径优化** | ❌ 无专门路径 | ✅ `AAConvexPathRenderer` |
| **RRect 识别回快速路径** | ✅ `path.isRRect()` → RRectDrawOp | ✅ `simplify()` → RRect Op |
| **AA 方式** | `PathTriangulator::ToAATriangles`（alpha ramp） | 取决于具体 PathRenderer |

TGFX 的 Path 三角化路径（`PathTriangulator::ToTriangles` / `ToAATriangles`）与 Ganesh 的 `TriangulatingPathRenderer` 在思路上相似：都是 CPU 侧将 Path 拆成三角形 mesh 上传 GPU。但 Ganesh 还有 GPU tessellation 和 stencil 等纯 GPU 方案，策略更丰富。

这也是本次四角独立半径改造的核心动机之一：让四角不同半径的 RRect 走 GPU 快速路径（`RRectDrawOp`），避免回退到 CPU 三角化。

### 3.3 Skia Graphite（新后端，Chrome 136+ Mac 已上线）

Graphite 采用**完全不同的架构**——统一 pipeline，AA/Non-AA 不分路径。

**核心设计：`AnalyticRRectRenderStep`**

源码位置：`src/gpu/graphite/render/AnalyticRRectRenderStep.cpp`

**几何布局：36 顶点（每角 9 顶点 × 4 角），69 索引**

每个角的 9 个顶点分三层：
```
ID  position  normal        normalScale  centerWeight  用途
0   (1,0)     (1, 0)        +1 outset    0             外层 AA ramp
1   (1,0)     (√2/2,√2/2)   +1 outset    0             外层 AA ramp（45°方向）
2   (0,1)     (√2/2,√2/2)   +1 outset    0             外层 AA ramp（45°方向）
3   (0,1)     (0, 1)        +1 outset    0             外层 AA ramp
4   (1,0)     (√2/2,√2/2)    0 anchor    0             圆弧控制线（非圆弧上的点）
5   (0,1)     (√2/2,√2/2)    0 anchor    0             圆弧控制线（非圆弧上的点）
6   (1,0)     (1, 0)        -1 inset     0             内曲线/描边内边缘
7   (0,1)     (0, 1)        -1 inset     0             内曲线/描边内边缘
8   (1,0)     (1, 0)        -1 inset     1 center      中心填充
```

69 个索引构成**三条连续的 Triangle Strip**，每条 strip 包含所有 4 个角（TL → TR → BR → BL → 闭合回 TL）。角间跳转产生的三角形覆盖了两角之间的直边区域。

顶点编号：每角 9 个顶点，起始偏移 = 角ID × 9（TL=+0, TR=+9, BR=+18, BL=+27）。

```
Strip 1：外层 AA ramp（outset ↔ anchor），30 索引
  TL: 0,4,1,5,2,3,5,  TR: 9,13,10,14,11,12,14,  BR: 18,22,19,23,20,21,23,  BL: 27,31,28,32,29,30,32,  闭合: 0,4
  每角 7 索引 = 4 个有效三角形（outset→anchor AA 渐变）+ 1 个重复索引（维持 strip 奇偶绕序）
  角间跳转三角形覆盖两角之间的直边 AA 带
  功能：覆盖形状外扩 1px 的区域，fragment shader 计算 0→1 的 coverage 过渡（AA 带）

Strip 2：outer→inner 过渡（anchor ↔ inset），18 索引
  TL: 4,6,5,7,  TR: 13,15,14,16,  BR: 22,24,23,25,  BL: 31,33,32,34,  闭合: 4,6
  每角 4 索引 = 2 个三角形（anchor→inset 过渡）
  角间跳转三角形覆盖直边的 anchor→inset 区域
  功能：圆弧附近区域，fragment shader 在此计算椭圆距离

Strip 3：内部填充（inset → center），21 索引
  TL: 6,8,7, 跳转: 7,17,  TR: 15,17,16, 跳转: 16,26,  BR: 24,26,25, 跳转: 25,35,  BL: 33,35,34, 闭合: 34,8,6
  每角 3 索引 = 1 个三角形（inset 两点 + center）+ 2 索引跳转到下一角
  四个角的 center 顶点（8,17,26,35）在 vertex shader 中都 snap 到矩形中心同一点
  功能：大面积内部填充，fragment shader 直接返回 coverage=1.0（perPixelControl.x > 0）
```

**Outset 层 45° 方向的 AA 带精度**

每个角有 4 个 outset 顶点而非 2 个，是为了在 45° 方向也有精确的法线外扩。但 outset 1→outset 2 的连线是直线，而圆弧是凸曲线，当半径较小时连线可能"切入"圆弧外扩 1px 区域的内部，导致 45° 处 AA 带不足 1px 宽。

以圆角 `r = 2` 为例验证：圆弧 45° 处的点到 out1→out2 连线的距离约为 0.41px，不足 1px。而大半径 `r = 20` 时约为 4px，远超 1px。

| 半径大小 | out1→out2 到圆弧距离 | AA 带效果 |
|---|---|---|
| 大半径（r ≥ 10） | >> 1px | 完整 1px AA 带 |
| 中等半径（r ≈ 3-10） | ≈ 1px | 基本足够 |
| 小半径（r ≤ 2） | < 1px | 45° 处 AA 带可能稍窄 |

Graphite 接受这个近似，原因有三：
1. 小半径时整个圆角只有几个像素，AA 带窄一点几乎不可感知
2. fragment shader 的距离计算是精确的（通过 Jacobian + 椭圆方程），不依赖 outset 几何的精度——只要三角形覆盖了需要 AA 的像素，fragment shader 就能给出正确的 coverage
3. 极端情况（半径接近 0）圆角退化为直角，outset 顶点重合，AA 带自然回到正确的直线模式
4. 极小半径触发 `kComplexAAInsets` 模式时，vertex shader 会调整顶点位置避免 AA 带塌陷

**统一静态缓冲 + Instance Data 架构**

Graphite 的核心设计：**Rect、RRect、描边矩形、描边圆角矩形、线段、四边形等所有形状共用同一套 36 顶点 + 69 索引的静态缓冲区**，每个形状只上传一组 instance data，通过 `drawInstanced` 一次 draw call 绘制所有形状，零 pipeline 切换。

```
静态 vertex/index buffer（全局唯一，所有形状共享）：
  36 顶点 × sizeof(Vertex)    ← 只创建一次，永不修改
  69 索引 × sizeof(uint16_t)  ← 只创建一次，永不修改

每个形状实例只上传 instance data：
  float4 xRadiiOrFlags   填充 RRect 时存四角 X 半径（值 > 0），其他形状通过特殊负值编码
  float4 radiiOrQuadXs   填充 RRect 时存四角 Y 半径，描边时存圆角半径
  float4 ltrbOrQuadYs    矩形边界 LTRB 或四边形/线段坐标
  float4 center          中心坐标 + 内部填充控制
  float  depth           深度排序值
  uint   ssboIndex       pipeline 数据索引
  float3 mat0/mat1/mat2  local-to-device 3×3 变换矩阵
```

**形状类型判定**：没有单独的 type 字段，vertex shader 通过检查 `xRadiiOrFlags` 的值隐式判定形状类型：

```
xRadiiOrFlags.x > 0        → 填充圆角矩形（四个分量直接是四角 X 半径）
xRadiiOrFlags.x ∈ [-1, 0]  → per-edge AA 四边形
xRadiiOrFlags.x == -1       → 填充矩形（四角方角，半径 = 0）
xRadiiOrFlags.x ≤ -2       → 描边/hairline（再看 .y 细分）
  .y < 0   → hairline 圆角矩形（X 半径 = -2 - xRadiiOrFlags[i]，需解码）
  .y == 0  → 描边矩形/圆角矩形（.z = strokeRadius, .w = joinLimit）
  .y > 0   → 描边/hairline 线段（.z = strokeRadius, .w = capStyle）
```

**各形状类型的 instance data 编码表**：

```
形状              xRadiiOrFlags                    radiiOrQuadXs              ltrbOrQuadYs
────────────────────────────────────────────────────────────────────────────────────────────
填充矩形          [-1, -1, -1, -1]                 [L, R, R, L]              [T, T, B, B]
填充 RRect        [xR_TL, xR_TR, xR_BR, xR_BL]    [yR_TL, yR_TR, yR_BR, yR_BL]  [L, T, R, B]
描边矩形          [-2, 0, strokeRad, joinLim]       [0, 0, 0, 0]              [L, T, R, B]
描边 RRect        [-2, 0, strokeRad, joinLim]       [r_TL, r_TR, r_BR, r_BL]  [L, T, R, B]
Hairline 矩形     [-2, -2, -2, -2]                 [0, 0, 0, 0]              [L, T, R, B]
Hairline RRect    [-2-xR_TL, -2-xR_TR, ...]        [yR_TL, yR_TR, ...]       [L, T, R, B]
描边线段          [-2, 1, strokeRad, capStyle]      [0, 0, 0, 0]              [x0, y0, x1, y1]
Per-edge 四边形   [aa?-1:0 per edge]               [x_TL, x_TR, x_BR, x_BL]  [y_TL, y_TR, y_BR, y_BL]
```

对于填充矩形（半径全 0），vertex shader 中圆角相关计算全部退化：`cornerRadii = (0, 0)`，anchor 和 inset 顶点重合在矩形角上，圆弧区域退化为直角，相关三角形面积为零被 GPU 自动丢弃。

**Vertex Shader 核心逻辑**

vertex shader 根据 `normalScale` 分三种情况放置顶点：

1. `normalScale > 0`（outset 顶点 0-3）：先计算形状边缘，再在 **device space** 沿法线方向外扩 1 像素
   ```glsl
   localPos = (cornerRadii + strokeRadius) * (position + joinScale * position.yx);
   // 用 Jacobian 变换 local normal → device space，然后 normalize 后外扩
   devPos.xy += devPos.z * normalize(nx + ny);  // 乘 w 保证透视正确
   ```

2. `normalScale == 0`（anchor 顶点 4-5）：圆弧的控制线位置，由 `kRoundScale = √2 - 1` 使 anchor 从圆弧端点向凸方向偏移，连线中点恰好落在圆弧上（两端在圆弧外侧）

3. `normalScale < 0`（inset 顶点 6-7-8）：向内收缩
   ```glsl
   float2 insetRadii = cornerRadii + (bidirectionalCoverage ? -strokeRadius : strokeRadius);
   if (miter || insetRadii <= localAARadius) {
       localPos = insetRadii - localAARadius;       // 直角 miter
   } else {
       localPos = insetRadii * position - localAARadius * normal;  // 圆角 inset
   }
   ```
   center 顶点（`centerWeight=1`）在实心填充时 snap 到矩形中心。

关键计算：
- **Edge distances**：`edgeDistances = dy*(xs - localPos.x) - dx*(ys - localPos.y)` — 到四条边的有符号距离
- **Jacobian**：`jacobian = deviceToLocal[col].xy - deviceToLocal[col].z * localPos` — local-to-device 逆变换的 2×2 子矩阵
- **perPixelControl**：编码 fragment shader 的行为（全覆盖 / 实心边缘 / 双向描边覆盖）

**Fragment Shader 核心逻辑**

源码位置：`src/sksl/sksl_graphite_frag.sksl` 中的 `analytic_rrect_coverage_fn`

```glsl
// 快速路径：全覆盖的内部像素
if (perPixelControl.x > 0.0) return half4(1.0);

// device-space 距离已预计算的矩形/四边形
if (perPixelControl.y > 1.0) {
    float2 outerDist = min(edgeDistances.xy, edgeDistances.zw);
    float c = min(outerDist.x, outerDist.y) * coords.w;
    float scale = (perPixelControl.y - 1.0) * coords.w;
    return half4(saturate(scale * (c + coverage_bias(scale))));
}

// 完整的 per-pixel coverage 计算
float2x2 J = float2x2(jacobian) / coords.w;
float2 invGradLen = float2(inverse_grad_len(float2(1,0), J),
                           inverse_grad_len(float2(0,1), J));
float2 outerDist = invGradLen * (strokeParams.x + min(edgeDistances.xy, edgeDistances.zw));
float2 d = float2(min(outerDist.x, outerDist.y), -1.0);

// 四角独立椭圆距离计算
corner_distances(d, J, strokeParams, edgeDistances, xRadii, yRadii);
// ↑ 内部对四个角分别调用 elliptical_distance()

float finalCoverage = scale * (min(d.x + outsetDist, -d.y) + bias);
return half4(saturate(finalCoverage));
```

椭圆距离计算（`$elliptical_distance`）：
```glsl
float2 invR2 = 1.0 / (radii * radii + strokeRadius * strokeRadius);
float2 normUV = invR2 * uv;
float invGradLength = inverse_grad_len(normUV, jacobian);
float f = 0.5 * invGradLength * (dot(uv, normUV) - 1.0);
float width = radii.x * strokeRadius * invR2.x * invGradLength;
return float2(width - f, width + f);  // (外距离, 内距离)
```

**AA / Non-AA 统一处理**

Graphite **不区分** AA 和 Non-AA 路径。当不需要 AA 时（如 `EdgeAAQuad` 的 `edgeFlags == kNone`），将 `aaRadius` 设为 0：
- outset 顶点不向外扩展，AA ramp 带退化为**零面积三角形**
- GPU 光栅化阶段自动跳过零面积三角形（几乎无开销）
- fragment shader 中 `perPixelControl.x > 0` 让内部像素直接返回全覆盖

---

## 4. 对比分析

### 4.1 TGFX 与 Skia GPU 渲染方案对比

| | TGFX 当前 | Skia Ganesh | Skia Graphite |
|---|---|---|---|
| **AA 模式** | 16 顶点 9-patch | 16 顶点 9-patch（EllipticalRRectOp）/ 40 顶点（FillRRectOp） | 36 顶点（统一模板） |
| **Non-AA 模式** | 4 顶点 quad + SDF | 无专用路径（FillRRectOp + kFakeNonAA） | 同上 36 顶点（AA 带退化） |
| **AA 算法** | 椭圆 Sampson 距离 | 椭圆 Sampson 距离 / `x²+y²-1` + `fwidth` | 解析距离场 + Jacobian |
| **距离单位** | 预归一化到单位圆 | 预归一化到单位圆 | 本地坐标 + Jacobian 转设备空间 |
| **四角独立半径** | ❌ 统一 | FillRRectOp ✅ / 其他 ❌ | ✅ `float4 xRadii` + `float4 yRadii` |
| **透视支持** | ❌ CPU 侧 mapPoints | FillRRectOp ❌ / 其他 ❌ | ✅ 完整透视（Jacobian 含 w） |
| **顶点数据位置** | CPU 计算写入 VBO | CPU 写 VBO / FillRRectOp 用 instance | 静态模板 + per-instance data |
| **Pipeline 变体** | AA / Non-AA 各一个 | 多个（Circle/Ellipse/FillRRect） | **唯一一个 pipeline** |
| **合批范围** | 同 AA 模式可合批 | 同 Op 类型可合批 | Rect/RRect/Quad/Line/Stroke 全合批 |
| **Stroke + Complex RRect** | ❌ 不支持 Complex | ❌ 走 Path 回退 | ✅ 原生支持（要求近似圆角） |

### 4.2 Graphite 方案对 TGFX 四角独立半径的参考价值

**值得参考的**：
1. **Fragment shader 的四角独立距离计算**：`$corner_distances` → `$elliptical_distance` 的模式——在 FS 中根据 `edgeDistances` 判断像素所在象限，分别计算四角椭圆距离。这个算法可以嫁接到 TGFX 的任一路径上。
2. **Instance data 编码**：用一组 `float4` 巧妙编码多种形状类型，实现极致合批。
3. **Device-space AA outset**：在 VS 中通过 Jacobian 做精确的 1px outset，比 TGFX 当前在 local space 做 `aaBloat` 更准确。

**不适合直接采用的**：
1. **36 顶点统一模板**：需要 instanced rendering 支持（`drawInstanced` + per-instance attributes），TGFX 当前的渲染架构（`drawIndexed` + 手写顶点）不直接支持。
2. **Jacobian 传递**：需要 fragment shader 支持 `float2x2` varying 和较复杂的矩阵运算，对低端设备有性能风险。
3. **统一 pipeline**：需要重构整个 GeometryProcessor 体系，工作量远超四角半径功能本身。

---

## 5. 设计方案

### 5.1 RRect 数据结构扩展

```cpp
// include/tgfx/core/RRect.h
struct RRect {
  /**
   * Type describes the specialization of the RRect.
   */
  enum class Type {
    Empty,     // empty rect
    Rect,      // all corner radii are zero
    Oval,      // radii fill the rect
    Simple,    // all four corners have the same radii
    NinePatch, // axis-aligned radii (left X == left X, etc.)
    Complex    // arbitrary per-corner radii
  };

  /**
   * Corner index for per-corner radii access.
   */
  enum Corner {
    kUpperLeft = 0,
    kUpperRight = 1,
    kLowerRight = 2,
    kLowerLeft = 3
  };

  Rect rect = {};
  std::array<Point, 4> radii = {};  // [UL, UR, LR, LL], each Point = (xRadius, yRadius)

  Type getType() const;
  bool isEmpty() const;
  bool isRect() const;
  bool isOval() const;
  bool isSimple() const;
  bool isNinePatch() const;
  bool isComplex() const;

  /**
   * Returns the simple radii (only valid when isSimple() or isOval()).
   */
  Point getSimpleRadii() const;

  void setEmpty();
  void setRect(const Rect& rect);
  void setOval(const Rect& oval);
  void setRectXY(const Rect& rect, float radiusX, float radiusY);
  void setNinePatch(const Rect& rect, float leftRad, float topRad,
                    float rightRad, float bottomRad);
  void setRectRadii(const Rect& rect, const std::array<Point, 4>& radii);

  void scale(float scaleX, float scaleY);

  // Inset/outset the rrect
  void inset(float dx, float dy);
  void outset(float dx, float dy);

  bool operator==(const RRect& other) const;
  bool operator!=(const RRect& other) const;

 private:
  /**
   * W3C CSS Background 5.5 section: scale down all radii proportionally so that
   * adjacent corner radii do not exceed the side length.
   */
  bool scaleRadii();

  /**
   * Compute the type from current rect and radii.
   */
  void computeType();

  Type type = Type::Empty;
};
```

**设计决策**：

1. **`std::array<Point, 4>` 而非 `Point radii[4]`**：类型安全，可直接拷贝/比较。
2. **Type 枚举**：后验推断，与 Skia 保持一致。`setXxx()` 方法内部调用 `scaleRadii()` → `computeType()`。
3. **内存布局**：`Rect(4) + std::array<Point,4>(8) + Type(4)` = 52 字节（相比原 24 字节增加 28 字节）。由于 RRect 不是高频创建的小对象（通常存于 Record/Content 中），这个增长可以接受。
4. **向后兼容**：旧的 `radii` 单值成员被替换为 `radii` 数组。需要更新所有引用 `rRect.radii.x / rRect.radii.y` 的代码。对于 Simple 类型，可通过 `getSimpleRadii()` 获取。

### 5.2 半径缩放算法

遵循 W3C CSS Background 规范 Section 5.5 "Overlapping Curves"，与 Skia 的 `SkRRect::scaleRadii()` 对齐：

```
令 f = min(Li/Si)，其中：
  i ∈ {top, right, bottom, left}
  S_top    = radii[UL].x + radii[UR].x
  S_right  = radii[UR].y + radii[LR].y
  S_bottom = radii[LR].x + radii[LL].x
  S_left   = radii[LL].y + radii[UL].y
  L_top = L_bottom = rect.width()
  L_left = L_right = rect.height()
若 f < 1，所有半径乘以 f。
```

关键实现细节：
- 使用 **double** 精度计算 scale 避免大小半径共存时的精度丢失（参考 Skia crbug.com/463920）
- 每条边 `flush_to_zero`：当 `a + b == a` 时强制 `b = 0`（处理浮点精度丢失，参考 Skia crbug.com/850350）
- 缩放后 clamp 负/零值角为 (0, 0)

### 5.3 Path::isRRect() 扩展

当前限制：

```cpp
bool Path::isRRect(RRect* rRect) const {
  SkRRect skRRect = {};
  if (!pathRef->path.isRRect(&skRRect) || !skRRect.isSimple()) {
    return false;  // ← 只接受 Simple
  }
  // ...
}
```

扩展后：

```cpp
bool Path::isRRect(RRect* rRect) const {
  SkRRect skRRect = {};
  if (!pathRef->path.isRRect(&skRRect) || skRRect.isRect()) {
    return false;
  }
  if (rRect) {
    const auto& rect = skRRect.rect();
    rRect->rect.setLTRB(rect.fLeft, rect.fTop, rect.fRight, rect.fBottom);
    for (int i = 0; i < 4; ++i) {
      auto radii = skRRect.radii(static_cast<SkRRect::Corner>(i));
      rRect->radii[i].set(radii.fX, radii.fY);
    }
    rRect->computeType();
  }
  return true;
}
```

同时 `Path::addRRect()` 也需要更新，根据 RRect 的 type 选择 Skia 的构造方式：

```cpp
void Path::addRRect(const RRect& rRect, bool reversed, unsigned startIndex) {
  SkRRect skRRect;
  skRRect.setRectRadii(ToSkRect(rRect.rect),
                        reinterpret_cast<const SkPoint*>(rRect.radii.data()));
  writableRef()->path.addRRect(skRRect, ToSkDirection(reversed), startIndex);
}
```

### 5.4 GPU 渲染适配

这是改动最核心也最复杂的部分。需要同时适配 **AA 模式** 和 **Non-AA 模式** 两条渲染路径。

#### 5.4.1 Non-AA 模式适配（NonAARRectGeometryProcessor）

**方案：传递四角独立半径到 fragment shader**

Non-AA 模式使用 4 顶点 quad + fragment shader SDF。当前传递统一的 `inRadii(Float2)`，需要改为传递四角独立半径。

**顶点属性变更**：

```
原: inRadii (Float2)          → 2 floats (统一 xRadius, yRadius)
新: inCornerRadii (Float4×2)  → 8 floats (四角 xRadii + 四角 yRadii)
```

具体方案：

```
inXRadii (Float4): [UL.x, UR.x, LR.x, LL.x]
inYRadii (Float4): [UL.y, UR.y, LR.y, LL.y]
```

**Fragment Shader 改造**：

当前 SDF 假设四角对称：`q = abs(p - center) - halfSize + radii`。需要改为根据像素所在象限选择对应角的半径：

```glsl
// Determine which corner the pixel is closest to
vec2 p = localCoord - center;
// Select radii based on quadrant
float rx, ry;
if (p.x < 0.0) {
    rx = (p.y < 0.0) ? xRadii[0] : xRadii[3];  // UL or LL
    ry = (p.y < 0.0) ? yRadii[0] : yRadii[3];
} else {
    rx = (p.y < 0.0) ? xRadii[1] : xRadii[2];  // UR or LR
    ry = (p.y < 0.0) ? yRadii[1] : yRadii[2];
}
vec2 radii = vec2(rx, ry);
vec2 q = abs(p) - halfSize + radii;
float d = min(max(q.x / radii.x, q.y / radii.y), 0.0)
         + length(max(q / radii, 0.0)) - 1.0;
```

**顶点数据变更**（RRectsVertexProvider）：

```
原: 每顶点 10 floats (position 2 + localCoord 2 + radii 2 + bounds 4)
新: 每顶点 16 floats (position 2 + localCoord 2 + xRadii 4 + yRadii 4 + bounds 4)
```

#### 5.4.2 AA 模式适配（EllipseGeometryProcessor）

AA 模式使用 9-patch 16 顶点，目前所有顶点共用同一组 `reciprocalRadii`（因为四角相同）。支持四角独立半径后，需要区分处理。

**方案 A：保持 9-patch，per-corner 传递半径（推荐）**

9-patch 布局天然地将 RRect 分成了角+边+中心区域。每个角区域的顶点可以传递该角对应的半径：

```
 TL角顶点 ← reciprocalRadii of UL corner
 TR角顶点 ← reciprocalRadii of UR corner
 BL角顶点 ← reciprocalRadii of LL corner
 BR角顶点 ← reciprocalRadii of LR corner
 边缘/中心顶点 ← 最近角的半径（这些区域内椭圆距离为 nearly-zero，不影响结果）
```

顶点属性保持不变（`inEllipseRadii: Float4`），但**不同角的顶点写入不同的 reciprocalRadii 值**。

关键改造点在 `RRectsVertexProvider::getAAVertices()`：

```
原: 统一 reciprocalRadii[4] 用于所有 16 顶点
新: 按角分配:
    - 行 0 (top):    左列顶点用 UL 半径，右列顶点用 UR 半径
    - 行 1 (top-inner): 同上
    - 行 2 (bottom-inner): 左列用 LL 半径，右列用 LR 半径
    - 行 3 (bottom):  同上
    - 中间列顶点: 使用该行对应的半径
```

对于 **9-patch NinePatch 类型**，四个角区域大小可以不同（左上、右上、左下、右下的 xRadius/yRadius 各异），需要调整 9-patch 的切分点：

```
原: xOuterRadius = xRadius (统一)
新: 左侧 xOuterRadius = max(UL.x, LL.x)
    右侧 xOuterRadius = max(UR.x, LR.x)
    上侧 yOuterRadius = max(UL.y, UR.y)
    下侧 yOuterRadius = max(LL.y, LR.y)
```

Fragment shader 无需修改——它已经使用 per-vertex 传入的 `ellipseRadii` varying，只要正确地在顶点层面分配半径即可。

**方案 B：转为 Graphite 风格的 per-pixel 四角评估**

改造 fragment shader 为 Skia Graphite 的 `analytic_rrect_coverage_fn` 风格，每像素评估四个角。这是更彻底的改造，性能更优（可以合并更多不同 RRect 类型），但工作量显著增大。

**决策**：采用方案 A（per-corner 顶点半径），因为：
- 改动量可控，复用现有 9-patch 拓扑和 fragment shader
- 保持了现有的批处理架构
- 方案 B 可作为后续优化迭代

#### 5.4.3 Stroke 处理

对于 Stroke 模式，当前已使用椭圆距离场。四角独立半径的 stroke 需要：
- Non-AA stroke: 与 fill 类似，per-corner 选择半径后计算内外 SDF
- AA stroke: 9-patch 中每个角区域使用该角的 outer/inner 半径

注意：当四角半径差异较大时，stroke 的内半径可能在某些角变为负值（即内部完全被 stroke 覆盖）。需要 per-corner 独立计算 `innerRadius = max(cornerRadius - strokeWidth, 0)`。

### 5.5 Rectangle VectorElement 扩展

当前 `Rectangle` 使用单一 `float _roundness`。扩展为支持四角独立值：

```cpp
class Rectangle : public VectorElement {
 public:
  // 保留原有统一设置（四角相同）
  float roundness() const;
  void setRoundness(float value);

  // 新增：四角独立设置
  const std::array<float, 4>& cornerRoundness() const;
  void setCornerRoundness(const std::array<float, 4>& values);

 private:
  std::array<float, 4> _roundness = {0.f, 0.f, 0.f, 0.f};
  // ...
};
```

`apply()` 方法改造：

```cpp
void Rectangle::apply(VectorContext* context) {
  if (_cachedShape == nullptr) {
    auto halfWidth = _size.width * 0.5f;
    auto halfHeight = _size.height * 0.5f;
    auto rect = Rect::MakeXYWH(_position.x - halfWidth, _position.y - halfHeight,
                                _size.width, _size.height);
    Path path;
    if (allRoundnessEqual()) {
      auto r = std::min({_roundness[0], halfWidth, halfHeight});
      path.addRoundRect(rect, r, r, _reversed, 2);
    } else {
      std::array<Point, 4> radii;
      for (int i = 0; i < 4; ++i) {
        auto r = std::min({_roundness[i], halfWidth, halfHeight});
        radii[i] = {r, r};
      }
      path.addRoundRect(rect, radii, _reversed, 2);
    }
    _cachedShape = Shape::MakeFrom(path);
  }
  context->addShape(_cachedShape);
}
```

### 5.6 关联代码适配

以下调用点需要从 `rRect.radii.x / rRect.radii.y` 迁移到新的 API：

| 文件 | 位置 | 当前用法 | 迁移方案 |
|------|------|----------|----------|
| `Canvas.cpp` | `drawRRect()` | `rRect.radii.x/y < 0.5f` | 检查所有 radii 是否全部 < 0.5f |
| `Canvas.cpp` | `UseDrawPath()` | `radii.x, radii.y` | 取最大的角半径判断 |
| `RRectsVertexProvider.cpp` | `getAAVertices()` | `rRect.radii.x/y` | 遍历四角 |
| `RRectsVertexProvider.cpp` | `getNonAAVertices()` | `rRect.radii.x/y` | 写入四角独立半径 |
| `RRectsVertexProvider.cpp` | `ApplyScales()` | `swap(rRect.radii.x, rRect.radii.y)` | 对每个角做 swap |
| `RRectContent.cpp` | `onHasSameGeometry()` | `rRect.radii == other.radii` | 使用 `operator==` |
| `RRectsContent.cpp` | `onHasSameGeometry()` | 同上 | 同上 |
| `LayerRecorder.cpp` | `addRRect()` | `rRect.isRect()` | 不变（isRect 逻辑不变） |
| `SVGExportContext.cpp` | `addRoundRectAttributes()` | `rrect.radii.x/y` | 传递四角半径 |

### 5.7 合批策略

当前 RRect 合批条件（`canAppend`）：相同 clip、相同 brush、相同 stroke、RRect 数量 < 1024。

四角独立半径 **不影响合批能力**，因为：
- AA 模式：半径已嵌入顶点数据（per-vertex varying），不同 RRect 可以有不同半径
- Non-AA 模式：半径同样嵌入顶点数据
- GeometryProcessor 的 key 不依赖具体半径值

唯一需要确认的是：不同类型（Simple vs Complex）的 RRect 是否仍可合批——答案是可以，因为 shader 已经支持 per-vertex 半径。

---

## 6. 数据流总览

```
用户 API                     内部数据               GPU 数据（顶点属性）

RRect {                      RRect {
  rect,                        rect,
  radii[4] (per-corner),       radii[4],              ──→ AA: reciprocalRadii[4] per corner-vertex
  type                         type                       NonAA: xRadii(Float4) + yRadii(Float4)
}                            }
       │
       ▼
  Path::addRRect()
  (→ SkRRect::setRectRadii)
       │
       ▼
  Path::isRRect()            ←── 扩展：支持 NinePatch/Complex
  (← SkRRect with any type)
       │
       ▼
  Canvas::drawRRect()
       │
       ▼
  OpsCompositor::drawRRect()
  (收集 RRectRecord)
       │
       ▼
  RRectsVertexProvider
  (按角分配半径到顶点)
       │
       ├──→ AA:    EllipseGeometryProcessor (9-patch, per-corner reciprocalRadii)
       └──→ NonAA: NonAARRectGeometryProcessor (quad, per-corner xRadii + yRadii in fragment)
```

---

## 7. 实现步骤

### Phase 1：核心数据结构（低风险，高优先级）

1. **扩展 `RRect` 结构体**
   - 将 `Point radii` 改为 `std::array<Point, 4> radii`
   - 添加 `Type` 枚举和 `type` 成员
   - 实现 `setRectRadii()`、`setNinePatch()`、`scaleRadii()`、`computeType()`
   - 保留并适配 `setRectXY()`、`setOval()` 等已有方法
   - 实现 `operator==`、`operator!=`

2. **全量适配编译错误**
   - 搜索所有 `rRect.radii.x`、`rRect.radii.y` 引用
   - Simple 类型继续通过 `getSimpleRadii()` 或 `radii[0]` 访问
   - 更新 `Path::addRRect()`：统一使用 `setRectRadii`

### Phase 2：Path 层适配

3. **扩展 `Path::isRRect()`**
   - 移除 `!skRRect.isSimple()` 限制
   - 转换 `SkRRect` 的四角半径到 TGFX `RRect` 的 `radii[4]`

4. **更新 `Path::addRRect()`**
   - 使用 `SkRRect::setRectRadii()` 替换 `SkRRect::MakeRectXY()`

### Phase 3：GPU Non-AA 渲染（中等风险）

5. **NonAARRectGeometryProcessor 属性扩展**
   - `inRadii(Float2)` → `inXRadii(Float4)` + `inYRadii(Float4)`
   - 更新 `getNonAAVertices()` 写入四角独立半径
   - 更新 `vertexCount()` 计算

6. **Non-AA Fragment Shader 改造**
   - 实现 per-quadrant 半径选择
   - 更新 SDF 计算使用选定的角半径
   - Stroke 模式同步适配

### Phase 4：GPU AA 渲染（高风险，需仔细测试）

7. **AA 9-patch 顶点生成改造**
   - `getAAVertices()` 按角分配 reciprocalRadii
   - 调整 9-patch 切分点（左右/上下可以不同的 OuterRadius）
   - 保持 fragment shader 不变（已使用 per-vertex varying）

8. **Stroke AA 模式适配**
   - Per-corner 计算 inner/outer radii
   - 保持 9-patch 拓扑和索引缓冲不变

### Phase 5：Layer 系统

9. **Rectangle VectorElement 扩展**
   - 添加 `cornerRoundness` 四值接口
   - 更新 `apply()` 生成四角独立半径 Path

10. **RRectContent / RRectsContent 适配**
    - `onHasSameGeometry()` 比较四角半径
    - `getFilledPath()` 使用更新后的 `addRRect()`

### Phase 6：Canvas 层适配

11. **Canvas::drawRRect() 适配**
    - 更新圆角过小的判断（检查所有角）
    - 更新 `UseDrawPath()` 的 stroke 判断逻辑
    - 确保 `drawPath()` → `isRRect()` 的反向优化路径畅通

### Phase 7：辅助功能

12. **SVG 导出适配**
    - `SVGExportContext` 中 `addRoundRectAttributes()` 输出四角独立 `rx`/`ry`

---

## 8. 测试计划

### 8.1 单元测试

- **RRect 数据结构测试**
  - `setRectXY` → Type == Simple
  - `setOval` → Type == Oval
  - `setNinePatch` → Type == NinePatch
  - `setRectRadii` 任意值 → Type == Complex
  - 半径缩放：相邻角半径超过边长时正确缩放
  - `isRect()`, `isOval()`, `isSimple()` 等查询方法
  - `scale()`, `inset()`, `outset()` 操作

- **Path 互操作测试**
  - `Path::addRRect(complexRRect)` → `Path::isRRect()` 返回 true 且还原正确
  - NinePatch/Complex RRect 的 Path roundtrip

- **渲染正确性测试**
  - 四角相同半径 → 与改动前结果一致（回归测试）
  - 四角不同半径 fill → 视觉正确
  - 四角不同半径 stroke → 视觉正确
  - 极端情况：某些角半径为 0（方角）、某些角半径为 oval
  - 合批：多个不同半径 RRect 合批绘制

### 8.2 性能测试

- 合批数量：确认不同半径 RRect 仍可合批
- 顶点数据增量：Non-AA 从 10 floats/vertex 增加到 16 floats/vertex 的带宽影响
- Draw call 数量对比：四角不同 RRect 从 shape path 走到 RRect 快速路径后的 draw call 减少

---

## 9. 风险评估

| 风险 | 等级 | 缓解策略 |
|------|------|----------|
| AA 9-patch 切分点不对齐导致接缝 | 高 | 仔细计算每行每列的 OuterRadius，确保角区域完全覆盖椭圆范围 |
| Non-AA SDF 在角边界处不连续 | 中 | 使用 step 函数（Non-AA 本身无抗锯齿），在象限切换处天然连续 |
| 半径缩放 double 精度差异 | 低 | 与 Skia 实现对齐，复用其测试用例 |
| 顶点数据增大影响带宽 | 低 | Non-AA 增加 6 floats/vertex，对于通常的 4 顶点 quad 影响有限 |
| `RRect` ABI 变化导致序列化问题 | 低 | TGFX 不对 RRect 做跨版本序列化 |

---

## 10. 参考资料

### 源码路径

**TGFX**：
- `include/tgfx/core/RRect.h` — RRect 数据结构定义
- `src/core/RRect.cpp` — RRect 方法实现
- `src/core/Path.cpp` — `Path::isRRect()` / `Path::addRRect()`
- `src/gpu/ops/RRectDrawOp.cpp` / `.h` — RRect GPU 绘制操作
- `src/gpu/RRectsVertexProvider.cpp` / `.h` — AA/Non-AA 顶点生成
- `src/gpu/processors/EllipseGeometryProcessor.cpp` / `.h` — AA GeometryProcessor
- `src/gpu/processors/NonAARRectGeometryProcessor.cpp` / `.h` — Non-AA GeometryProcessor
- `src/gpu/glsl/processors/GLSLEllipseGeometryProcessor.cpp` — AA fragment shader 生成
- `src/gpu/glsl/processors/GLSLNonAARRectGeometryProcessor.cpp` — Non-AA fragment shader 生成
- `src/gpu/GlobalCache.cpp` — 索引缓冲区创建（`OverstrokeRRectIndices`）
- `src/gpu/OpsCompositor.cpp` — RRect 合批收集
- `src/layers/contents/RRectContent.cpp` / `RRectsContent.cpp` — Layer 系统 RRect 内容

**Skia Ganesh**（本地路径 `/Users/chenjie/Desktop/Develop/skia`）：
- `src/gpu/ganesh/SurfaceDrawContext.cpp` — `drawRRect()` 分发入口（第 1003 行）
- `src/gpu/ganesh/ops/GrOvalOpFactory.cpp` — `CircularRRectOp` / `EllipticalRRectOp` 实现
- `src/gpu/ganesh/ops/FillRRectOp.cpp` — `FillRRectOp` 实现（40 顶点，四角独立半径）
- `src/gpu/ganesh/geometry/GrShape.cpp` — `asPath()` RRect → Path 按需转换
- `src/gpu/ganesh/geometry/GrStyledShape.h` — `GrStyledShape` 包装

**Skia Graphite**：
- `src/gpu/graphite/render/AnalyticRRectRenderStep.cpp` — 36 顶点模板 + instance data 编码
- `src/gpu/graphite/Device.cpp` — `drawRRect()` + `chooseRenderer()` + `is_simple_shape()`
- `src/sksl/sksl_graphite_vert.sksl` — `analytic_rrect_vertex_fn` 顶点放置 + Jacobian
- `src/sksl/sksl_graphite_frag.sksl` — `analytic_rrect_coverage_fn` + `$corner_distances` + `$elliptical_distance`

### 参考论文/规范

- [VanVerth GDC 2015 - Drawing Antialiased Ellipse](https://www.essentialmath.com/GDC2015/VanVerth_Jim_DrawingAntialiasedEllipse.pdf) — 椭圆 Sampson 距离近似算法
- [W3C CSS Backgrounds and Borders Level 3, Section 5.5](https://www.w3.org/TR/css-backgrounds-3/#corner-overlap) — 圆角半径缩放规范
- [Skia Graphite AnalyticRRect 设计文档](https://docs.google.com/presentation/d/1MCPstNsSlDBhR8CrsJo0r-cZNbu-sEJEvU9W94GOJoY/edit?usp=sharing) — Graphite 36 顶点几何布局图解
