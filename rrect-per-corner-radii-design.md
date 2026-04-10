# RRect 四角独立半径扩展 — 技术设计文档

## 1. 背景

### 1.1 背景

TGFX 当前的 `RRect` 结构仅支持 **Simple 模式**——四个角共用一组 `(radiusX, radiusY)` 半径。然而在实际业务中，四角不同圆角半径是非常常见的需求（如对话气泡、卡片 UI），目前业务方只能将其转为 Path 后再提交，导致：

1. **无法走 GPU RRect 快速路径**：`Path::isRRect()` 要求 `skRRect.isSimple()` 才返回 true，四角不同半径的 RRect Path 只能走通用的 Shape 光栅化/三角化流程。
2. **无法合批**：通用 Shape 的合批能力远弱于 RRect 专用的合批（最多 1024 个 RRect/draw call）。
3. **API 不完整**：相比 Skia 的 `SkRRect`（支持 Empty/Rect/Oval/Simple/NinePatch/Complex 六种类型），TGFX 做了过度简化。

### 1.2 目标

- 扩展 `RRect` 数据结构，支持四角独立椭圆半径
- `Path::isRRect()` 能识别 Complex 类型（含 Skia NinePatch）的 RRect
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
  - Type 枚举（区分 Simple/Complex，用于 GPU 路径选择）
  - `std::array<Point, 4> radii`（四角独立半径数据）
  - W3C 完整半径缩放（检查四条边各自相邻角半径之和，当前简化版仅适用于统一半径）
  - `setRectRadii()` 等新 API

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

#### 2.3.3 两条路径的对比总结

AA 和 Non-AA 使用不同算法的根本原因：Non-AA 的 SDF 用 `step(d, 0.0)` 做硬边判断，只关心距离正负号，无需设备空间单位；AA 需要平滑的 coverage 过渡（`clamp(0.5 - d, 0, 1)`），要求距离值是设备空间像素单位。TGFX AA 路径选择椭圆隐式方程 `f/|∇f|`（Sampson 距离），自带解析梯度信息，配合 9-patch 分区让中心和边缘区域几乎零计算开销。

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
- **半径缩放**（`scaleRadii()`）：当相邻两个角的半径之和超过矩形边长时，等比例缩小所有半径使其不重叠（遵循 W3C CSS Background 5.5 节）。使用 double 精度计算避免大小半径共存时的精度丢失。详见 5.2 节。

**六种 Type 的含义**

| Type | 含义 |
|---|---|
| `kEmpty` | 空矩形（`rect.isEmpty()`） |
| `kRect` | 纯矩形，四角半径全为 0 |
| `kOval` | 椭圆，每角半径 = 半边长（`radii[i].x == width/2 && radii[i].y == height/2`） |
| `kSimple` | 四角统一半径，x 和 y 可以不同但四角的 (x,y) 完全一致（`radii[0] == radii[1] == radii[2] == radii[3]`） |
| `kNinePatch` | 轴对齐半径：左两角 X 相同、右两角 X 相同、上两角 Y 相同、下两角 Y 相同 |
| `kComplex` | 完全任意的四角半径，不满足以上任何条件 |

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
    ├─ FillRRectOp ← 仅 fill，但 RRect 类型最宽松，drawInstanced 性能更优
    │
    │ 第 3 优先级
    ├─ EllipticalRRectOp ← 支持 stroke，但要求 Simple
    │
    │ 全部失败
    └─ drawShapeUsingPathRenderer（多级 PathRenderer 选择）
```

各 RRect 类型按此优先级逐级尝试，每种类型只会命中一个 Op：

| RRect 类型 + 样式 | 命中的 Op |
|---|---|
| Simple + 圆角 + Fill | CircularRRectOp |
| Simple + 圆角 + Stroke | CircularRRectOp |
| Simple + 椭圆角 + Fill | FillRRectOp |
| Simple + 椭圆角 + Stroke | EllipticalRRectOp |
| NinePatch/Complex + Fill | FillRRectOp |
| NinePatch/Complex + Stroke | Path 回退 |

FillRRectOp 优先级高于 EllipticalRRectOp 的原因：两者对 Simple + Fill 都能处理，但 FillRRectOp 使用 drawInstanced 机制——40 个静态顶点常驻 GPU 缓存，每个 RRect 只上传约 80 字节 instance data，而 EllipticalRRectOp 每个 RRect 需 CPU 计算并上传 16 个顶点（约 512 字节）。合批大量 RRect 时，FillRRectOp 的 CPU 开销和数据传输量显著更小。

#### 3.2.1 Op 1：`CircularRRectOp`

**适用条件**（全部满足）：
- `aaType == Coverage`（必须 coverage AA，不能是 MSAA）
- `rrect.isSimple()`（四角统一半径）
- `radii.fX == radii.fY`（X/Y 半径相等，即圆角非椭圆角）
- `viewMatrix.isSimilarity()`（等比缩放 + 旋转，不能有非均匀缩放）
- `viewMatrix.rectStaysRect()`（轴对齐）

**几何布局**：4×4 网格 = 16 顶点

```
    col0  col1       col2  col3
     ┌─────┬──────────┬─────┐  row0
     │ TL  │   上边   │ TR  │
     ├─────┼──────────┼─────┤  row1
     │     │          │     │
     │左边 │   中心   │右边 │
     │     │          │     │
     ├─────┼──────────┼─────┤  row2
     │ BL  │   下边   │ BR  │
     └─────┴──────────┴─────┘  row3
```

**三角形拓扑**：每个 quad = 2 个三角形 = 6 个索引，根据 Fill/Stroke 模式使用不同的 quad 组合：

- `kFill`：9 个 quad（全部区域），54 索引
- `kStroke`：8 个 quad（去掉中心 quad，因为描边中心是空的），48 索引
- `kOverstroke`（`strokeWidth > 2 * cornerRadius`）：额外 8 个顶点覆盖角区域内侧，24 顶点，66 索引

类型判定逻辑：
```
devStrokeWidth <= 0                              → kFill
strokeOnly && cornerR - halfStroke >= 0          → kStroke
strokeOnly && cornerR - halfStroke < 0           → kOverstroke
strokeOnly && strokeWidth > min(width, height)   → kFill（描边完全覆盖矩形，退化）
```

**顶点坐标计算**

CPU 侧根据以下参数计算 16 个顶点的位置和属性，计算结果写入 VBO，GPU 只接收最终的顶点数据：

- `R`（`devRect`）：RRect 的矩形边界，已通过 viewMatrix 变换到设备空间
- `r`（`devRadius`）：圆角半径，已变换到设备空间
- `h`（`halfStrokeWidth`）：半描边宽度（Fill 时为 0）
- `a`（`aaBloat`）：AA 外扩量（Coverage AA 时通常为 0.5，MSAA 时为 √2）

每个顶点写入 VBO 的数据（即 GPU 侧的顶点属性）为 `position(float2)` + `circleEdge(float4)`，具体计算如下：

**`position` 计算**：4 列 × 4 行 = 16 个顶点位置

```
outerRadius = r + h + a

