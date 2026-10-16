# 圆角平滑度（Corner Smoothing）技术方案

## 1 背景

### 1.1 问题描述

tgfx 的圆角矩形（`RRect`）目前只支持标准圆角：每角是一段椭圆弧，弧与相邻边在切点处一阶连续（G1），但曲率在切点处从 `1/r` 突变为 `0`，视觉上能察觉这一突变。设计工具普遍提供"圆角平滑度"参数来消除这一突变：在圆弧两侧各插入一段过渡曲线，让曲率连续变化，得到俗称squircle 的外观。

### 1.2 需求

1. `RRect` 支持圆角平滑度参数，四角统一单值，半径仍每角独立。
2. 普通 `Canvas` 绘制圆角矩形时支持平滑度。
3. `DropShadowStyle` 与 `InnerShadowStyle` 的 spread 支持平滑度。
4. 矢量元素层 `Rectangle` 暴露平滑度接口。

以下能力明确不在本次范围内：

- `DropShadowFilter`：它没有 spread 参数，且作为滤镜只能拿到位图内容、拿不到矢量几何，无从对形状重新求值。
- `RoundCorner` 的通用多边形平滑：它作用于任意 path，支持平滑需要处理任意夹角的过渡曲线构造，而本方案的过渡曲线只针对直角定义。
- 不同平滑度之间的合批：平滑度经 uniform 传入 shader，同一 draw call 内所有圆角矩形必须共享同一数值。
- 放宽平滑角描边的 `UseDrawPath` 判据：首版与椭圆角保持同样保守的判据。
- 大面积静态单形状的 CPU 路径专项优化：CPU 路径在缓存命中后单形状的 fragment 成本更低，但该收益不足以换取双主路径分发的复杂度与轮廓一致性风险。

### 1.3 术语

| 术语 | 含义 |
|---|---|
| `smoothness`（简写 `s`） | 平滑度，无量纲，取值 `[0, 1]`，四角统一单值 |
| `r` | 某角的圆形半径（标量） |
| 曲线连接点偏移 | 曲线连接点（角曲线与直边的衔接点）到角顶点的距离，等于 `(1+s)·r` |
| `φ = (π/2)(1-s)` | 直角处保留的圆弧扫过角 |
| 两级降级 | 边长不足时先削减 smoothness，其归零后再按比例缩减半径 |

### 1.4 调研脚本

本文档引用的第三方行为均由脚本实测得出，可复现：

| 脚本 | 用途 |
|---|---|
| `tools/extract_figma_smooth_shader.py` | 从 Figma 压缩 shader 中提取并反混淆平滑圆角相关函数 |
| `tools/verify_smooth_corner_math.py` | 数值验证本文档的数学论断 |
| `tools/measure_figma_shadow_corner.py` | 从截图中以 50% 等值线测量阴影轮廓，判定 spread 的实现口径 |

---

## 2 现有实现分析

### 2.1 RRect 数据结构

`include/tgfx/core/RRect.h` 的私有成员为：

```cpp
Rect _rect = {};
std::array<Point, 4> _radii = {};   // 序：TL, TR, BR, BL；每角 (radiusX, radiusY)
Type _type = Type::Rect;            // Rect / Oval / Simple / Complex
```

`operator==`（166 行起）只比较 `_rect` 与 `_radii`。

`src/core/RRect.cpp` 的 `setRectRadii` 流程为：负分量整角归零 → `ScaleRadii` → `ComputeType`。其中 `ScaleRadii`（44-79 行）保证相邻两角半径之和不超过共享边长，超出则整体按比例缩放，随后 `FlushToZero` 处理浮点吸收，最后将任一分量为零的角整角归零。

`scale`（142 行起）的注释说明：正缩放保持全部分类不变式，因此有意跳过 `ScaleRadii` 与 `ComputeType`。

### 2.2 Canvas 分发

`src/core/Canvas.cpp:397-434` 的 `drawRRect`：空 rect转 `drawRect`；全部角在设备空间不足半像素时转 `drawRect`；`UseDrawPath` 为真时转 `drawPath`；否则进入 `drawContext->drawRRect` 的解析路径。

`HasSharpCorner`（298-309 行）判定任一半径乘设备缩放后小于 `0.5`。`UseDrawPath`（330-395 行）在尖角、对角重叠、厚描边等情形返回真。

`drawPath`（469-481 行）会用 `path.isOval` 与 `path.isRRect` 反推`RRect` 再走解析路径。

### 2.3 CPU 链路

`src/core/Path.cpp:173-175` 的 `isRRect` 经 pathkit 的 rrect 检测反推；`addRRect`（520-524 行）把 `RRect` 转为 pathkit 的 rrect。两者都没有承载平滑度的通道。

`src/core/PathTriangulator.cpp:38-57` 的 `ShouldTriangulatePath`：

```cpp
if (minDimension <= 0) return true;
if (maxDimension <= MIN_TRIANGULATE_SIZE) return false;          // 162
if (path.countVerbs() <= AA_TESSELLATOR_MAX_VERB_COUNT) return true;   // 100
if (maxDimension > MAX_RASTERIZED_TEXTURE_SIZE) return true;     // 4096
return path.countPoints() * AA_TESSELLATOR_BUFFER_SIZE_FACTOR <= width * height;  // 170
```

一个平滑圆角矩形的轮廓约 18 个 verb（四角各 3 段三次贝塞尔，加四条截断边与 move/close），远低于 100 的阈值。因此中小尺寸走纹理光栅化，较大尺寸走三角化，顶点规模是数十量级。

`src/gpu/OpsCompositor.cpp:202-224` 的 `drawShape` 合批要求 `getUniqueKey()` 完全相同且矩阵仅差平移，否则各自一个draw call。Shape 有基于 `getUniqueKey()` 的三角化与纹理缓存（`src/gpu/ProxyProvider.cpp`），并支持异步。

### 2.4 GPU 链路

RRect 相关的 shader 分两类：GeometryProcessor（GP，绘制路径，顶点布局与 shader 成对）与 FragmentProcessor（FP，裁剪覆盖率）。全景如下：

| 场景 | 类名 | 类型 | 样式 | 说明 |
|---|---|---|---|---|
| 绘制 | `EllipseGeometryProcessor` | GP | Fill / Stroke | AA、四角统一半径；4×4 网格（16 顶点、54/48 索引），角区的椭圆偏移量由顶点插值提供 |
| 绘制 | `ComplexEllipseGeometryProcessor` | GP | Fill / Stroke | AA、逐角半径；同上，另带逐角 radii 与 edgeDist 属性 |
| 绘制 | `NonAARRectGeometryProcessor` | GP | Fill / Stroke | NonAA、四角统一半径；单 quad（4 顶点、6 索引），逐 fragment 求椭圆隐式方程 |
| 绘制 | `ComplexNonAARRectGeometryProcessor` | GP | Fill / Stroke | NonAA、逐角半径；单 quad + 逐 fragment 角区分发 |
| 绘制（降级） | 通用形状渲染管线 | — | — | `drawPath` 降级后三角化或纹理光栅，无 RRect 专用 shader |
| 阴影 | 无专用形状 shader | — | — | 扩散形状经 `PictureRecorder` 录制后走上述绘制 GP 出 mask，再由 `GaussianBlur1DFragmentProcessor`（`Blur1DFragmentProcessor` 子类）模糊 |
| 裁剪覆盖率 | `RectEffect` | FP | — | 矩形裁剪；`needTransform` 编译期变体 |
| 裁剪覆盖率 | `RRectEffect` | FP | — | 圆角矩形裁剪；逐 fragment Sampson 距离 + AA 软边 |

#### 2.4.1 绘制

`src/gpu/ops/RRectDrawOp.cpp:67-82` 按 `aaType` 与 `isComplex` 两个维度在四个 GeometryProcessor 之间选择。`RRectDrawOp.h:33-47` 定义 `MaxNumRRects = 1024`、`IndicesPerAAFillRRect = 54`、`IndicesPerAAStrokeRRect = 48`、`IndicesPerNonAARRect = 6`。

`src/gpu/OpsCompositor.cpp:127-133` 的 `ComplexMismatch` 决定 `drawRRect`（150行起）是否打断合批，该函数只比较 `isComplex`。

AA 路径的 4×4 网格把切线开在 `bounds + radii` 处，使每个角格的顶点能携带该角局部坐标系下的椭圆偏移量，fragment 端免去角归属分发；NonAA 路径单 quad 罩住整个矩形，逐 fragment 求椭圆隐式方程的符号即可（硬边不需要距离归一化）。

#### 2.4.2 阴影

`src/layers/SpreadUtils.cpp` 的 `MakeSpreadRRect`（33-53 行）对 bounds 做 `outset(distance)`，并把每个非零半径分量加上 `distance` 以保持圆角同心：

```cpp
auto bounds = rRect.rect();
bounds.outset(distance, distance);
if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
  return {};
}
auto radii = rRect.radii();
for (auto& corner : radii) {
  if (corner.x > 0.0f) {
    corner.x = std::max(0.0f, corner.x + distance);
  }
  if (corner.y > 0.0f) {
    corner.y = std::max(0.0f, corner.y + distance);
  }
}
```

`MakeSpreadShapeImage`（154-231 行）从 `ContourInputSource::shape()` 取出 `StyledShape`，用 `UnwrapMatrixShape` 剥离矩阵，判退化后用 `PictureRecorder` 录制 `DrawSpreadRRect` 的结果，再由 `ToImageWithOffset` 出图。形状识别依赖 `path.isOval(&rect)` 与 `path.isRRect(&rRect)`，复杂路径降级为包围盒。

`StyledShape`（`include/tgfx/layers/layerstyles/StyledShape.h`）只有 `{shape, type, strokeWidth, strokeAlign}`，没有结构化的 `RRect`。

`contentShape` 的生产点：`ShapeLayer::onGetContentShape`（ShapeLayer.cpp:253-273）、`VectorLayer.cpp:156`、`SolidLayer.cpp:92`、`Layer.cpp:1032-1043`（兜底取紧包围盒矩形）。

#### 2.4.3 裁剪覆盖率

`src/gpu/glsl/processors/GLSLRRectEffect.cpp` 的 `emitCode` 分为八步，其中：