position.x 的 4 个取值（列）：
  col0 = R.left  - h - a          // 外扩后的左边界
  col1 = R.left  + r              // 左侧圆心 x
  col2 = R.right - r              // 右侧圆心 x
  col3 = R.right + h + a          // 外扩后的右边界

position.y 的 4 个取值（行）：
  row0 = R.top    - h - a         // 外扩后的上边界
  row1 = R.top    + r             // 上侧圆心 y
  row2 = R.bottom - r             // 下侧圆心 y
  row3 = R.bottom + h + a         // 外扩后的下边界
```

col1/col2/row1/row2 是 9-patch 的内边界线，只取决于圆角半径 `r`，**不受描边宽度 `h` 和 AA 外扩 `a` 影响**——内边界线恰好穿过圆角的圆心。

**`circleEdge` 计算**：每个顶点的 4 个分量

```
circleEdge.xy（offset）：
  col0 → -1,  col1 → 0,  col2 → 0,  col3 → +1    // x 分量
  row0 → -1,  row1 → 0,  row2 → 0,  row3 → +1    // y 分量

circleEdge.z = outerRadius（所有顶点相同）
circleEdge.w = innerRadius（Fill/Stroke 不同，见下文）
```

varying 插值后 `circleEdge.xy` 在各区域的行为：
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

#### 3.2.2 Op 2：`FillRRectOp`

**适用条件**：
- `style.isSimpleFill()`（纯填充，无描边）
- `caps->drawInstancedSupport()`（硬件支持实例化渲染）
- `!viewMatrix.hasPerspective()`（不支持透视）
- RRect 尺寸 < 1e6（防数值溢出）
- **不要求 `isSimple()`**，任意 RRect 类型（Simple / NinePatch / Complex）都接受

**几何布局**：40 个静态顶点 = 8 外八边形 + 8 内八边形 + 24 角弧顶点（每角 6 个）

```
       o──────────────o          o = 外八边形顶点（8 个）
      ╱ · ·        · · ╲        · = 角弧顶点（每角 6 个）
     o · ·          · · o        ● = 内八边形顶点（8 个）
     │ · ●──────────● · │
     │   │          │   │        内八边形实际是切角形状，
     │   │          │   │        图中用矩形近似表示
     │ · ●──────────● · │
     o · ·          · · o
      ╲ · ·        · · ╱
       o──────────────o
```

**三角形拓扑**：Primitive type 为 Triangles（独立三角形，非 Triangle Strip），共 90 个索引（30 个三角形），通过 `drawIndexedInstanced` 绘制：

- 内八边形：6 个三角形 = 18 索引（实心填充）
- AA 边：4 条边 × 2 个三角形 = 24 索引（线性 AA 过渡）
- 角弧：4 个角 × 4 个三角形 = 48 索引（椭圆距离 AA）

外八边形和内八边形在直边部分距离均匀，线性插值即可做 AA。但在角区域，两个八边形的切角边之间存在三角形空白——没有几何覆盖，GPU 不会光栅化这些像素。角弧的 6 个顶点填充这个间隙：每个角分为外弧 3 个 + 内弧 3 个，分布在 0°、45°、90° 三个方向（角的起点、中间、终点），内外两层顶点配对形成三角形带。fragment shader 在此区域用椭圆距离方程计算圆弧的精确 coverage。没有角弧顶点，圆角区域不会被光栅化，形状会退化为八边形。仅支持 Fill——在 FillRRectOp 中实现 Stroke 需要处理内外双边缘计算、overstroke 退化、内半径为零等复杂情况，而简单 RRect 的 Stroke 已有其他 Op 处理，复杂 RRect 的 Stroke 可回退 Path，实际未覆盖的场景极少，不值得增加这些复杂度。

Instance data 包含 `radii_x(float4)` + `radii_y(float4)`，**天然支持四角独立半径**。
Fragment shader 用 `x² + y² - 1` 单位圆方程 + `fwidth()` 硬件导数做 AA。

**顶点坐标计算**

与 CircularRRectOp/EllipticalRRectOp 在 CPU 侧手写 VBO 不同，FillRRectOp 的 40 个顶点是**预定义的静态模板**（全局唯一，所有 RRect 共用），vertex shader 根据 per-instance data 动态计算最终位置。

**静态顶点模板**：每个顶点携带以下属性（CPU 侧预定义，不随 RRect 变化）：

- `radii_selector(float4)`：one-hot 向量，标识当前顶点属于哪个角（如 `(1,0,0,0)` = TL）
- `corner(float2)`：归一化角方向，取值 `(-1,-1)` / `(+1,-1)` / `(+1,+1)` / `(-1,+1)` 对应四个角
- `radius_outset(float2)`：沿圆角半径方向的偏移（角弧顶点非零，直边顶点为零）
- `aa_bloat_direction(float2)`：AA 外扩方向
- `coverage(float)`：内八边形顶点 = 1（全覆盖），外八边形顶点 = 0（AA 边界），角弧外弧顶点 = 0，角弧内弧顶点 = 1
- `is_linear_coverage(float)`：直边区域 = 1（线性 AA），角弧区域 = 0（椭圆 AA）

三种顶点的代表性模板值（以左上角 TL 为例）：

```
类型          radii_selector  corner    radius_outset  aa_bloat_dir  coverage  is_linear
内八边形(左)  (0,0,0,1)       (-1,+1)   (0,-1)         (+1,0)        1         1
外八边形(左)  (0,0,0,1)       (-1,+1)   (0,-1)         (-1,0)        0         1
角弧外弧(0°)  (1,0,0,0)       (-1,-1)   (0,-1)         (-1,-1)       0         0
角弧外弧(45°) (1,0,0,0)       (-1,-1)   (-1,-1)        (-1,-1)       0         0
角弧内弧(0°)  (1,0,0,0)       (-1,-1)   (0,-1)         (0,0)         1         0
角弧内弧(45°) (1,0,0,0)       (-1,-1)   (-1,-1)        (0,0)         1         0
```

内/外八边形的 `radius_outset = (0, -1)` 表示顶点在边方向上无圆弧偏移。角弧顶点的 `radius_outset` 有 `(0,-1)`（0°）和 `(-1,-1)`（45°）等值，沿圆弧方向分布。`aa_bloat_direction` 控制外扩方向：外八边形和角弧外弧向外扩（用于 AA），内八边形和角弧内弧不外扩（`(0,0)` 或向内）。

**per-instance data**：每个 RRect 上传一份（约 80 字节）：

- `radii_x(float4)`：四角 X 半径 `[TL, TR, BR, BL]`
- `radii_y(float4)`：四角 Y 半径 `[TL, TR, BR, BL]`
- `skew(float4)`：2×2 变换矩阵（viewMatrix × 矩形归一化矩阵）
- `translate(float2)`：设备空间平移

**Vertex shader 计算公式**（内八边形、外八边形、角弧三种顶点共用同一套公式，行为差异由模板属性值驱动）：

```
// 1. 通过 radii_selector 从 instance data 提取当前角的半径
radii_and_neighbors = radii_selector * float4x4(radii_x, radii_y, radii_x.yxwz, radii_y.wzyx);
radii = radii_and_neighbors.xy;

// 2. 计算设备空间的 AA 外扩量
pixellength = inversesqrt(float2(dot(skew.xz, skew.xz), dot(skew.yw, skew.yw)));
aa_bloatradius = axiswidths * pixellength * 0.5;

// 3. 计算归一化空间中的顶点位置
aa_outset = aa_bloat_direction * aa_bloatradius;
vertexpos = corner + radius_outset * radii + aa_outset;
// corner 确定角方向，radius_outset * radii 沿圆弧偏移，aa_outset 做 AA 外扩

// 4. 变换到设备空间
skewmatrix = float2x2(skew.xy, skew.zw);
devcoord = vertexpos * skewmatrix + translate;
```

`corner` 取值 `±1` 使顶点在归一化空间 `[-1, +1]` 中分布，`skewmatrix` 同时完成从归一化空间到矩形实际尺寸的缩放和 viewMatrix 变换。

#### 3.2.3 Op 3：`EllipticalRRectOp`

**适用条件**：
- `aaType == Coverage` 或 `dynamicMSAA`
- `rrect.isSimple()`（四角统一半径）
- `viewMatrix.rectStaysRect()`（轴对齐）
- 半径 ≥ 0.5px（太小时 9-patch 内部会有 fractional coverage）

**几何布局**：4×4 网格 = 16 顶点，9 个 quad（与 CircularRRectOp 相同的 9-patch 布局）

```
    col0  col1       col2  col3
     ┌─────┬──────────┬─────┐  row0
     │ TL  │   上边   │ TR  │
     ├─────┼──────────┼─────┤  row1
     │     │          │     │
     │左边 │   中心   │右边 │
     │     │          │     │
     ├─────┼──────────┼─────┤  row2
     │ BL  │   下边   │ BR  │
     └─────┴──────────┴─────┘  row3
```

使用 `EllipseGeometryProcessor`，fragment shader 用椭圆隐式方程 + Sampson 距离：

**三角形拓扑**：与 CircularRRectOp 相同——Fill 时 9 个 quad = 54 索引，Stroke 时去掉中心 quad = 48 索引。

```glsl
float test = dot(offset, offset) - 1.0;
float2 grad = 2.0 * offset * ellipseRadii.xy;
float edgeAlpha = saturate(0.5 - test * inversesqrt(dot(grad, grad)));
```
支持 fill 和 stroke，支持椭圆角（X/Y 半径不同）。**这就是 TGFX AA 路径的来源。**
参考论文：[VanVerth GDC 2015 - Drawing Antialiased Ellipse](https://www.essentialmath.com/GDC2015/VanVerth_Jim_DrawingAntialiasedEllipse.pdf)

椭圆角的 Stroke 处理使用**放大/缩小椭圆近似等距线**——直接将描边宽度加减到 X/Y 半径上：

```
外边缘半径 = (a + halfStrokeWidth, b + halfStrokeWidth)
内边缘半径 = (a - halfStrokeWidth, b - halfStrokeWidth)
```

这不是精确的等距线，中间区域会有偏差（详见 3.3 节分析），但在实际业务中通常可接受：圆角半径远大于描边宽度时偏差极小，且椭圆 Sampson 距离本身已是近似算法，再叠加一层等距线近似在视觉上很难察觉。TGFX 沿用了这个近似方案。

#### 3.2.4 Path 回退逻辑

当所有 RRect 快速路径 Op 都失败时，Ganesh 调用 `drawShapeUsingPathRenderer`。`GrStyledShape` 内部直接存储 `SkRRect` 对象（不立即转 Path），RRect → Path 的转换通过 `GrShape::asPath()` 按需发生（调用 `SkPathBuilder::addRRect()` 生成 `moveTo` + `conicTo` 封闭路径）。

内部是一个多级 PathRenderer 选择链：先尝试硬件 tessellation → `simplify()` 重试简单形状路径 → 原始 shape 查找 PathRenderer → apply path effect → stroke 轮廓化 → 最终兜底 CPU 光栅化。因此 **Complex RRect + Stroke** 的实际路径通常是：硬件 tessellation → 不支持则 stroke 轮廓化为填充 Path → 三角化或 stencil 绘制 → CPU 光栅化兜底。

**TGFX drawPath 与 Ganesh Path 回退的对比**

| | TGFX drawPath | Ganesh drawShapeUsingPathRenderer |
|---|---|---|
| **策略数** | 基本 1 种（CPU 三角化） | 5 种 PathRenderer 按优先级选择 |
| **GPU tessellation** | ❌ 无 | ✅ `TessellationPathRenderer`（需硬件支持） |
| **GPU stencil** | ❌ 无 | ✅ `StencilAndCoverPathRenderer` |
| **CPU 三角化** | ✅ `PathTriangulator::ToTriangles` | ✅ `TriangulatingPathRenderer` |
| **CPU 光栅化为纹理** | ✅（复杂路径备选） | ✅ `SoftwarePathRenderer`（兜底） |
| **凸路径优化** | ❌ 无专门路径 | ✅ `AAConvexPathRenderer` |
| **RRect 识别回快速路径** | ✅ `path.isRRect()` → RRectDrawOp | ✅ `simplify()` → RRect Op |

这也是本次四角独立半径改造的核心动机之一：让四角不同半径的 RRect 走 GPU 快速路径（`RRectDrawOp`），避免回退到 CPU 三角化。

### 3.3 Skia Graphite（新后端，Chrome 136+ Mac 已上线）

Graphite 采用**完全不同的架构**——统一 pipeline，AA/Non-AA 不分路径。

**核心设计：`AnalyticRRectRenderStep`**

源码位置：`src/gpu/graphite/render/AnalyticRRectRenderStep.cpp`

**几何布局：36 顶点（每角 9 顶点 × 4 角），69 索引**

每个角的 9 个顶点分三层。表中的 `position` 和 `normal` 定义在抽象的**角模板空间**中：`(1,0)` 表示"沿该角的第一条相邻边方向"，`(0,1)` 表示"沿第二条相邻边方向"。四个角共用同一套模板，vertex shader 通过每个角的边向量基底（xAxis, yAxis）将模板空间映射到实际本地坐标。对于轴对齐矩形（屏幕坐标系，Y 轴向下），各角的基底为：

```
角   corner_point   xAxis（模板 x 方向）   yAxis（模板 y 方向）
TL   (0, 0)         (-1, 0) 向左           (0, -1) 向上
TR   (W, 0)         (0, -1) 向上           (1, 0) 向右
BR   (W, H)         (1, 0) 向右            (0, 1) 向下
BL   (0, H)         (0, 1) 向下            (-1, 0) 向左
```

xAxis 和 yAxis 都指向矩形**外侧**（即从椭圆圆心指向圆弧方向）。`position = (1,0)` 映射到 xAxis，`position = (0,1)` 映射到 yAxis。

```
ID  position  normal        normalScale  centerWeight  位置
0   (1,0)     (1, 0)        +1 outset    0             外扩层，x 轴端点
1   (1,0)     (√2/2,√2/2)   +1 outset    0             外扩层，x 轴端点（45°法线）
2   (0,1)     (√2/2,√2/2)   +1 outset    0             外扩层，y 轴端点（45°法线）
3   (0,1)     (0, 1)        +1 outset    0             外扩层，y 轴端点
4   (1,0)     (√2/2,√2/2)    0 anchor    0             圆弧外侧控制点，x 轴端点
5   (0,1)     (√2/2,√2/2)    0 anchor    0             圆弧外侧控制点，y 轴端点
6   (1,0)     (1, 0)        -1 inset     0             内缩层，x 轴端点
7   (0,1)     (0, 1)        -1 inset     0             内缩层，y 轴端点
8   (1,0)     (1, 0)        -1 inset     1 center      矩形中心
```

**Vertex Shader 坐标计算公式**

Vertex shader 分三步计算顶点最终位置。公式中的 `strokeRadius` 在填充时为 0，描边时为半描边宽度：

1. 在**角模板空间**中根据 `normalScale` 计算 `localPos_corner`
2. 通过边向量基底（xAxis, yAxis）转换到**本地坐标系**得到 `localPos`
3. 通过 `localToDevice` 变换到**设备空间**得到 `devPos`（所有顶点都需要）
4. 对 Outset 顶点，额外沿 normal 方向外扩 `aaRadius` 像素（AA 时为 1，Non-AA 时为 0）

**1. Outset 顶点（`normalScale = +1.0`，ID 0-3）**

`normalScale = +1.0` 使得 vertex shader 在第 4 步执行设备空间 AA 外扩（`normalScale > 0` 时触发）。本地坐标计算公式：

```
localPos_corner = (cornerRadii + strokeRadius) * (position + joinScale * position.yx)