- Step 2（83-111 行）：以每角的 arc-box 判定fragment 归属，用前缀和折叠为 one-hot，再用 `dot(inBox, ...)` 选出该角的圆心与半径。
- Step 5（118-124 行）：椭圆SDF，采用 Sampson 距离。

```cpp
fragBuilder->codeAppend("highp vec2 offset = (local - arcCenter) / safeRadii;");
fragBuilder->codeAppend("highp vec2 safeOffset = max(abs(offset), vec2(1.0/4096.0));");
fragBuilder->codeAppend("highp float test = dot(safeOffset, safeOffset) - 1.0;");
fragBuilder->codeAppend("highp vec2 grad = 2.0 * safeOffset / safeRadii;");
fragBuilder->codeAppend("highp float gradDot = max(dot(grad, grad), 1.1755e-38);");
fragBuilder->codeAppend("highp float cornerDist = test * inversesqrt(gradDot);");
```

该公式仅能表达椭圆弧，无法表达平滑角轮廓。

`RRectEffect::Make`（24-51 行）拒绝透视矩阵，并把 `axisScales` 烘焙进 `RRect`（`adjustedRRect.scale`），把去掉 scale 的逆矩阵作为 `deviceToLocal` 传入 shader。

`src/gpu/processors/RRectEffect.cpp:28-30` 的 `onComputeProcessorKey` 目前只写入 `_needTransform`。

shader 变体机制：`src/gpu/glsl/processors/GLSLRectEffect.cpp:68-77` 展示了本项目的变体做法——以构造期确定的 `needTransform()` 决定是否添加 `DeviceToLocal` uniform 并改变取坐标的代码，`onSetData` 以同样条件传值；`RectEffect.h:52` 在构造函数里由 `!deviceToLocal.isIdentity()` 求出 `_needTransform`。这是编译期分变体而非运行期 uniform 分支，使不需要该能力的绘制不承担任何额外成本。

### 2.5 其他 RRect 消费点

`src/core/RRectUtils.cpp:95-102` 的 `IsInnerToCorners` 写死椭圆方程：

```cpp
const float ellipseTest =
    centerOffset.x * centerOffset.x * ry * ry + centerOffset.y * centerOffset.y * rx * rx;
const float limit = (rx * ry) * (rx * ry);
return ellipseTest <= limit;
```

它被 `ConservativeIntersect`（165 行）与 `ContainsPoint` / `ContainsRect`（193-200 行）使用，调用方为 `src/core/ClipStack.cpp:294`、`:365`、`:376` 与 `src/core/GeometryShape.cpp:131`、`:145`。

导出侧：`src/svg/SVGExportContext.cpp` 对 simple 类型输出 `<rect rx ry>`，complex 类型转path；`src/pdf/PDFExportContext.cpp` 一律走 `path.addRRect`。

### 2.6 矢量元素层

`include/tgfx/layers/vectors/Rectangle.h` 的 `_roundness` 是 `std::array<float, 4>`（标量半径）。`src/layers/vectors/Rectangle.cpp:73-99` 的 `apply()` 把它转为 `std::array<Point, 4>`，经 `RRect::MakeRectRadii` 归一化后 `path.addRRect(rRect, _reversed, 2)`，结果缓存在 `_cachedShape`。

`RoundCorner`（`src/layers/vectors/RoundCorner.cpp`）走 `PathEffect::MakeCorner`，是对任意 path 的通用倒角，与矩形圆角是两套机制。

---

## 3 参考设计

### 3.1 数学定义

平滑圆角（squircle / continuous corner）的动机是普通圆角的曲率不连续：直边曲率为 0、圆弧曲率为 1/r，拼接点处曲率跳变（仅 G1 连续），视觉上呈现可察觉的折点。平滑圆角要求 G2 连续——曲率从 0 连续爬升到 1/r，再连续降回 0。该定义与具体库无关，Ardot（3.2 节）与 Figma（3.3 节）的实现均为其参数化实例。

**曲线族定义。** 平滑角以 `(r, s)` 为参数（r 为半径，s ∈ [0,1] 为平滑度），轮廓为对称三段式：

```
直边 ── 入过渡曲线 ── 保留圆弧 ── 出过渡曲线 ── 直边
        κ: 0→1/r      κ = 1/r      κ: 1/r→0
```

90° 转向角按 s 在圆弧与过渡曲线之间重新分配：

```
每段过渡曲线转角:  (π/4)·s
保留圆弧扫角:      (π/2)·(1−s)
校验: 2·(π/4)·s + (π/2)·(1−s) = π/2
```

角在每条边上的占位（footprint）为 `(1+s)·r`：轮廓偏离直边的起点距顶点 `(1+s)·r`，而非普通圆角的 `r`。两个退化端点：s=0 时过渡曲线消失，退化为普通圆弧圆角；s=1 时圆弧扫角为零，整角由两条过渡曲线直接对接（squircle 形态）。

**过渡曲线构造。** 在角局部坐标系（顶点为原点，x 轴沿入边指向矩形内部、y 轴为内法向）中，第一段过渡曲线是三次贝塞尔，其控制点由四个长度量决定（均随半径 r 线性缩放，系数只依赖 s）：

- `p`：footprint，过渡起点到顶点的边向距离，即轮廓离开直边的位置；
- `w`：控制点步进，过渡起点到切线交点（直边切线与圆弧切线的交点）的总跨度为 `3w`；
- `e`：圆弧起点相对切线交点继续向顶点的边向偏移；
- `g`：圆弧起点抬离直边、进入矩形内部的法向高度。

控制点坐标：

```
b0 = (p, 0)            过渡起点，轮廓在此离开直边
b1 = (p − 2w, 0)       起点到切线交点的 2/3 处
b2 = (p − 3w, 0)       直边切线与圆弧切线的交点
b3 = (p − 3w − e, g)   保留圆弧的起点（arcStart）
```

前三个控制点共线于直边，保证起点处一阶、二阶导数均沿边方向，即起点曲率为 0（与直边 G2 对接）。四个长度量随 s 的闭式定义（r 的线性比例，实现侧以 5.2 节的单位半径系数为唯一真源）：

```
p/r = 1 + s
e/r = tan(π·s/8)·cos(π·s/4)
g/r = (e/r)·tan(π·s/4)
w/r = [ (1+s) − √2·sin(π·(1−s)/4) − (1 + tan(π·s/4))·(e/r) ] / 3
```

代入贝塞尔公式后参数方程塌缩为：

```
X(t) = p − 6w·t + 3w·t² − e·t³
Y(t) = g·t³
```

**隐式形式。** 定义两个组合坐标（它们就是曲线的参数多项式本身，把 X(t) 代入即得）：

```
ν = y/g                    = t³
β = (x − p + (e/g)·y)/(3w) = t² − 2t
```

隐式方程即 t³ 与 t²−2t 之间的代数关系——展开 `β³ = t³(t−2)³ = t⁶ − 6t⁵ + 12t⁴ − 8t³`，与 `ν² = t⁶`、`6βν = 6t⁵ − 12t⁴`、`8ν = 8t³` 恰好逐项相消：

```
f(x, y) = ν² − 6βν − 8ν − β³ = 0        f < 0 为形状内部
```

三条性质使它可用于 NonAA 的符号判定（定性不需距离，无需牛顿迭代）。其一，`Y(t) = g·t³` 是双射——严格单调的三次函数，任意 y 可唯一反解 `t = ∛(y/g)`，反之亦然。这是符号判定成立的前提：参数曲线消去 t 隐式化后，代数方程的零点集可能**大于**原曲线——多出满足 f = 0 但不对应任何实参数 t 的幻影分支；一旦存在，f 的符号会在没有轮廓的地方翻转，NonAA 判定出错。双射性排除了这种可能：每个 y 唯一确定 t、进而唯一确定 x，代数曲线因此是单值函数图像 `x = φ(y)`；固定 y 后 f = 0 虽是关于 β 的三次方程（理论上有三个实根的空间），但多余的根不对应实参数，幻影分支不存在（实点必对应实参数），此结论由 `verify_smooth_corner_math.py` 的第 7 项检查数值确认。其二，归一化后系数均为 O(1)，f 与其梯度同比缩放，s 趋零时（g∝s²、w∝s）符号判定的几何误差仍在 1e-7 量级。其三，x 轴上 `f = −β³`，符号恰在过渡起点 x = p 处翻转。组合变量不是人为技巧：直接摊开到 x、y 会得到 10 个单项的散乱三次式，结构完全不可见，且小 s 时系数病态。

出过渡曲线与入过渡曲线关于角平分线对称；保留圆弧以角平分线为中轴、扫角 `(π/2)(1−s)`，cubic 逼近手柄为 `(4/3)·tan(arcSweep/4)·r`。数值例：100×100 矩形、r=20、s=1 的右上角（顶点 (100, 0)），`w = 20√2/3`、`e = g = 10(2−√2)`，第一段过渡曲线的控制点为 `(60, 0)`、`(60+40√2/3, 0)`、`(60+20√2, 0)`、`(80+10√2, 20−10√2)`，约为 `(60, 0)`、`(78.86, 0)`、`(88.28, 0)`、`(94.14, 5.86)`；此时 `arcStart == arcEnd`，两段过渡曲线在角平分线上的该点对接。

**椭圆角。** xy 半径不等的椭圆角不在椭圆上直接分配角度，而是先在半径 r_x 的圆形框架中构建平滑角，再做逐轴仿射缩放 `(x, y) → (x, y·r_y/r_x)`。仿射映射逐轴单调，参数角区间 `[0, π/2]` 在映射下不变，因此任意离心率下角轮廓都不会越出所在象限；但曲率连续性在非等比缩放下不再严格成立，椭圆角的平滑属于视觉近似。

**可实现性与投影。** 一条边长为 L 的边承载两个角的 footprint 区间，曲线族可实现的充要条件是它们不相交：

```
(1+s)·(r_a + r_b) ≤ L    （对每条边）
```

参数落在可行集之外时投影回可行集。按"半径是主几何、平滑度是调制"的主次关系取字典序投影——先压平滑度：

```
s' = min( s,  min_e [ L_e/(r_a+r_b) ] − 1 )
```

footprint 关于 (1+s) 线性，压 s 即等比缩小角区，半径保持不变；取等号时两段过渡曲线的起点在边上相遇（直线段长度为零、曲率为 0 对接），轮廓仍为简单闭合曲线。若 s 归零后仍不可行（裸半径之和超出边长），退化为普通圆角的经典半径缩放规则，与平滑度无关。