其中：
  position = 表格中的 position 列，(1,0) 或 (0,1)（取决于 ID 0-3）
  position.yx = GLSL swizzle 语法，将 (x,y) 翻转为 (y,x)
    position = (1,0) 时：position.yx = (0,1)，结果 = (1, k) → 从 x 轴端点向 y 方向偏移
    position = (0,1) 时：position.yx = (1,0)，结果 = (k, 1) → 从 y 轴端点向 x 方向偏移
    效果：让顶点从圆弧端点沿凸出方向偏移，使三角形覆盖圆弧区域
  cornerRadii = (xRadii[cornerID], yRadii[cornerID])
  strokeRadius = 填充时为 0，描边时为 strokeWidth / 2
  joinScale = 根据角的连接类型取值：
    圆角（cornerRadii > 0）：kRoundScale = √2 - 1 ≈ 0.414
    直角（Miter join）：    kMiterScale = 1.0（顶点伸到对角线方向）
    方角（cornerRadii = 0）：0（position.yx 项消失，顶点留在端点上）
```

**`kRoundScale` 的推导**：圆角时 `joinScale = kRoundScale`，其目的是让两个 anchor 顶点（ID 4-5）的连线中点恰好落在圆弧上。以圆心在原点、半径 `r` 的 90° 圆弧为例：

```
圆弧端点：(r, 0) 和 (0, r)

Anchor ID 4（position = (1,0)）：
  localPos = r * ((1,0) + k*(0,1)) = (r, rk)      ← position.yx = (0,1)

Anchor ID 5（position = (0,1)）：
  localPos = r * ((0,1) + k*(1,0)) = (rk, r)      ← position.yx = (1,0)

两点连线中点：
  mid = ((r + rk)/2, (rk + r)/2) = (r(1+k)/2, r(1+k)/2)

圆弧 45° 处的点：
  (r/√2, r/√2)

令中点落在圆弧上：
  r(1+k)/2 = r/√2
  1 + k = √2
  k = √2 - 1 ≈ 0.414
```

这使得 anchor 连线从两端位于圆弧**外侧**，中点恰好**触碰**圆弧，形成紧密包裹圆弧的三角形分区——既不过度外扩浪费像素，也确保圆弧区域被完整覆盖。

然后转换到全局本地坐标系：

```
localPos = corner_point + xAxis * (localPos_corner.x - cornerRadii.x) + 
                          yAxis * (localPos_corner.y - cornerRadii.y)

其中：
  corner_point = 矩形该角的坐标
  xAxis = 从该角沿第一条相邻边出发的方向（模板 position 的 x 分量沿此方向映射）
  yAxis = 从该角沿第二条相邻边出发的方向（模板 position 的 y 分量沿此方向映射）
```

最后，变换到设备空间：

```
devPos = localToDevice * localPos    // 所有顶点都需要（齐次坐标，devPos.z = w）
```

Outset 顶点额外沿 normal 方向外扩 `aaRadius` 像素（AA 时 `aaRadius = 1`，Non-AA 时 `aaRadius = 0` 使外扩退化为零，三角形面积为零被 GPU 自动跳过）：

```
normal_device = xAxis * normal.x + yAxis * normal.y   // 模板 normal 映射到本地空间方向
devPos.xy += devPos.z * normalize(J * normal_device) * aaRadius  // 沿法线外扩（乘 w 保证透视正确）

其中：
  normal = 表格中的 normal 列
  J = local-to-device 变换在当前顶点处的 2×2 Jacobian 矩阵，描述本地坐标的微小变化
      如何映射到设备坐标的变化（只含缩放和旋转，不含平移和透视，因为 normal 是方向向量）。
      无透视时，J 是 localToDevice 左上角的常量 2×2 子矩阵：
        localToDevice = | a  b  c |      J = | a  b |
                        | d  e  f |          | d  e |
                        | 0  0  1 |
      有透视时，J 随顶点位置变化。Graphite 通过逆矩阵 deviceToLocal 计算：
        J_inv = float2x2(deviceToLocal[0].xy - deviceToLocal[0].z * localPos,
                         deviceToLocal[1].xy - deviceToLocal[1].z * localPos)
      其中 deviceToLocal[col].z 是逆矩阵第 col 列的透视分量。无透视时 .z = 0，
      退化为常量矩阵。有透视时 J 随 localPos 变化，近处外扩大、远处外扩小，
      保证 AA 带在屏幕上始终为 1 像素宽。
  当 localToDevice = I（单位矩阵）时简化为：devPos = localPos + normalize(normal_device)
```

这就是为什么表格中 position 相同的顶点（如 ID 0 和 ID 1）本地坐标相同，但最终设备坐标不同——它们的 normal 不同，外扩方向不同。

**2. Anchor 顶点（`normalScale = 0`，ID 4-5）**

`normalScale = 0` 不触发 AA 外扩（`normalScale > 0` 才触发），也不触发本地空间内缩（`normalScale < 0` 才触发）。本地坐标公式与 outset 完全相同，但最终设备坐标不含 AA 偏移。因此 anchor 顶点位于圆弧**外侧**（而非圆弧上）：

```
localPos_corner = (cornerRadii + strokeRadius) * (position + joinScale * position.yx)
localPos = corner_point + xAxis * (localPos_corner.x - cornerRadii.x) + 
                          yAxis * (localPos_corner.y - cornerRadii.y)
```

**3. Inset 顶点（`normalScale = -1.0`，ID 6-8）**

`normalScale = -1.0` 使得 vertex shader 在本地坐标计算阶段执行内缩（通过 `- localAARadius * normal`）。计算公式：

```
描边时：insetRadii = cornerRadii - strokeRadius    // inset 顶点收缩到描边内边缘
填充时：insetRadii = cornerRadii                   // strokeRadius = 0，无变化

localAARadius = 本地坐标系中 1 设备像素对应的距离（由 localToDevice 的缩放比例决定）。
               inset 顶点需要额外向内收缩 localAARadius，为内侧 AA 条带的 fragment shader
               留出计算 coverage 过渡的空间。

若半径足够大（insetRadii 所有分量 > localAARadius）：
  inset 层仍为圆弧形状：
  localPos_corner = insetRadii * position - localAARadius * normal
  （position 放在缩小后的圆弧端点上，再沿 normal 方向内缩 localAARadius）

若半径过小或为零（insetRadii 某分量 ≤ localAARadius）：
  圆弧已退化，无法维持弧形，改为直角（miter）处理：
  localPos_corner = insetRadii - localAARadius
  （标量偏移，两个 inset 顶点重合在角上，形成直角）

然后转换到全局本地坐标系：
localPos = corner_point + xAxis * (localPos_corner.x - cornerRadii.x) + 
                          yAxis * (localPos_corner.y - cornerRadii.y)

其中：
  position = 表格中的 position 列，(1,0) 或 (0,1)（取决于 ID 6-8）
  normal = 表格中的 normal 列，(1,0) 或 (0,1)（取决于 ID 6-8）
```

Inset 顶点的 AA 内缩已在本地坐标计算阶段完成（公式中的 `- localAARadius * normal`），设备空间不再做额外偏移。Center 顶点（ID 8，`centerWeight = 1`）直接 snap 到矩形中心。

**数值例子**

假设一个填充 RRect：
- 矩形边界：左 = 100，顶 = 100，右 = 200，底 = 200（即 xy = (100, 100)，宽高 = 100×100）
- 四个角的半径都相同：`cornerRadii = (30, 30)`
- 无描边：`strokeRadius = 0`
- 变换矩阵为单位矩阵（`localToDevice = I`，即本地坐标 = 设备坐标，1 本地单位 = 1 像素）

计算右上角（TR，cornerID = 1）的 9 个顶点坐标。

**边向量与局部坐标系**

TR 角位于矩形右上方，根据之前表格中的基底定义：

```
corner_point = (200, 100)

xAxis = (0, -1)  向上
yAxis = (1, 0)   向右

椭圆圆心 = corner_point 沿 xAxis 和 yAxis 各内偏 cornerRadii
         = (200, 100) + (0,-1)*(-30) + (1,0)*(-30)
         = (200 - 30, 100 + 30) = (170, 130)
```

模板中 `position = (1,0)` 映射到 xAxis（向上），`position = (0,1)` 映射到 yAxis（向右）。

**第 1 步：计算本地坐标**

Outset/Anchor 顶点（ID 0-5）使用相同的本地坐标公式：

```
localPos_corner = cornerRadii * (position + kRoundScale * position.yx)
```

position = (1,0) 的顶点（ID 0, 1, 4）：
  localPos_corner = (30,30) * ((1,0) + 0.414*(0,1)) = (30, 12.43)
  offset = (30 - 30, 12.43 - 30) = (0, -17.57)
  localPos = (200,100) + (0,-1)*0 + (1,0)*(-17.57) = (182.43, 100)

position = (0,1) 的顶点（ID 2, 3, 5）：
  localPos_corner = (30,30) * ((0,1) + 0.414*(1,0)) = (12.43, 30)
  offset = (12.43 - 30, 30 - 30) = (-17.57, 0)
  localPos = (200,100) + (0,-1)*(-17.57) + (1,0)*0 = (200, 117.57)

Inset 顶点（ID 6-7，localAARadius = 0）：

ID 6（position = (1,0)，normal = (1,0)）：
  localPos_corner = (30,30) * (1,0) = (30, 0)
  offset = (30 - 30, 0 - 30) = (0, -30)
  localPos = (200,100) + (0,-1)*0 + (1,0)*(-30) = (170, 100)

ID 7（position = (0,1)，normal = (0,1)）：
  localPos_corner = (30,30) * (0,1) = (0, 30)
  offset = (0 - 30, 30 - 30) = (-30, 0)
  localPos = (200,100) + (0,-1)*(-30) + (1,0)*0 = (200, 130)

ID 8（Center，centerWeight = 1）：
  localPos = 矩形中心 = (150, 150)

**第 2 步：设备空间变换与 AA 外扩**

当 localToDevice = I 时，本地坐标 = 设备坐标。所有顶点变换后 devPos = localPos。

Outset 顶点额外沿 normal 方向外扩 `aaRadius` 像素（本例中 AA 模式，`aaRadius = 1`）：

```
normal_device = xAxis * normal.x + yAxis * normal.y
              = (0,-1) * normal.x + (1,0) * normal.y
devPos = localPos + normalize(normal_device) * 1
```

各 Outset 顶点的外扩计算：

ID 0（localPos = (182.43, 100)，normal = (1, 0)）：
  normal_device = (0,-1)*1 + (1,0)*0 = (0, -1)
  devPos = (182.43, 100) + (0, -1) = (182.43, 99)

ID 1（localPos = (182.43, 100)，normal = (√2/2, √2/2)）：
  normal_device = (0,-1)*√2/2 + (1,0)*√2/2 = (√2/2, -√2/2)
  devPos = (182.43, 100) + (√2/2, -√2/2) ≈ (183.14, 99.29)

ID 2（localPos = (200, 117.57)，normal = (√2/2, √2/2)）：
  normal_device = (√2/2, -√2/2)
  devPos = (200, 117.57) + (√2/2, -√2/2) ≈ (200.71, 116.86)

ID 3（localPos = (200, 117.57)，normal = (0, 1)）：
  normal_device = (0,-1)*0 + (1,0)*1 = (1, 0)
  devPos = (200, 117.57) + (1, 0) = (201, 117.57)

Anchor 和 Inset 顶点不做外扩，设备坐标 = 本地坐标。

**汇总 TR 角 9 个顶点坐标**：

| ID | 用途 | 本地坐标 | 设备坐标（含 AA 外扩） |
|----|------|---------|----------------------|
| 0 | 外侧 AA（向上） | (182.43, 100) | (182.43, 99) |
| 1 | 外侧 AA（45°方向） | (182.43, 100) | (183.14, 99.29) |
| 2 | 外侧 AA（45°方向） | (200, 117.57) | (200.71, 116.86) |
| 3 | 外侧 AA（向右） | (200, 117.57) | (201, 117.57) |
| 4 | Anchor | (182.43, 100) | (182.43, 100) |
| 5 | Anchor | (200, 117.57) | (200, 117.57) |
| 6 | Inset（向上） | (170, 100) | (170, 100) |
| 7 | Inset（向右） | (200, 130) | (200, 130) |
| 8 | 填充中心 | (150, 150) | (150, 150) |

从表中可看出：

- **Outset 顶点 ID 0-3**：本地坐标两两相同（因为 position 相同），但设备空间因 AA 外扩方向不同而分开。四个 outset 顶点在设备空间形成一段从 (182.43, 99) 到 (201, 117.57) 的外包络弧形——沿圆弧外侧扩展 1 像素
- **Anchor 顶点 ID 4-5**：不做 AA 外扩，设备坐标 = 本地坐标。ID 4 在 (182.43, 100)，ID 5 在 (200, 117.57)，连线中点 ≈ (191.21, 108.79)。椭圆圆心在 (170, 130)，中点到圆心距离 = √((191.21-170)² + (108.79-130)²) = √(449.7 + 449.7) ≈ 30，恰好落在圆弧上
- **Inset 顶点 ID 6-7**：ID 6 在 (170, 100)，ID 7 在 (200, 130)。从椭圆圆心 (170, 130) 看，ID 6 距离 = √(0² + 30²) = 30，ID 7 距离 = √(30² + 0²) = 30，都恰好在圆弧上。这两个点就是圆弧的两个端点
- **Center 顶点 ID 8**：snap 到矩形中心 (150, 150)，所有四角的 center 顶点重合在此

69 个索引构成**三条连续的 Triangle Strip**，每条 strip 包含所有 4 个角（TL → TR → BR → BL → 闭合回 TL）。角间跳转产生的三角形覆盖了两角之间的直边区域。

顶点编号：每角 9 个顶点，起始偏移 = 角ID × 9（TL=+0, TR=+9, BR=+18, BL=+27）。

```
Strip 1：外侧 AA 条带（outset ↔ anchor），30 索引
  TL: 0,4,1,5,2,3,5,  TR: 9,13,10,14,11,12,14,  BR: 18,22,19,23,20,21,23,  BL: 27,31,28,32,29,30,32,  闭合: 0,4
  每角 7 索引 = 4 个有效三角形（outset→anchor AA 渐变）+ 1 个重复索引（维持 strip 奇偶绕序）
  角间跳转三角形覆盖两角之间的直边 AA 带
  功能：覆盖形状外扩 1px 的区域，fragment shader 计算 0→1 的 coverage 过渡