### 3.2 Ardot 的CPU 实现

核心文件 `packages/common/editor/geometry/src/path/utils/corner_builder.cc`（861 行）。参数 `cornerSmoothing` 内部取值 `[0,1]`（UI 为 0~100%），默认 0，四角统一，半径每角独立。

`SmoothRoundCorner()`（479-570 行）输出五段：截断的起始边、过渡曲线、圆弧、过渡曲线、截断的终止边。

切线距离（274-286 行）：

```cpp
float ComputeTangentDistances(pk::SkVector v1, pk::SkVector v2, float radius) {
  auto dot_product = std::max(-1.0f, std::min(1.0f, v1.dot(v2)));
  if (pk::SkScalarNearlyEqual(dot_product, 1.0f, TOLERANCE)) {
    return std::numeric_limits<float>::max();
  }
  auto half_angle = std::acos(dot_product) / 2.0f;
  auto tan_half_angle = std::tan(half_angle);
  if (pk::SkScalarNearlyZero(tan_half_angle, TOLERANCE)) {
    return std::numeric_limits<float>::max();
  }
  return radius / tan_half_angle;
}
```

平滑后按`(1 + smoothness)` 扩展（508-510 行）：

```cpp
auto start_tangent_distance = std::min(smooth_tangent_distance * (1.f + smoothness), start_curve_length);
auto end_tangent_distance = std::min(smooth_tangent_distance * (1.f + smoothness), end_curve_length);
```

控制点比例（184-186 行）：

```cpp
// 创建平滑曲线，控制点距离端点的比例为2/3
// 参考文档 https://www.figma.com/blog/desperately-seeking-squircles/
constexpr float kControlPointRatio = 2.f / 3.f;
```

过渡曲线的第二控制点取两条切线的交点，第一控制点位于起点到交点的 2/3 处（250-272 行的 `CreateSmoothCurve`）。

圆弧按弧长截断（366-368 行）：

```cpp
auto start_distance = arc_length * smoothness * 0.5f;
auto end_distance = arc_length - start_distance;
auto segment_result = arc_curve.getSegment(start_distance, end_distance);
```

圆弧的三次贝塞尔控制柄长度（288-304 行）：

```cpp
auto handle_length = (4.0f * (1.0f - std::cos(half_angle))) / (3.0f * sin_half_angle);
```

两级降级（744-781 行的 `ResolveEdgeCompetition`）：先按 `edge_length / total_base_length - 1` 削减 smoothing；若该值仍为负，则把 smoothing 置零并按 `corner_length / total_base_length * edge_length` 缩减圆角长度。

Ardot 不支持阴影扩散与平滑圆角共存。`packages/common/editor/style/src/effect/utils/effect.cc:114` 的 `supports_shadow_spread` 对矩形转交 `rectangle_supports_spread`，其测试用例明确断言这一点：

```cpp
TEST_F(SupportsShadowSpreadTest, rectangle_corner_smoothing_not_supported) {
  auto rect = createTestNode(Schema::NodeType::RECTANGLE, [](const entity::PROPERTY& prop) {
    entity::Set_cornerRadius(prop, 12.0f);
    entity::Set_cornerSmoothing(prop, 0.6f);
  });
  EXPECT_FALSE(style::supports_shadow_spread(rect));
}
```

### 3.3 Figma 的 GPU 实现

调研基于 `FigmaSource/raw.wasm`（约 47 MB，内含全部 shader 源码字符串与管线符号名；uniform 块字段名不做压缩，可全文检索核实）。

#### 3.3.1 总览

先说明方法论边界：wasm 字符串层能提供的是**shader 清单与能力**（有哪些 shader、每个能做什么，读源码即可核实）；而**场景到 shader 的分发**（画某个形状时实际调用哪个 shader）由 CPU 侧代码决策，编译为 wasm 字节码，字符串层不可见——分发结论只能靠"能力匹配"推断，最终确认需要抓帧。下表的结论类型据此标注：

| 场景 | 适用形状 | 处理方式 | 结论类型 | 证据 |
|---|---|---|---|---|
| 简单填充 | 四角统一半径的圆角矩形 / 椭圆，无平滑度 | 快路 shader 硬裁剪填充，无软边 | 能力实证；分发推断 | 22 个 paint 变体源码一致；是否承担主画布渲染未确认 |
| 复杂填充 | 逐角半径、平滑角、任意 path | 矢量管线（stencil 环绕数计数 + Loop-Blinn cover） | 管线实证；归属推断 | 管线符号名实证；任何填充 shader 都不支持逐角半径（实证） |
| 描边 | 实线描边（含矩形描边） | 无专用描边 shader；推断轮廓化为填充后走矢量管线。例外：虚线有专用 shader | 推断 | 快路无描边逻辑（实证）；轮廓化概念存在但归属不明；无法排除顶点烘焙宽度的方案 |
| 玻璃特效 | 所有形状（特效作用于节点轮廓） | 平滑角解析 SDF（逐角半径 + 逐角平滑度）；另有高度图纹理变体 | 实证 | 字段名、π 常量、四角并集三重指纹只命中折射 shader |
| 阴影特效 | 快路：标量半径圆角矩形；任意形状走纹理模糊 | 解析阴影 shader 家族（阴影与遮罩各自携带形状参数重新求值，模糊为解析积分）+ 通用纹理模糊管线（`texture0` + `blurDelta` 分离式高斯） | 实证（两条管线均核实源码）；平滑角阴影的归属为推断 | `figma_shaders/ShadowShaders_all.txt` 与 wasm 内纹理模糊 shader，详见 3.3.6 节 |
| AA 软边 | 各场景的边缘抗锯齿 | 各 shader 的 AA 能力可核实，但绘制时 AA 在哪一层产生不可见 | 未知 | 快路无软边代码（实证）；分发不可见 |

对本课题最重要的一点：**平滑角 GPU SDF 在 Figma 里只服务玻璃特效**（实证：`cornerRadii` / `cornerSmoothings` 字段名、π 常量、"四角并集"代码结构三重指纹检索全部只命中折射 shader），**普通填充路径没有任何平滑角求值能力**（连逐角半径的填充 shader 都不存在）。因此平滑角矩形的填充无论在何处渲染，平滑度都必须在 CPU 侧先烘焙进几何；推断其走矢量管线（CPU 展平），未经抓帧实证。

#### 3.3.2 简单填充：快路 shader（能力实证 / 分发推断）

`uShapeType` 家族指 **uniform 块中带 `int uShapeType` 字段的全部 shader 变体**的统称：同一套形状裁剪代码 × 多种 paint（纯色、纹理、色彩转换等），raw.wasm 中共 22 个变体，全部硬 `discard`、全部标量半径、全部无平滑度，无一软边逻辑。其能力匹配四角统一半径的圆角矩形与椭圆填充。其中纹理填充变体的形状裁剪部分（从 raw.wasm 提取后重新排版并加注释，标识符保持压缩原样；`main` 中与形状无关的上色代码略去）：

```glsl
uniform int uShapeType;
uniform float cornerRadius;
uniform vec2 rectSize;
varying vec2 _vertexCoord2;
const int bc = 1;
const int bd = 2;

bool ax(vec2 ay, vec2 az, float ba) {           // 检查点是否在圆角矩形内：归一化坐标、尺寸、半径
  vec2 bb = abs(ay * az - az / 2.0) - (az / 2.0) + ba;
  return min(max(bb.x, bb.y), 0.0) + length(max(bb, 0.0)) <= ba;
}

void be() {
  if (uShapeType == bc) {                        // 圆角矩形
    if (!ax(_vertexCoord2, rectSize, cornerRadius)) { discard; }
  } else if (uShapeType == bd) {                 // 椭圆
    vec2 bf = vec2(0.5, 0.5) - _vertexCoord2;
    if ((bf.x * bf.x) / 0.25 + (bf.y * bf.y) / 0.25 > 1.0) { discard; }
  }
}

void main() { be(); /* 上色随 paint 变体而不同 */ }
```

注意 `ax()` 就是矩形 SDF 的内外判定形式（圆形角的偏移恒等式），只取符号不要距离，所以无需梯度校正。

边界：以上是该 shader 家族的能力描述（实证）。"主画布绘制四角统一矩形时实际使用它"是推断——分发决策在 CPU 侧字节码中，它也可能仅供缩略图、导出等低保真场合使用。

#### 3.3.3 复杂填充：矢量管线（管线实证 / 归属推断）

逐角半径、平滑角、描边轮廓、任意 path 等形状，推断由矢量管线渲染。推断依据：填充 shader 侧的能力缺口是实证——快路家族只认标量半径（3.3.2 节），且全文检索核实**只有玻璃折射 shader 使用了逐角半径**，任何填充 shader 都不支持；而矢量管线是 wasm 中唯一消费几何的通道。wasm 符号实证了它的完整环节：

- CPU 侧展平与组装（`render-stencil-geometry`、`flatten*` 系列符号）：弧用弦代替拆成 stencil 三角形，曲线段生成带 uv 参数的曲线三角形；平滑度、逐角半径等参数在此烘焙进贝塞尔曲线，GPU 侧完全不感知。
- stencil 环绕数计数（`_nonZeroWindingRuleUsages` / `_evenOddWindingRuleUsages`，两种填充规则各一套）：按三角形朝向累加环绕数，得到保守的内部区域。
- cover 阶段曲线精修与上色（Loop-Blinn shader 家族 `loopBlinnSolidPaint` / `loopBlinnNoDiscard` / `loopBlinnInstanced` / `loopBlinnGlyphsUShort/UInt`，字形也走它，按 `stencilState` 等参数组合变体）：stencil 三角形不再重画，cover 只画两类几何——曲线三角形（每曲线段一个，逐像素求隐式方程，二次贝塞尔即 `u²−v`，精修弦多算的月牙区）与罩住包围盒的大 quad（不做曲线计算，靠 stencil 测试挡住外部，统一给内部上色）。内部与直边在 cover 阶段没有对应三角形，完全由 stencil 蒙版 + 大 quad 承接；纯直线形状没有曲线段，cover 退化为只有大 quad 一批。

边界：这条管线的存在与各环节符号为实证；"平滑角矩形运行时经它渲染"是推断（快路表达不了 + 产品截图证明该能力存在），具体 stencil 修正时序未抓帧核实。