Strip 2：内侧 AA 条带（anchor ↔ inset），18 索引
  TL: 4,6,5,7,  TR: 13,15,14,16,  BR: 22,24,23,25,  BL: 31,33,32,34,  闭合: 4,6
  每角 4 索引 = 2 个三角形（anchor→inset 过渡）
  角间跳转三角形覆盖直边的 anchor→inset 区域
  功能：圆弧附近区域，fragment shader 计算椭圆距离 coverage

Strip 3：实心填充条带（inset → center），21 索引
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

**Vertex Shader 输出到 Fragment Shader 的关键数据**

除了顶点位置外，vertex shader 还计算以下传递给 fragment shader 的 varying：

- **edgeDistances (float4)**：当前顶点到矩形四条边的有符号距离，fragment shader 用它判断像素所在区域（哪个角、哪条边）
  ```glsl
  edgeDistances = dy*(xs - localPos.x) - dx*(ys - localPos.y)
  ```
- **jacobian (float4)**：`deviceToLocal` 逆变换的 2×2 Jacobian 子矩阵（前面 Outset 部分已详细说明），fragment shader 用它将本地空间距离转换为设备空间像素距离
  ```glsl
  jacobian = float4(deviceToLocal[0].xy - deviceToLocal[0].z * localPos,
                    deviceToLocal[1].xy - deviceToLocal[1].z * localPos)
  ```
- **perPixelControl (float2)**：编码 fragment shader 的行为模式
  - `perPixelControl.x > 0`：内部像素，直接返回 coverage = 1.0（快速路径）
  - `perPixelControl.x ≤ 0`：边缘像素，需要逐像素计算距离
  - `perPixelControl.y`：编码 outset 距离或矩形快速路径标记

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

**为什么 Graphite 不支持椭圆角描边**

Graphite 的描边 RRect 只支持圆角（X = Y），不支持椭圆角（X ≠ Y）。这是因为椭圆的等距线（offset curve）不是椭圆——椭圆上每个点沿法线偏移 `w` 后得到的曲线没有解析表达式。

直觉上可能认为椭圆 `x²/a² + y²/b² = 1` 向外偏移 `w` 后得到椭圆 `x²/(a+w)² + y²/(b+w)² = 1`，但这是不对的。椭圆的法线方向不是径向的，偏移后的曲线和放大椭圆在端点重合，但中间区域偏离。

以 a=10, b=2, w=1 的椭圆为例分析第一象限弧（从短轴端点 (0, 2) 到长轴端点 (10, 0)）：

```
等距线与 (a+w, b+w) = (11, 3) 椭圆的对比：

短轴端点 (0, 2)：
  等距线点 = (0, 3)，(11,3) 椭圆点 = (0, 3) → 重合

中间 θ = π/4：
  椭圆点 = (7.07, 1.41)
  法线方向 = normalize(cosθ/a, sinθ/b) = normalize(0.0707, 0.354) ≈ (0.196, 0.981)
  等距线点 = (7.07 + 0.196, 1.41 + 0.981) = (7.27, 2.39)
  (11,3) 椭圆点 = (7.78, 2.12)
  → 等距线比 (11,3) 椭圆更远离圆心（更"鼓"）

长轴端点 (10, 0)：
  等距线点 = (11, 0)，(11,3) 椭圆点 = (11, 0) → 重合
```

等距线在端点与 (a+w, b+w) 椭圆重合，但中间区域始终偏向外侧。椭圆越扁（a/b 比值越大），偏差越大。Ganesh 的 EllipticalRRectOp 用放大/缩小椭圆近似等距线（详见 3.2.3 节），接受小幅误差。Graphite 选择不做这个近似，椭圆角描边走 Path 回退。

---

## 4. 对比分析

### 4.1 TGFX 与 Skia GPU 渲染方案对比

| 策略 | 引擎 | RRect 类型 | 绘制类型 | 顶点数 | 索引数（Fill/Stroke） | AA 算法 | 四角独立半径 | Stroke | 透视 | 合批方式 |
|------|------|-----------|---------|-------|---------------------|---------|-------------|--------|------|---------|
| **RRectDrawOp AA** | TGFX | Simple | drawIndexed | 16 | 54 / 48 | 椭圆 Sampson 距离 | ❌ | ✅ | ❌ | 同 AA 模式合批 |
| **RRectDrawOp Non-AA** | TGFX | Simple | drawIndexed | 4 | 6 / 6 | SDF + `step()` | ❌ | ✅ | ❌ | 同 Non-AA 合批 |
| **CircularRRectOp** | Ganesh | Simple + 圆角 | drawIndexed | 16<br>（overstroke 24） | 54 / 48<br>（overstroke 66） | 圆距离函数 | ❌ | ✅ | ❌ | 同 Op 合批 |
| **EllipticalRRectOp** | Ganesh | Simple | drawIndexed | 16 | 54 / 48 | 椭圆 Sampson 距离 | ❌ | ✅ | ❌ | 同 Op 合批 |
| **FillRRectOp** | Ganesh | 任意 | drawInstanced | 40 | — | `x²+y²-1` + `fwidth` | ✅ | ❌ | ❌ | 同 Op 合批 |
| **AnalyticRRectRenderStep** | Graphite | 任意 | drawInstanced | 36 | 69 | 解析距离场 + Jacobian | ✅ | ✅ | ✅ | 所有形状统一合批 |

### 4.2 Graphite 方案对 TGFX 四角独立半径的参考价值

**值得参考的**：
1. **Fragment shader 的四角独立距离计算**：`$corner_distances` → `$elliptical_distance` 的模式——在 FS 中根据 `edgeDistances` 判断像素所在象限，分别计算四角椭圆距离。本次改造不采用此模式（AA 路径用 per-vertex 分配半径，Non-AA 路径用本地坐标象限判断），后续若 TGFX 演进为 Graphite 风格的统一 pipeline 时可参考。
2. **Instance data 编码**：用一组 `float4` 巧妙编码多种形状类型，实现极致合批。本次改造不涉及（TGFX 各形状类型有独立的 Op），后续若演进为统一 pipeline 架构时可参考。

**不适合直接采用的**：
1. **36 顶点统一模板**：需要 instanced rendering 支持（`drawInstanced` + per-instance attributes），TGFX 当前的渲染架构（`drawIndexed` + 手写顶点）不直接支持。
2. **Jacobian 传递**：需要 fragment shader 支持 `float2x2` varying 和较复杂的矩阵运算，对低端设备有性能风险。且 TGFX 在 CPU 侧已将顶点变换到设备空间，shader 不涉及透视变换，无需 Jacobian。Graphite 之所以需要 Jacobian 做设备空间外扩而不能在 vertex shader 中提前做透视除法后用固定像素外扩，是因为 GPU 光栅化阶段依赖顶点的 `w` 分量做**透视插值矫正**——varying 的插值会自动除以 `w`，使得 edgeDistances 等数据在透视下正确。如果提前做透视除法（输出 `w = 1`），varying 退化为线性插值，透视场景下圆角区域的 coverage 计算会出错。TGFX 在 CPU 侧 `mapPoints` 已完成透视除法，shader 中所有坐标在设备空间且 `w = 1`，线性插值就是正确的，不存在这个问题。
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
    Rect,      // all corner radii are zero (or rect is empty)
    Oval,      // radii fill the rect
    Simple,    // all four corners have the same radii
    Complex    // arbitrary per-corner radii
  };

  Rect rect = {};
  std::array<Point, 4> radii = {};  // [TL, TR, BR, BL], each Point = (xRadius, yRadius)
  Type type = Type::Rect;

  bool isRect() const;
  bool isOval() const;
  bool isSimple() const;
  bool isComplex() const;

  void setRectXY(const Rect& rect, float radiusX, float radiusY);
  void setRectRadii(const Rect& rect, const std::array<Point, 4>& radii);
  void setOval(const Rect& oval);

  void scale(float scaleX, float scaleY);
};
```

`ScaleRadii()`（W3C 半径缩放）和 `ComputeType()`（根据数据推断 Type）作为 `.cpp` 文件中的静态函数实现，不出现在头文件中。仅 `setRectRadii` 内部调用它们。`setRectXY` 和 `setOval` 保持当前逻辑，额外设置 `type` 字段即可。

**设计决策**：

1. **`std::array<Point, 4>` 而非 `Point radii[4]`**：支持整体赋值（`a = b`）和整体比较（`a == b`），C 原生数组需要 `memcpy` 和手动逐元素比较。
2. **Type 枚举**：保留 Rect / Oval / Simple / Complex 四种（去掉 Skia 的 Empty 和 NinePatch，TGFX 无使用场景）。仅 `setRectRadii` 内部调用 `ScaleRadii()` → `ComputeType()` 推断类型，`setRectXY` 和 `setOval` 保持当前逻辑并直接设置 `type`。
3. **内存布局**：`Rect(16) + std::array<Point,4>(32) + Type(4)` = 52 字节（相比原 24 字节增加 28 字节）。由于 RRect 不是高频创建的小对象（通常存于 Record/Content 中），这个增长可以接受。
4. **向后兼容**：旧的 `radii` 单值成员被替换为 `radii` 数组。需要更新所有引用 `rRect.radii.x / rRect.radii.y` 的代码。对于 Simple 类型，通过 `rRect.radii[0]` 访问统一半径。

### 5.2 半径缩放算法

遵循 W3C CSS Background 规范 Section 5.5 "Overlapping Curves"，与 Skia 的 `SkRRect::scaleRadii()` 对齐：

```
令 f = min(Li/Si)，其中：
  i ∈ {top, right, bottom, left}
  S_top    = radii[TL].x + radii[TR].x
  S_right  = radii[TR].y + radii[BR].y
  S_bottom = radii[BR].x + radii[BL].x
  S_left   = radii[BL].y + radii[TL].y
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
  if (!pathRef->path.isRRect(&skRRect) || (!skRRect.isSimple() && !skRRect.isNinePatch() &&
                                             !skRRect.isComplex())) {
    return false;  // 接受 Simple、NinePatch、Complex
  }
  if (rRect) {
    const auto& rect = skRRect.rect();
    rRect->rect.setLTRB(rect.fLeft, rect.fTop, rect.fRight, rect.fBottom);
    for (int i = 0; i < 4; ++i) {
      auto radii = skRRect.radii(static_cast<SkRRect::Corner>(i));
      rRect->radii[i].set(radii.fX, radii.fY);
    }
    ComputeType(rRect);
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

Non-AA 模式使用 4 顶点 quad + fragment shader SDF。当前传递统一的 `inRadii(Float2)`。扩展四角独立半径后，在同一个 `NonAARRectGeometryProcessor` 中通过编译期条件 `perCornerRadii` 生成两种 shader 变体：

**Simple RRect（`perCornerRadii = false`）**：保持现有逻辑不变

```
顶点属性: inRadii (Float2) → 2 floats (统一 xRadius, yRadius)
每顶点: 10 floats (position 2 + localCoord 2 + radii 2 + bounds 4)
Fragment shader: 现有 SDF 公式，四角对称
```

**Complex RRect（`perCornerRadii = true`）**：传递四角独立半径

```
顶点属性: inXRadii (Float4) + inYRadii (Float4) → 8 floats
  inXRadii: [TL.x, TR.x, BR.x, BL.x]
  inYRadii: [TL.y, TR.y, BR.y, BL.y]
每顶点: 16 floats (position 2 + localCoord 2 + xRadii 4 + yRadii 4 + bounds 4)
```

Fragment shader 根据像素所在象限选择对应角的半径（`localCoord` 是变换前的本地坐标，不受 viewMatrix 影响）：

```glsl
vec2 p = localCoord - center;
float rx, ry;
if (p.x < 0.0) {
    rx = (p.y < 0.0) ? xRadii[0] : xRadii[3];  // TL or BL
    ry = (p.y < 0.0) ? yRadii[0] : yRadii[3];
} else {
    rx = (p.y < 0.0) ? xRadii[1] : xRadii[2];  // TR or BR
    ry = (p.y < 0.0) ? yRadii[1] : yRadii[2];
}
vec2 radii = vec2(rx, ry);
vec2 q = abs(p) - halfSize + radii;
float d = min(max(q.x / radii.x, q.y / radii.y), 0.0)
         + length(max(q / radii, 0.0)) - 1.0;
```

**合批策略**：Simple 之间可合批，Complex 之间可合批，Simple 和 Complex 不能合批（pipeline key 不同，顶点布局不同）。Complex 场景预期很少，打断合批可接受。

#### 5.4.2 AA 模式适配（EllipseGeometryProcessor）

AA 模式使用 9-patch 16 顶点，目前所有顶点共用同一组 `reciprocalRadii`（因为四角相同）。支持四角独立半径后，保持 9-patch 拓扑不变，按角分配不同的 `reciprocalRadii` 值。

9-patch 布局天然地将 RRect 分成了角+边+中心区域。每个角区域的顶点可以传递该角对应的半径：

```
 TL角顶点 ← reciprocalRadii of TL corner
 TR角顶点 ← reciprocalRadii of TR corner
 BL角顶点 ← reciprocalRadii of BL corner
 BR角顶点 ← reciprocalRadii of BR corner
 边缘/中心顶点 ← 最近角的半径
```

边缘和中心区域的顶点虽然可能写入"不属于"该区域的角半径，但这不影响渲染正确性：这些区域内 `EllipseOffset` 接近零，fragment shader 中 `test = dot(offset, offset) - 1.0 ≈ -1`，`edgeAlpha` 直接饱和为 1.0，`reciprocalRadii` 的值不参与有效计算。varying 插值从角区域到边缘区域的过渡中，`EllipseOffset` 同步衰减为零，半径插值误差被 offset 的衰减完全掩盖。

顶点属性保持不变（`inEllipseRadii: Float4`），但**不同角的顶点写入不同的 reciprocalRadii 值**。

关键改造点在 `RRectsVertexProvider::getAAVertices()`：

```
原: 统一 reciprocalRadii[4] 用于所有 16 顶点
新: 按角分配:
    - 行 0 (top):    左列顶点用 TL 半径，右列顶点用 TR 半径
    - 行 1 (top-inner): 同上
    - 行 2 (bottom-inner): 左列用 BL 半径，右列用 BR 半径
    - 行 3 (bottom):  同上
    - 中间列顶点: 使用该行对应的半径
```

对于四角独立半径，四个角区域大小可以不同，需要调整 9-patch 的切分点：

```
原: xOuterRadius = xRadius (统一)
新: 左侧 xOuterRadius = max(TL.x, BL.x)
    右侧 xOuterRadius = max(TR.x, BR.x)
    上侧 yOuterRadius = max(TL.y, TR.y)
    下侧 yOuterRadius = max(BL.y, BR.y)
```

Fragment shader 无需修改——它已经使用 per-vertex 传入的 `ellipseRadii` varying，只要正确地在顶点层面分配半径即可。

因此对 Simple 和 Complex 类型都能正确渲染，fragment shader 无需修改，改动集中在顶点生成层，保持了现有的批处理架构和 9-patch 拓扑。

#### 5.4.3 Stroke 处理

对于 Stroke 模式，当前已使用椭圆距离场。四角独立半径的 stroke 需要：
- Non-AA stroke：与 fill 类似，per-corner 选择半径后计算内外 SDF
- AA stroke：9-patch 中每个角区域使用该角的 outer/inner 半径

**Overstroke 退化判断**：当前 TGFX 使用全局 `stroked` 布尔值判断是否退化为 fill。四角独立半径后，判断条件扩展为检查所有角的内半径：

```cpp
// 当前（统一半径）：
stroked = innerXRadius > 0.0f && innerYRadius > 0.0f;

// 扩展后（四角独立半径）：
stroked = true;
for (int i = 0; i < 4; ++i) {
    if (radii[i].x - halfStrokeX <= 0 || radii[i].y - halfStrokeY <= 0) {
        stroked = false;
        break;
    }
}
```

只要任意一个角的内半径 ≤ 0，整个 RRect 退化为 fill。不做 per-corner 的部分描边部分 fill——描边是整体行为，部分退化在视觉上不合理。

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
    if (_roundness[0] == _roundness[1] && _roundness[1] == _roundness[2] &&
        _roundness[2] == _roundness[3]) {
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

四角独立半径对合批的影响：

- **AA 模式**：半径嵌入 per-vertex varying（`reciprocalRadii`），GeometryProcessor key 不依赖具体半径值，Simple 和 Complex RRect **可以合批**
- **Non-AA 模式**：Simple 和 Complex 使用不同的顶点布局（`perCornerRadii` 编译期条件导致不同 pipeline key），**不能合批**。Complex 场景预期很少，打断合批可接受

### 5.8 数据流总览

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
  Path::isRRect()            ←── 扩展：支持 Complex（含 Skia NinePatch）
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

## 6. 测试计划

### 6.1 单元测试

- **RRect 数据结构测试**
  - `setRectXY` → Type == Simple
  - `setOval` → Type == Oval
  - `setRectRadii` 任意值 → Type 正确推断（Simple / Complex）
  - 半径缩放：相邻角半径超过边长时正确缩放
  - `isRect()`, `isOval()`, `isSimple()` 等查询方法
  - `scale()` 操作

- **Path 互操作测试**
  - `Path::addRRect(complexRRect)` → `Path::isRRect()` 返回 true 且还原正确
  - Complex RRect 的 Path roundtrip

- **渲染正确性测试**
  - 四角相同半径 → 与改动前结果一致（回归测试）
  - 四角不同半径 fill → 视觉正确
  - 四角不同半径 stroke → 视觉正确
  - 极端情况：某些角半径为 0（方角）、某些角半径为 oval
  - 合批：多个不同半径 RRect 合批绘制

### 6.2 性能测试

- 合批数量：确认不同半径 RRect 仍可合批
- 顶点数据增量：Non-AA 从 10 floats/vertex 增加到 16 floats/vertex 的带宽影响
- Draw call 数量对比：四角不同 RRect 从 shape path 走到 RRect 快速路径后的 draw call 减少

### 6.3 风险评估

| 风险 | 等级 | 缓解策略 |
|------|------|----------|
| AA 9-patch 切分点不对齐导致接缝 | 高 | 仔细计算每行每列的 OuterRadius，确保角区域完全覆盖椭圆范围 |
| Non-AA SDF 在角边界处不连续 | 中 | 使用 step 函数（Non-AA 本身无抗锯齿），在象限切换处天然连续 |
| 半径缩放 double 精度差异 | 低 | 与 Skia 实现对齐，复用其测试用例 |
| 顶点数据增大影响带宽 | 低 | Non-AA 增加 6 floats/vertex，对于通常的 4 顶点 quad 影响有限 |
| `RRect` ABI 变化导致序列化问题 | 低 | TGFX 不对 RRect 做跨版本序列化 |

---

## 7. 实现步骤

### Phase 1：核心数据结构（低风险，高优先级）

1. **扩展 `RRect` 结构体**
   - 将 `Point radii` 改为 `std::array<Point, 4> radii`
   - 添加 `Type` 枚举和 `type` 成员
   - 实现 `setRectRadii()`、`ScaleRadii()`、`ComputeType()`
   - 保留并适配 `setRectXY()`、`setOval()` 等已有方法
   - 实现 `operator==`、`operator!=`

2. **全量适配编译错误**
   - 搜索所有 `rRect.radii.x`、`rRect.radii.y` 引用
   - Simple 类型通过 `radii[0]` 访问统一半径
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

## 8. 参考资料

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