#### 3.3.4 描边（推断）

Figma 如何渲染描边，现有证据不足以定论。能确定的只有两点：快路 shader 无描边逻辑（实证）；127 个 uniform 块无 stroke 宽度字段（实证，但只能排除 uniform 传宽度的方案——宽度也可以像 tgfx 的 `RectsVertexProvider` 那样烘进顶点，此类 shader 经字符串检索无法排除）。

在此之上的推断是：描边在上游轮廓化（outline stroke）成填充 path 后走矢量管线。间接迹象是代码库存在"描边轮廓合并"概念（符号 `dontMergeStrokeOutlines`，但归属不明，可能是渲染步骤也可能是 Outline Stroke 编辑命令）。若成立，纯直线轮廓（如矩形描边环）没有曲线段，Loop-Blinn 精修批次为空，退化为 stencil 计数 + quad 上色两批。

唯一的例外是虚线专用 shader（实证，源码已读）：

```glsl
void main() {
  vec2 d = _coord4.xy;  vec2 e = _coord4.zw;
  float f = min(min(d.x, d.y), min(e.x - d.x, e.y - d.y));   // 到四边的距离
  float g = dash + gap;
  float h = max(1.0, floor(e.x / g + 0.5));                  // 周期数取整
  f = min(f, 0.5 + abs(fract(d.x * h / e.x) - 0.5) * g - gap * 0.5);  // 叠上虚线周期
  gl_FragColor = _color4 * min(f / subpixel, 1.0);           // subpixel 软边
}
```

它的几何（细长 quad）由上游给好，shader 只做沿长度方向的 dash 周期裁剪与软边，不承担"填充矩形转描边环"。

#### 3.3.5 玻璃特效：平滑角 GPU SDF（实证）

平滑角的 GPU 解析求值只存在于玻璃折射 shader，分两组变体：解析 SDF 现场求值版（带 `cornerSmoothings`）与预烘焙高度图纹理版（带 `bevelExpansionScale` 等参数）。以下代码由 `tools/extract_figma_smooth_shader.py` 从解析版中提取并展开，变量名保持提取原样（均为压缩后的短名）。

"这是玻璃特效 shader"的判定依据就在它的 uniform 块与 main 里（`refractionDistance`/`bevelSize`/`chromaticAberration` 等字段名不做压缩）：

```glsl
uniform UniformBlock {
  mat3 nodeToTileSpace; mat3 tileToNodeSpace; vec4 quad;
  vec4 cornerRadii; vec4 cornerSmoothings; vec4 tintColor;
  vec2 nodeSpaceCenterPoint; int hasUniformCorners;
  float tileExpansionScale; float splay; float frameRotation;
  float refractionIntensity; float refractionDistance;
  float lightIntensity; float lightAngle; float bevelSize;
  float chromaticAberration;
}

void main() {
  vec2 ec = (tileToNodeSpace * vec3(_coord2, 1.0)).xy;   // 像素 → 节点空间
  vec2 ee = nodeSpaceCenterPoint;                        // 矩形半尺寸
  vec2 eg = ec - nodeSpaceCenterPoint;                   // 中心为原点的坐标
  float eh = ds(eg, ee);                                 // 形状 SDF（平滑角距离）
  // 折射：按距离 eh 计算偏移强度，沿距离场梯度方向偏移采样位置
  float ei = cx(-refractionDistance - bevelSize, -bevelSize, eh);
  float ej = pow(ei, ct) * refractionDistance;
  float ek = 0.999 * refractionDistance;
  float el = clamp(ej * refractionIntensity, -ek, ek);
  float em = refractionIntensity * cs * ek;
  float en = cx(-bevelSize - 0.0001, -bevelSize * 0.3, eh);
  vec2 eo = dz(eg, ee, max(bevelSize, refractionDistance));  // 距离场梯度 → 法线
  vec2 dg = -eo * mix(el, em, en);                           // 折射偏移量
  vec2 e = ec + dg;
  vec4 ep;
  if (chromaticAberration < 0.001) { ep = dd(e); }           // 直接采样背景
  else { ep = df(e, dg, chromaticAberration); }              // 色散：RGB 错位采样
  // 倒角高光：法线 / 角部方向与光照方向点积，按到边缘距离衰减
  float es = smoothstep(-bevelSize - 0.001, -bevelSize * cu, eh);
  float et = lightAngle - frameRotation + bv;
  vec2 eu = dt(eg, ee, cornerRadii);
  vec2 ev = vec2(cos(et), sin(et));
  ep.xyz += smoothstep(cq, 1.0, dot(eu, -ev)) * es * eq;
  ep.xyz += smoothstep(cq, 1.0, dot(eu,  ev)) * es * eq * cr;
  float ex = pow(cx(-refractionDistance, -bevelSize + 0.0001, eh), 2.0);
  ep.xyz += dot(eo, ev) * ex * cv * eq;
  fig_FragColor = ep;
}
```

main 中与形状相关的调用有三个：`ds()` 求平滑角距离、`dz()` 求距离场梯度（法线）、`dt()` 求角部方向。以下展开这条工具函数链，形状参数入口是逐角独立的 `vec4 cornerRadii` 与 `vec4 cornerSmoothings`。

分发函数按四角是否统一选择快捷分支：

```glsl
float ds(vec2 e, vec2 f) {
  if (hasUniformCorners == 1) {
    return ch(e, f, cornerRadii.x, cornerSmoothings.x);
  } else {
    return cn(e, f, cornerRadii, cornerSmoothings);
  }
}
```

四角取并集（对每角做象限镜像后取 max）：

```glsl
float cn(vec2 e, vec2 f, vec4 co, vec4 cp) {
  float g = d(e, f);
  vec2 m = f * 2.0;
  g = max(g, ch(e * vec2( 1.0,  1.0) + f, m, co.x, cp.x));
  g = max(g, ch(e * vec2(-1.0,  1.0) + f, m, co.y, cp.y));
  g = max(g, ch(e * vec2(-1.0, -1.0) + f, m, co.z, cp.z));
  g = max(g, ch(e * vec2( 1.0, -1.0) + f, m, co.w, cp.w));
  return g;
}
```

单角分发含两级降级，与 Ardot 的 `(1 + smoothness)` 扩展同构：

```glsl
float ch(vec2 ak, vec2 f, float bs, float bt) {
  if (bs <= 1e-6) {
    return d(ak, f);              // 半径为零，退回矩形
  }
  float ci = min(f.x, f.y);
  float cj = bs * (1.0 + bt);     // 曲线连接点偏移
  float ck = max(0.0, cj - ci) / bs;
  float cl = max(0.0, bt - ck);   // 第一级：削减平滑度
  float cm = min(ci, bs);         // 第二级：夹住半径
  if (cl <= 1e-6) {
    return h(ak, f, cm);          // 平滑度耗尽，退回标准圆角
  }
  vec2 e = abs(ak) - f;
  return br(e, cm, cl);
}
```

其中的基础 SDF：

```glsl
float d(vec2 e, vec2 f) {                       // 矩形
  vec2 g = abs(e) - f;
  return length(max(g, vec2(0.0))) + min(max(g.x, g.y), 0.0);
}

float h(vec2 e, vec2 f, float i) {              // 标准圆角矩形
  float j = min(i, min(f.x, f.y));
  return d(e, f - j) - j;
}
```

平滑角 SDF 构造三段贝塞尔并求精确距离：

```glsl
float br(vec2 ak, float bs, float bt) {
  float bu = 3.14159265359;
  float bv = bu * 0.5;
  float bw = 1.41421356237;
  float bx = 0.5 * bv * bt;          // 过渡曲线转角
  float by = bv * (1.0 - bt);        // 保留圆弧扫过角
  float bz = (1.0 + bt) * bs;        // 曲线连接点偏移
  float ca = tan(bx);
  float cb = sin(by * 0.5) * bs * bw;
  float x = bs * tan(bx * 0.5) * cos(bx);
  float g = x * ca;
  float w = ((bz - cb) - (1.0 + ca) * x) / 3.0;
  float v = 2.0 * w;
  ak = n(bu * -0.25) * ak;
  ak.x = -abs(ak.x);
  vec2 cc = ak + vec2(0.0, 1.414213) * bs;
  float cd = atan(cc.x, cc.y);
  ak = n(bu * 0.25) * ak;
  vec2 al = vec2(-bz, 0.0);
  vec2 am = al + vec2(v, 0.0);
  vec2 an = am + vec2(w, 0.0);
  vec2 aa = an + vec2(x, -g);
  vec2 ce = ak + bz;
  if (ce.x < 0.0) {
    return ce.y - bz;                // 边线线性段
  } else if (abs(cd) < by * 0.5) {
    return length(cc) - bs;          // 圆弧扇区
  }
  float cf = bq(ak, al, am, an, aa);
  float cg = -aj(ak, al, am, an, aa);
  return cf * cg;
}
```

到三次贝塞尔的距离用牛顿迭代，`s = 3` 个初值各迭代 `r = 2` 次后取最小值：

```glsl
const int r = 2;
const int s = 3;
const float t = 1.0;

float ba(float o, vec2 bb, vec2 bc, vec2 bd, vec2 be) {
  vec2 bf = bd + o * be;
  vec2 bg = bc + o * bf;
  vec2 bh = bf + o * be;
  vec2 bi = bb + o * bg;
  vec2 bj = bg + o * bh;
  float bk = dot(bj, bj);
  float bl = dot(bj, bi);
  return o - t * bl / bk;
}

float bq(vec2 ak, vec2 al, vec2 am, vec2 an, vec2 aa) {
  return sqrt(bm(ak, al, am, an, aa));
}

float bm(vec2 ak, vec2 al, vec2 am, vec2 an, vec2 aa) {
  vec2 be = (-al + 3.0 * am - 3.0 * an + aa);
  vec2 bd = (3.0 * al - 6.0 * am + 3.0 * an);
  vec2 bc = (-3.0 * al + 3.0 * am);
  vec2 bb = al - ak;
  float bn = 1.0e38;
  float bo = 0.;
  float o;
  for (int au = 0; au < s; au++) {
    o = bo;
    for (int bp = 0; bp < r; bp++) {
      o = ba(o, bb, bc, bd, be);
    }
    o = clamp(o, 0.0, 1.0);
    vec2 bi = ((be * o + bd) * o + bc) * o + bb;
    bn = min(bn, dot(bi, bi));
    bo += 1. / float(s - 1);
  }
  return bn;
}
```

内外符号由三次方程求根加射线交叉计数得出（`aj` 与其调用的 `u`，篇幅较长，见提取脚本输出）。

单个fragment 命中过渡曲线段时的成本为：6 次牛顿求值，加一次三次方程解析求根，加若干三角函数。

#### 3.3.6 阴影特效（实证）

阴影是独立的 shader 家族（共四个 uniform 块，提取自 `figma_shaders/ShadowShaders_all.txt`）。内阴影变体的 uniform 与 main：

```glsl
uniform UniformBlock {
  mat3 vertexTransform;
  vec4 shadowBounds; vec4 maskBounds; vec4 color;
  vec2 maskHalfSize; vec2 shadowHalfSize;          // 遮罩与阴影各自独立的半尺寸
  float blurRadius;
  float maskCornerRadius; float shadowCornerRadius; // 各自独立的标量半径
  float invBlurRadius;
  int useAAAMask;                                   // 遮罩 AA 开关
}

void main() {
  float bx = 1.0 - x(_centeredNormShadowBlurCoords,
                     shadowHalfSize * invBlurRadius,
                     shadowCornerRadius * invBlurRadius);   // 阴影模糊覆盖率（解析积分）
  float by = bv(_centeredMaskRectCoords, maskHalfSize,
                maskCornerRadius);                         // 遮罩 SDF（圆角矩形）
  float bz = bool(useAAAMask) ? bp(by)                     // AA：fwidth + smoothstep 软边
                              : 1.0 - step(0.0, by);       // 或硬边，uniform 切换
  fig_FragColor = d(color * (bx * bz));                    // 此 shader 的 d 是抖动函数
}
```

（压缩名逐 shader 独立分配：此处的 `d`/`bv`/`bp` 与玻璃 shader 的同名函数不是同一个。）

外阴影（模糊）变体的 main 更短，内部离边界足够远时直接全覆盖，否则调用同一个解析积分 `x()`：

```glsl
uniform UniformBlock {
  mat3 vertexTransform; vec4 quad; vec4 color;
  vec2 halfSizeOverBlurRadius;        // 矩形半尺寸 / 模糊半径
  float blurRadius;
  float cornerRadiusOverBlurRadius;   // 标量半径 / 模糊半径
}

void main() {
  float aj;
  if (all(lessThan(abs(_coord2),
                   halfSizeOverBlurRadius - vec2(cornerRadiusOverBlurRadius + 1.0)))) {
    aj = 1.0;    // 内部快捷路径
  } else {
    aj = x(_coord2, halfSizeOverBlurRadius, cornerRadiusOverBlurRadius);
  }
  fig_FragColor = d(color * aj);
}
```

与 3.3.5 节玻璃 shader 的复用关系：复用体现在内阴影的遮罩判定上（上面 main 里的 `bv()` 调用），两个函数体与玻璃的 `d()`/`h()` **逐字符一致，仅压缩名不同**——系同一套基础 SDF 代码各自编译压缩：

```glsl
float bs(vec2 bt, vec2 v) {                 // 矩形 SDF（同玻璃 d()）
  vec2 bu = abs(bt) - v;
  return length(max(bu, vec2(0.0))) + min(max(bu.x, bu.y), 0.0);
}

float bv(vec2 bt, vec2 v, float n) {        // 圆角矩形 SDF（同玻璃 h()）
  float bw = min(n, min(v.x, v.y));
  return bs(bt, v - bw) - bw;
}
```

复用只到基础 SDF 为止；平滑角函数链（`br`/`cn`/牛顿迭代）在阴影 shader 中一行都没有，外阴影模糊变体与玻璃 shader 则完全无共享。阴影独有的是模糊算法 `x()`/`k()`：帐篷滤波器与圆角矩形覆盖函数做 4 段数值积分，圆弧段覆盖有闭式解（圆弓形面积公式），不走距离场。

形状参数为**标量统一半径**（`cornerRadiusOverBlurRadius`、`maskCornerRadius` / `shadowCornerRadius`），不支持逐角半径，**不支持平滑度**——解析阴影 shader 只是简单形状的快路。任意形状（含平滑角）的阴影走**通用纹理模糊管线**（实证：`uniform sampler2D texture0; uniform vec2 blurDelta; uniform float count; const int a=64` 的分离式高斯模糊 shader 家族，输入是纹理而非形状参数）：形状先渲染成透明度纹理（平滑度已在矢量管线烘进几何），再做纹理模糊。"平滑角阴影实际走此路"为分发层面的推断。

##### spread 口径

语义结论（截图实测，`tools/measure_figma_shadow_corner.py`；被测形状为逐角半径，本就走纹理路径，故结论与渲染路径无关）：**spread 不改变角的性质，尖角仍是尖角，平滑度原值保留**——`圆角 = 0/30/0/0`、`spread = 17` 的形状，其尖角在扩散后未被圆化，否定了同心膨胀口径。

推测平滑角形状的阴影流程：CPU 侧生成应用 spread 后的 path（生成代码在 wasm 字节码中拿不到），经矢量管线渲染成纹理后做通用模糊。tgfx 对应链路（`SpreadUtils::MakeSpreadShapeImage` + `drawPath`）现成，需要对齐的只有上述几何语义（5.7 节）。

#### 3.3.7 AA 软边（未知）

各 shader 的 AA 能力可以核实：快路填充 shader 无软边代码（3.3.2 节）；阴影 shader 自带 `fwidth` + `smoothstep` 软边（3.3.6 节）；Loop-Blinn 隐式方程值可换算覆盖率（3.3.3 节）。但"绘制圆角矩形时实际走哪条路径、AA 在哪一层产生"是分发问题，wasm 字符串层不可见，**结论：未知**。

### 3.4 两种圆弧截断口径

Ardot 与 Figma 的控制点构造是同构的。以下等价关系由 `tools/verify_smooth_corner_math.py` 数值验证通过：

| 量 | Ardot | Figma | 关系 |
|---|---|---|---|
| 曲线连接点偏移 | `tangent_distance·(1+s)`，直角下 `tan(45°)=1` 故为 `(1+s)r` | `bz = (1+bt)·bs` | 相同 |
| 过渡曲线第二控制点 | 两切线交点 | `an = al + 3w·û`，即切线交点 | 相同 |
| 过渡曲线第一控制点 | 起点到交点的 `2/3` | `am = al + 2w·û` | 相同，均为 `2/3` |
| 过渡曲线转角 | 隐含 `(π/2 - φ)/2` | `bx = (π/4)·bt` | 代入 `φ=(π/2)(1-s)` 后相等 |
| 圆弧控制柄 | `4(1-cos θ)/(3 sin θ)·r` | `(4/3)·tan(θ/2)·r` | 代数恒等 |
| 圆弧截断口径 | 按弧长（`getSegment`） | 按角度（扇区判定） | 不同，存在亚像素级偏差 |

唯一的实质差异是圆弧的截断口径：**Ardot 按弧长截断**（对圆弧的三次贝塞尔近似做`getSegment`），**Figma 按角度截断**（`abs(cd) < by*0.5` 的扇区判定）。由于贝塞尔近似的弧长与圆心角并非严格成正比，两者存在亚像素级偏差。

### 3.5 Skia 对照

Skia core 没有圆角平滑度概念。`include/core/SkRRect.h:536-537` 的私有成员只有 `fRect`、`fRadii[4]`、`fType`，`Type` 枚举（:70-78）为 Empty/Rect/Oval/Simple/NinePatch/Complex；`SkCanvas` 只提供 `drawRRect(const SkRRect&, const SkPaint&)`（:1454）与 `drawRoundRect(rect, rx, ry, paint)`（:1565），均无平滑度入参。仓库中 squircle 与 superellipse 的命中都在 GM 测试内：`gm/fwidth_squircle.cpp` 的注释自述用途是验证 `fwidth()` 在各 GPU 配置上的行为，`gm/runtimeshader.cpp:432-452` 的 `ClipSuperRRect` 是超椭圆 `x^n + y^n = 1` 的 RuntimeShader 示例，与分段贝塞尔模型不同。故平滑度本身无可借鉴处。

RRect 的渲染格局（`src/gpu/ganesh/` 下核实）：Skia 的填充主路径已迁移到逐 fragment 求值，网格方案只留在描边侧：

| 场景 | 类名 | 类型 | 样式 | 说明 |
|---|---|---|---|---|
| 绘制 | `FillRRectOp`（内部 `FillRRectOp::Processor`） | Op | Fill | 单 quad 实例化，逐 fragment 求隐式方程 + `fwidth()` 硬件导数 AA，simple/complex/椭圆通吃 |
| 绘制 | `CircularRRectOp` | Op | Fill / Stroke | 圆形角网格 op（fill/stroke/overstroke 三种拓扑），`GrOvalOpFactory` 产出 |
| 绘制 | `EllipticalRRectOp` | Op | Fill / Stroke | 椭圆角网格 op，同上 |
| 阴影 | `ShadowRRectOp` | Op | — | ShadowUtils 阴影专用 |
| 裁剪覆盖率 | `GrRRectEffect` | FP | — | 圆角矩形裁剪覆盖率 |

---

## 4 对比分析

| 维度 | Ardot | Figma |
|---|---|---|
| 求值位置 | CPU 生成贝塞尔路径 | 矢量管线为主（CPU 展平 + GPU Loop-Blinn cover，分发为推断）；GPU SDF 仅用于玻璃特效（实证） |
| 平滑度粒度 | 四角统一单值 | 每角独立`vec4` |
| 半径形态 | 标量 | 标量 |
| 圆弧截断 | 按弧长 | 按角度 |
| 两级降级 | ✅ | ✅ |
| 阴影扩散共存 | ❌ 明确禁止 | ✅ 参数化重新求值 |

---

## 5 实现方案

### 5.1 平滑度只对圆形角定义

本方案确立的契约是：**平滑角只对圆形角（`rx == ry`）解析求值**，椭圆平滑角定义为圆形平滑角的各向异性缩放像，由 CPU 路径承载。

依据：Figma 的 `ch()` 接受的`bs` 是标量，`cornerRadii` 是四个标量；Ardot 的 `corner_radius` 与 tgfx 的 `Rectangle::_roundness` 同样是标量。标量是产生侧的天然形态。

由此，GPU 解析路径在以下情形返回 `nullptr` 交由上层降级：任一非零角 `rx != ry`；`matrix.getAxisScales()` 的两个分量不等（烘焙后会把圆形角变为椭圆角）。

### 5.2 唯一真源：单位半径系数

`br()` 中所有长度量都线性正比于半径，角度量只依赖平滑度。该性质由 `tools/verify_smooth_corner_math.py` 的第 1 项检查数值验证通过（对 `s∈ {0, 0.25, 0.5, 0.75, 1}` 与 `r ∈ {7, 40, 133}` 逐项比对）。

因此把角几何表达为"半径乘以只依赖平滑度的系数"，在 CPU 侧计算一次，作为 uniform 传入 shader。新增内部头文件 `src/core/SmoothCornerCoeffs.h`：

```cpp
// Per-unit-radius coefficients of a smoothed right-angle corner. Every length of the corner
// scales linearly with the corner radius, so they are stored as ratios that depend only on
// smoothness.
struct SmoothCornerCoeffs {
  float tangentRatio = 1.0f;
  float endOffsetX = 0.0f;
  float endOffsetY = 0.0f;
  float arcChordRatio = 0.0f;
  float controlStep = 0.0f;
  float arcHandle = 0.0f;
  float arcCosSquared = 1.0f;
};

// Computes the coefficients for the given smoothness, which is clamped to [0, 1].
SmoothCornerCoeffs ComputeSmoothCornerCoeffs(float smoothness);
```

对应 `br()` 的量：`tangentRatio` 为 `bz/bs`，`endOffsetX` 为 `x/bs`，`endOffsetY` 为 `g/bs`，`arcChordRatio` 为 `cb/bs`，`controlStep` 为 `w/bs`，`arcHandle` 为圆弧控制柄比例，`arcCosSquared` 为扇区判定用的 `cos²(φ/2)`。

三个消费方必须都从此处取值：`Path::addRRect` 的平滑分支、shader 的 uniform 打包、单测中的参考实现。这样 CPU 与 GPU 的差异被限制为"同一组系数下的两种求值方式"（显式贝塞尔与牛顿求距），而非两条独立定义的曲线。

选择角度截断作为真源的理由：它是闭式的，无需数值弧长积分，因而 CPU 与 GPU 能共用同一组系数。这是填充与描边、裁剪、导出之间轮廓一致的根本保障。代价是与 Ardot 现有渲染存在亚像素差异。

### 5.3 RRect 改造

`include/tgfx/core/RRect.h` 新增成员与接口：

```cpp
private:
  Rect _rect = {};
  std::array<Point, 4> _radii = {};
  float _smoothness = 0.0f;    // resolved value after edge competition, not the authored input
  Type _type = Type::Rect;
```

```cpp
static RRect MakeRectXY(const Rect& rect, float radiusX, float radiusY, float smoothness);
static RRect MakeRectRadii(const Rect& rect, const std::array<Point, 4>& radii, float smoothness);

void setRectXY(const Rect& rect, float radiusX, float radiusY, float smoothness);
void setRectRadii(const Rect& rect, const std::array<Point, 4>& radii, float smoothness);

float smoothness() const {
  return _smoothness;
}
```

不提供独立的 `setSmoothness`：`_radii` 存的是已被降级处理过的结果，事后单独设置平滑度需要基于原始半径重跑竞争，而原始半径此时已丢失，会得到非幂等的结果。

`operator==` 必须同步加入 `_smoothness`，否则 `RRectContent::onHasSameGeometry`、裁剪去重、合批判定都会把不同形状判为相同。

两级降级放在 `setRectRadii`，与 `ScaleRadii` 的职责分层一致：新增文件级静态函数 `ResolveSmoothness`，对四条边分别求 `edgeLength / (r_a + r_b) - 1`，取最小值并 clamp 到 `[0, 1]`。

`setRectRadii` 的新流程：

```
_rect = rect.makeSorted()
_radii = radii，负分量整角归零
_smoothness = ResolveSmoothness(_rect, _radii, smoothness)   // 第一级：削减平滑度
ScaleRadii(_rect, _radii)                                    // 第二级：缩减半径，逻辑不变
_type = ComputeType(_rect, _radii)
若 _type == Type::Rect 则 _smoothness = 0
```

顺序上`ResolveSmoothness` 必须先于 `ScaleRadii`，用未缩放的半径判断。若先跑 `ScaleRadii`，半径已被缩到刚好贴合边长，`ResolveSmoothness` 必然把平滑度削为零，两级降级会退化为一级。

一个自检推论：`Type::Oval` 时四条边都有 `r_a + r_b = edgeLength`，故 `ResolveSmoothness` 返回 0，即圆形与椭圆不可能带平滑度。

`ComputeType` 不改动：Type 描述半径分布形态，平滑度是正交属性，把它编入 Type 会让 `isComplex()` 的语义混乱。

`scale` 与 `offset` 都不改动平滑度。`scale` 的理由：平滑度的唯一约束是 `(1+s)(r_a + r_b) <= edgeLength`；水平边的约束涉及 `radii[i].x` 与 `width`，两者在 `scale(sx, sy)` 下同乘 `sx`，垂直边同理，约束两侧同比缩放故不变式恒保持。这与现有注释"正缩放保持全部分类不变式"的论证一致。`offset` 为平移，显然不变。

### 5.4 CPU 路径生成

设角顶点 `V`，入边单位方向 `u`（指向 `V`），该边指向形状内部的单位法向 `n`，半径 `r`，系数取 5.2 节：

```
P0 = V - tangentRatio·r·u                // 边上切点
P1 = P0 + 2·controlStep·r·u                      // 第一控制点，位于到切线交点的 2/3 处
P2 = P0 + 3·controlStep·r·u                      // 第二控制点，即两切线交点
P3 = P2 + endOffsetX·r·u + endOffsetY·r·n        // 过渡曲线终点，即圆弧起点
```

圆弧段在 `s < 1` 时存在，圆心为 `V + r·(n1 + n2)`，从 `P3` 到其关于该角平分线的镜像点 `P3'`，用单条三次贝塞尔表示，控制柄长度为 `arcHandle·r`，方向取两端点处的圆弧切向。第二段过渡曲线是第一段关于角平分线的镜像，控制点顺序反转。

`s == 1` 时圆弧退化为一点（`φ = 0`，`P3` 与 `P3'` 重合），只输出两段过渡曲线，与 Ardot `AppendSmoothTransitionCurves` 在 `smoothness != 1` 时才追加圆弧的行为一致。

`src/core/Path.cpp` 的 `addRRect` 改为：平滑度不大于零时走现有实现；否则走新增的文件级静态函数生成上述贝塞尔。

该函数需保持与圆角版相同的起点编号语义：圆角 rrect 有 8 个边界点（每角两个切点），平滑 rrect 每角同样有两个端点，只是距顶点为 `(1+s)r` 而非 `r`，故 `startIndex` 一一对应，`reversed` 反转遍历方向。这保证 `src/layers/vectors/Rectangle.cpp` 中 `addRRect(rRect, _reversed, 2)` 的语义不变。

椭圆角的处理：先按 `r = radii[i].x` 构造圆形平滑角，再对该角的全部控制点沿 y 方向乘以 `radii[i].y / radii[i].x`。三次贝塞尔在仿射变换下精确保持，故这是复用已生成中间产物的做法，无需按角度重新拟合。

`Path::isRRect` 经pathkit 的 rrect 检测反推，其要求每角是单条 conic 或 arc，而平滑角是三段 cubic，因此平滑 rrect 的 path 不会被误判为普通 rrect。这是隐式依赖，需由单测锁定。

### 5.5 GPU 方案

#### 5.5.1 单角求值

Figma 的 `cn()` 对四个角无条件各求一次再取 max。本方案复用现有 `GLSLRRectEffect` 的 arc-box one-hot 选角机制（2.4.3 节 Step 2），只把选择框从 `radii[i]` 加宽到 `tangentRatio·radii[i]`，因为平滑角轮廓延伸到 `(1+s)r`。

正确性前提是加宽后的四个角框互不相交，这由两处保证。同一条边上相邻两角由 5.3 节的降级保证 `(1+s)(r_a + r_b) <= edgeLength`。对角方向需分类型讨论：`Simple` 类型四角半径相同，同一条边上的两角即对角方向的两角，故边约束已等价于对角约束，加宽后不可能重叠；`Complex` 类型的对角重叠由 `HasDiagonalCornerAxisOverlap` 判定，其半径需同步改用 `(1+s)r`，命中则降级走 CPU 路径。注意该函数首行有 `if (!rRect.isComplex()) return false;`，因此它只覆盖 `Complex`，`Simple` 的安全性完全依赖上述边约束等价性，此结论由 `tools/verify_smooth_corner_math.py` 的对角框检查覆盖。

由此每个 fragment 至多求值一个角，且内部fragment 经`mix(edgeDist, cornerDist, inAnyCorner)` 完全跳过角求值。

#### 5.5.2 消除逐fragment 三角函数

依5.2 节，7 个系数在 CPU 侧算好后打成两个 `Float4` uniform 传入，shader 内不含任何三角函数。Figma 原版每个 fragment 需要三次 `tan`、一次 `sin`、一次 `cos`、一次 `atan`。

#### 5.5.3 内外符号

牛顿迭代已得到最近点参数 `t*`，可用 `sign(cross(B'(t*), p - B(t*)))` 求符号，替代 Figma 的三次方程求根加射线交叉计数。

该化简在最近点被clamp 到端点时垂直性不成立，符号可能出错。设计上两个端点分别落在边线段（由线性分支接管）与圆弧接合点（由扇区分支接管），SDF 在接缝处连续故不应暴露。此项标注为推测，必须在实现阶段由单测数值验证；若不成立则退回 Figma 的原始做法。

#### 5.5.4 shader 变体

照 2.4.3 节 `GLSLRectEffect` 的模式，以构造期确定的 bool 分变体。`src/gpu/processors/RRectEffect.h`：

```cpp
RRectEffect(const Rect& localRect, const std::array<Point, 4>& radii, float smoothness,
            const Matrix& deviceToLocal, bool antiAlias)
    : FragmentProcessor(ClassID()), localRect(localRect), radii(radii), smoothness(smoothness),
      antiAlias(antiAlias), _deviceToLocal(deviceToLocal),
      _needTransform(!deviceToLocal.isIdentity()), _hasSmoothness(smoothness > 0.0f) {
}

bool hasSmoothness() const {
  return _hasSmoothness;
}
```

`emitCode` 仅在 `hasSmoothness()` 为真时注入辅助函数（经 `fragBuilder->addFunction` 与 `getMangledFunctionName`）、添加系数 uniform、加宽 arc-box、把椭圆 SDF 替换为三分支平滑角 SDF。为假时生成的代码与现状完全相同，常规圆角矩形不承担任何额外成本。

#### 5.5.5 缓存键

`onComputeProcessorKey` 目前只写 `_needTransform`。若不把 `_hasSmoothness` 一并写入，带平滑度与不带平滑度的绘制会命中同一个缓存 program，先编译的那份决定行为，导致视觉错误且难以复现。改为位域打包：

```cpp
// Both flags change the generated shader code, so they must take part in the key. The smoothness
// value itself stays a uniform and is deliberately left out, otherwise every distinct value would
// produce its own program.
uint32_t key = _needTransform ? 1 : 0;
key |= _hasSmoothness ? 2 : 0;
bytesKey->write(key);
```

`GeometryProcessor::computeProcessorKey` 只写 classID 且无`onComputeProcessorKey` 钩子，故平滑变体必须是独立的 GeometryProcessor 类，而不能在现有类里加 bool。

#### 5.5.6 顶点布局与合批

现有 AA 路径用 4×4 顶点网格，其切线位于 `bounds.left + radii`，而平滑角轮廓延伸到 `(1+s)r`，会被网格裁掉外侧。故平滑 RRect 在 AA 与 NonAA 下统一采用单 quad 布局（4 顶点，6 索引），复用现有 NonAA 路径的索引缓冲形态；Figma 的玻璃特效 shader 也是单 quad 加 SDF 的形态（3.3.5 节）。5.5.1 节的 one-hot 选角已消除单 quad 带来的多角求值成本。

新增 `SmoothRRectsVertexProvider`，每角只需一个标量半径（四角共 4 个 float），比现有 complex 版的 `xRadii(4) + yRadii(4)` 更省。平滑度作为 uniform 而非顶点属性，因此同一个 op 内所有圆角矩形必须共享同一平滑度。

`ComplexMismatch` 之外新增一条打断条件，比较平滑度数值是否相等。代价是不同平滑度的圆角矩形无法合批（§1.2 已列为不在范围内）；实际使用中平滑度是设计系统级常量，同一界面基本统一，Figma 的 `hasUniformCorners` uniform 也反映了同样的假设。后续如需放开，可把系数移入顶点属性。

`RRectDrawOp` 增加 `hasSmoothness` 字段，`indicesPerRRect` 在其为真时取 `IndicesPerNonAARRect`，`onMakeGeometryProcessor` 在现有四路选择之前插入平滑分支。

FP 与 GP 两侧的 GLSL 生成代码抽为`src/gpu/glsl/GLSLSmoothCorner.{h,cpp}` 的自由函数共用，保证裁剪与填充的轮廓同源。

### 5.6 分发策略与性能结论

结论：**GPU 解析为主路径，CPU 路径为降级**，与现有常规圆角矩形的分发结构一致。

依据（区分事实与推测）：

已验证事实。其一，阴影扩散的 mask 由 `MakeSpreadShapeImage` 经 `PictureRecorder` 加`canvas->drawRRect` 生成，只有 `drawRRect` 走解析路径时扩散计算才留在 GPU shader 内完成；若对平滑度一律降级走 CPU，扩散 mask 就退回 CPU 光栅化。其二，解析路径可把至多 1024 个尺寸与半径各异的圆角矩形合入一个 draw call（`MaxNumRRects = 1024`），而 CPU 路径的合批要求 `getUniqueKey()` 完全相同且矩阵仅差平移（`OpsCompositor.cpp:202-224`），异构场景下无法合批，这是数量级差异。其三，CPU 路径依赖 `getUniqueKey()` 缓存，几何逐帧变化（尺寸或半径动画）时缓存收益为零，反而每帧付出三角化与缓存分配成本。其四，CPU 贝塞尔构造无论如何都要实现，因为它是 SVG/PDF 导出、椭圆角、非等比缩放的必需项，故"少写一份实现"不能作为支持 CPU 路径的论据。

推测部分。经 5.5节的三项优化后，逐 fragment 成本中只有命中过渡曲线段的部分需要牛顿迭代，圆弧扇区只需一次 `length()`（比现有椭圆 Sampson 距离更省，无梯度校正），边与中心区被 `mix` 跳过。过渡曲线区占比随半径与平滑度变化，需实测确认。

一并说明：单个大面积静态形状上，CPU 路径缓存命中后的 fragment 成本确实更低。本方案判断该收益不足以换取双主路径分发的复杂度与轮廓一致性风险，§1.2 据此把这项专项优化列为不在范围内；若后续实测证明它是热点，可作为独立优化项在 `UseDrawPath` 中按面积与静态性追加降级规则，不影响本方案结构。

`UseDrawPath` 在平滑度大于零时追加的降级条件：任一非零角 `rx != ry`；矩阵非等比缩放；以及现有全部条件，其中对角重叠判定的半径改用 `(1+s)r`。

### 5.7 阴影扩散

采用 Figma 的参数化路线（3.3.6 节）：扩散不对内容几何做同心膨胀，而是构造一组独立的形状参数重新求值。

这带来两项与现状不同的行为，均与 Figma 实测一致：其一，尖角在扩散后仍是尖角，不会被圆化；其二，平滑度随形状参数一起指定，不需要额外的传递公式。

`MakeSpreadRRect` 改为按参数化口径构造扩散后的形状参数：bounds 仍 `outset(distance)`；每个**非零**半径加上 `distance`（零半径角保持为零，即尖角不被圆化，这是现有实现已有的行为）；平滑度**原值传递**。若结果违反 `(1+s)(r_a + r_b) <= edgeLength`，交由 5.3 节 `setRectRadii` 内的两级降级自然处理。

关于平滑度原值传递的说明。`tools/verify_smooth_corner_math.py` 的第 4 项检查实测到：保持平滑度不变时曲线连接点偏移之和的增长快于边长增长，例如半径20 与 30、边长 100、平滑度 0.6，在扩散 20 时曲线连接点偏移之和为 144 而可用边长为 140，此时两级降级把平滑度降至 0.556；扩散 100 时降至 0.200，扩散 1000 时降至 0.024。即平滑度随扩散量增大而自然消退，这与大尺度膨胀趋于胶囊形的几何事实一致。本方案接受该行为，因为它与参数化语义相符：新参数只需自身满足约束，超出时按既有降级规则处理，无需引入两个参考实现都不具备的自创衰减公式。

`MakeSpreadShapeImage` 的形状识别问题：平滑 rrect 的 path 不会被 `path.isRRect` 识别（5.4 节），会落入复杂路径分支被降级为包围盒，导致阴影变成直角矩形。解决办法是让 `StyledShape` 携带结构化的圆角矩形：

```cpp
/**
 * The structured rounded rectangle this shape was built from, when available. Present only when
 * the shape is a single rounded rectangle in the same coordinate space as shape.
 */
std::optional<RRect> rRect = std::nullopt;
```

并为 `StyledShape::Make` 增加带 `RRect` 的重载。`MakeSpreadShapeImage` 的识别顺序改为优先取该字段，其余分支保留以服务未携带结构化信息的产生方。

坐标空间约束：`MakeSpreadShapeImage` 会先 `UnwrapMatrixShape` 剥离矩阵，故该字段必须与剥离后的 shape 同空间。这一约束写入字段注释。

产生方中，`SolidLayer` 与 `VectorLayer`（元素链只含单个 `Rectangle` 时）填充该字段；`ShapeLayer` 的 `_shape` 是任意 Shape，无平滑度概念，保持 `nullopt`；`Layer` 的兜底实现取紧包围盒矩形，同样保持 `nullopt`。

`DropShadowStyle::filterBounds` 不需要改动。理由：平滑角的起点落在边线上，圆弧与过渡曲线都朝形状内部弯曲，故包围盒恒等于 `rect()`，与圆角情形相同。该性质由 `verify_smooth_corner_math.py` 的第 5 项检查数值验证通过。`InnerShadowStyle` 与 `IsSpreadCollapsed` 同理不需改动，后者也依赖包围盒不变这一性质。

`InnerShadowStyle::drawWithSpread` 生成扩散量为 `0` 与 `-_spread` 的两张图，两者都经 `MakeSpreadShapeImage`，故上述改动完成后自动正确，该文件无需改动。

### 5.8 裁剪与保守判定

`RRectUtils` 的 `IsInnerToCorners` 写死椭圆方程（2.5 节），对平滑角不保守：椭圆测试可能把落在平滑轮廓之外的点判为在内，使 `ContainsPoint` 或 `ContainsRect` 错误返回真，进而让裁剪被错误地优化掉。

策略是在平滑度大于零时放弃解析判定：`ContainsPoint` 与 `ContainsRect` 返回假，`ConservativeIntersect` 返回 `nullopt` 拒绝合并。代价是失去裁剪栈层面的化简，但裁剪本身仍走 5.5 节的解析 FP，结果正确。`ClipStack` 与 `GeometryShape` 作为调用方随之获得正确行为，无需改动。

`GeometryShape::setRRect` 在无实际圆角时会折叠为 `Type::Rect`，此时 5.3 节已把平滑度规范化为零，折叠是安全的，由单测锁定。

导出侧：SVG 的 simple 分支输出 `<rect rx ry>` 无法表达平滑度，需在该分支入口判断平滑度并强制走 path 分支；PDF 一律走 `path.addRRect`，5.4 节改完后自动正确。

### 5.9 矢量元素层

`Rectangle` 新增平滑度接口，这是平滑度进入 layers 体系乃至阴影链路的入口：

```cpp
/**
 * Returns the corner smoothing of the rectangle. Defaults to 0.
 */
float cornerSmoothing() const {
  return _cornerSmoothing;
}

/**
 * Sets the corner smoothing shared by all four corners. Values are clamped to [0, 1]. A value of 0
 * produces a circular corner arc; larger values blend the arc into the adjacent edges over a
 * longer span. When an edge is too short to fit the smoothed corners at both of its ends, the
 * smoothing is reduced first, and the roundness is scaled down only after the smoothing reaches 0.
 * @param value corner smoothing applied uniformly to all corners
 */
void setCornerSmoothing(float value);
```

成员命名为 `_cornerSmoothing`，保存用户原始输入；`RRect::_smoothness` 保存竞争后的有效值。这与 `Rectangle::_roundness`（原始）对 `RRect::_radii`（已归一化）的现有关系一致。

`apply()` 改用三参工厂传入平滑度，`setCornerSmoothing` 需清空 `_cachedShape`，并把结构化的 `RRect` 经 `VectorContext` 传出以供 5.7 节使用。

`RoundCorner` 是对任意 path 的通用倒角，本次不改动（§1.2 已列为不在范围内）。

---

## 6 测试计划

### 6.1 单元测试

几何与降级：平滑度 clamp 到 `[0,1]`；第一级降级只削平滑度不动半径（200×200、四角 60、平滑度 1，期望平滑度降为 `200/120 - 1`、半径保持 60）；第二级接棒（四角 120、平滑度 0.8，期望平滑度为 0 且半径被缩至 100）；四角半径不等时取四条边的最小允许值；`MakeOval` 加平滑度后平滑度为 0；全零半径时类型为 `Rect` 且平滑度为 0；`scale` 与 `offset` 后平滑度不变且不变式仍成立；rect 与 radii 相同而平滑度不同时 `operator!=` 成立。

路径与真源一致性：平滑 rrect 的 path 的 verb 数落在预期区间（锁定 5.6 节的三角化前提）；其`isRRect` 与 `isOval` 均返回假（锁定 5.4 节的隐式依赖）；`startIndex` 对 0 到 7 的起点落点正确且 `reversed` 方向相反；按 5.2 节系数在 C++ 侧实现与 GLSL 等价的 SDF，沿 CPU 贝塞尔曲线采样点求值应近似为零，容差取亚像素，覆盖平滑度 0.2/0.5/0.8/1.0，这是轮廓一致性的核心锁定；验证 5.5.3 节的叉积符号法在过渡曲线段、圆弧接缝、边线接缝三处均给出正确符号，含端点 clamp 情形；系数在平滑度 0 与 1 两端的退化值正确。

变体与合批：平滑度为 0 与 0.5 的两个 `RRectEffect` 的 processor key 不同（锁定 5.5.5 节）；平滑度 0.3 与 0.7 的 key 相同（锁定数值不进key）；连续绘制平滑度 0、0.5、0.5 三个圆角矩形时前两者之间打断、后两者合批。

扩散：`MakeSpreadRRect` 对零半径角在正扩散下保持为零（尖角不被圆化，对齐 3.3.6 节实测）；平滑度原值传递；扩散量较大时由两级降级处理且结果自洽；负扩散使半径归零时角变尖且平滑度为 0；同 rect 与 radii、不同平滑度的 path 包围盒完全相同（锁定 5.7 节 `filterBounds` 无需改动的依据）。

裁剪与导出：`ConservativeIntersect` 对含平滑度的输入返回 `nullopt`；`ContainsPoint` 与 `ContainsRect` 返回假；SVG 导出结果不含 `<rect rx` 而改用 `<path`。

截图测试（归入既有绘制/裁剪测试的 cell 网格，不设独立测试文件，内容居中、坐标取整）：`CanvasTest/AARRectOp` 扩为四列并新增平滑用例——平滑度阶梯（同尺寸方块平滑度 0.25/0.5/1.0，与其余平滑度为 0 的 cell 像素对照）、四角圆形半径不等加平滑度（锁定解析 Op 路径，对角的曲线连接点偏移需留出 `(1+s)` 余量）、两级降级的三种状态并排、解析路径与 CPU 降级路径的并排对照（关键，验证轮廓重合）、仅因平滑度才发生的对角重叠回退、椭圆角与非等比缩放的回退（结果为拉伸的平滑圆角而非圆角矩形）、普通圆角与满平滑度的半透明叠画对照；`CanvasTest/NonAARRectOp` 网格化并新增 NonAA 平滑填充（均匀半径与四角半径不等）；`CanvasTest/NonAARRectOpStroke` 网格化并新增描边加平滑度的回退用例；`CanvasTest/RRectOpBatchSmooth` 以 30 个以上尺寸与半径各异但平滑度相同的形状端到端验证合批；`ClipTest/ClipOverview` 新增平滑圆角裁剪 cell（含等比缩放变体），锁定裁剪 FP 与绘制 GP 轮廓同源；平滑圆角配 `DropShadowStyle` 在扩散 0/10/30 下阴影保持平滑角，且零半径角在扩散后仍为尖角；配 `InnerShadowStyle` 在扩散 0/10 下的表现；负扩散使内腔消失时的退化表现。

截图基准的接受只能由用户执行 `/accept-baseline`，实现过程中不运行 `accept_baseline.sh`、不修改 `version.json`、不执行 `UpdateBaseline_*` 目标。

---

## 7 实现步骤

### 阶段 1 几何真源与 RRect

| 文件 | 说明 |
|---|---|
| `src/core/SmoothCornerCoeffs.h` / `.cpp` | 新增，5.2 节的唯一真源与 `ComputeSmoothCornerCoeffs` |
| `include/tgfx/core/RRect.h` | 新增 `_smoothness`、三参工厂与 setter、`smoothness()`，修改 `operator==` 及其注释 |
| `src/core/RRect.cpp` | 新增 `ResolveSmoothness`，`setRectRadii` 插入两级降级与 `Type::Rect` 规范化；`scale` / `offset` 补注释说明平滑度为何不变 |
| `test/src/RRectTest.cpp` | 几何与降级相关单测 |

### 阶段 2 CPU 路径生成

| 文件 | 说明 |
|---|---|
| `src/core/Path.cpp` | `addRRect` 新增平滑分支与文件级静态生成函数，含椭圆角缩放与 `reversed` / `startIndex` 语义 |
| `src/svg/SVGExportContext.cpp` | simple 分支在平滑度大于零时强制走 path |
| `test/src/RRectTest.cpp`、`test/src/PathTest.cpp` | 路径与真源一致性单测 |

### 阶段 3 GPU shader 核心与裁剪

| 文件 | 说明 |
|---|---|
| `src/gpu/glsl/GLSLSmoothCorner.h` / `.cpp` | 新增，FP 与 GP 共用的 GLSL 生成：辅助函数注入与三分支平滑角 SDF |
| `src/gpu/processors/RRectEffect.h` | 新增 `smoothness`、`_hasSmoothness`、`hasSmoothness()` |
| `src/gpu/processors/RRectEffect.cpp` | `onComputeProcessorKey` 打包变体位并扩写注释 |
| `src/gpu/glsl/processors/GLSLRRectEffect.cpp` | `Make` 新增圆形角与等比缩放拦截；`emitCode` 按变体分支；`onSetData` 条件传系数 |
| `test/src/…` | 符号法与变体 key 单测，裁剪截图 |

### 阶段 4 GPU 解析绘制路径

| 文件 | 说明 |
|---|---|
| `src/gpu/processors/SmoothRRectGeometryProcessor.h` / `.cpp` | 新增，独立 classID |
| `src/gpu/glsl/processors/GLSLSmoothRRectGeometryProcessor.h` / `.cpp` | 新增，复用 `GLSLSmoothCorner` |
| `src/gpu/RRectsVertexProvider.h` / `.cpp` | 新增 `SmoothRRectsVertexProvider`（单 quad、每角标量半径），`MakeFrom` 增加分派维度 |
| `src/gpu/ops/RRectDrawOp.h` / `.cpp` | 新增 `hasSmoothness` 字段，调整 `indicesPerRRect` 与 GP 选择 |
| `src/gpu/OpsCompositor.cpp` | 新增平滑度合批打断条件 |
| `src/core/Canvas.cpp` | `UseDrawPath` 新增降级条件，对角重叠判定改用 `(1+s)r` |
| `test/src/…` | 合批单测与主要截图用例 |

### 阶段 5 保守判定收口

| 文件 | 说明 |
|---|---|
| `src/core/RRectUtils.cpp` | `ContainsPoint` / `ContainsRect` / `ConservativeIntersect` 在平滑度大于零时早退 |
| `test/src/…` | 裁剪判定单测 |

### 阶段 6 阴影扩散

| 文件 | 说明 |
|---|---|
| `include/tgfx/layers/layerstyles/StyledShape.h` | 新增 `std::optional<RRect> rRect` 与 `Make` 重载 |
| `src/layers/layerstyles/StyledShape.cpp` | 实现新重载 |
| `src/layers/SpreadUtils.cpp` | `MakeSpreadRRect` 按参数化口径传递平滑度；`MakeSpreadShapeImage` 优先使用结构化字段 |
| `src/layers/SolidLayer.cpp`、`src/layers/VectorLayer.cpp` | 填充结构化字段 |
| `test/src/…` | 扩散单测与阴影截图 |

### 阶段 7 矢量元素层

| 文件 | 说明 |
|---|---|
| `include/tgfx/layers/vectors/Rectangle.h` | 新增 `cornerSmoothing()` / `setCornerSmoothing()` / `_cornerSmoothing` |
| `src/layers/vectors/Rectangle.cpp` | `apply()` 传平滑度、清缓存、经 `VectorContext` 传出结构化 `RRect` |

---

---

## 8 参考资料

| 内容 | 位置 |
|---|---|
| Ardot 平滑圆角算法 | `Ardot/packages/common/editor/geometry/src/path/utils/corner_builder.cc` |
| Ardot 扩散适用性判定 | `Ardot/packages/common/editor/style/src/effect/utils/effect.cc:114` 及其测试 |
| Figma 玻璃特效 shader（含平滑角 SDF） | 由 `tools/extract_figma_smooth_shader.py` 从 `shader_blocks.txt` 提取；全文检索基于 `FigmaSource/raw.wasm` |
| Figma 阴影 shader | `FigmaSource/figma_shaders/ShadowShaders_all.txt` |
| Figma 扩散口径实测 | `tools/measure_figma_shadow_corner.py` |
| 数学论断验证 | `tools/verify_smooth_corner_math.py` |
| 平滑圆角背景文章 | https://www.figma.com/blog/desperately-seeking-squircles/ |
