# 圆角平滑度（Corner Smoothing）技术方案

## 1 背景

### 1.1 问题描述

tgfx 的圆角矩形（`RRect`）目前只支持标准圆角：每角是一段椭圆弧，弧与相邻边在切点处切线方向连续（G1），但曲率从弧端的非零值突变为直边的 `0`；圆形角的弧端曲率为 `1/r`。设计工具普遍提供"圆角平滑度"参数，在弧与直边之间插入过渡曲线，改善曲率突变带来的视觉效果，得到俗称 squircle 的外观。

### 1.2 需求

1. `RRect` 支持圆角平滑度参数，四角统一单值，半径仍每角独立。
2. 普通 `Canvas` 绘制圆角矩形时支持平滑度。
3. `DropShadowStyle` 与 `InnerShadowStyle` 的 spread 支持平滑度。
4. 矢量元素层 `Rectangle` 暴露平滑度接口。
5. 平滑角矩形的描边在圆形角、宽度不超过任一非零角半径时走 GPU 解析路径。

以下能力明确不在本次范围内：

- `DropShadowFilter`：它没有 spread 参数，且作为滤镜只能拿到位图内容、拿不到矢量几何，无从对形状重新求值。
- `RoundCorner` 的通用多边形平滑：它作用于任意 path，支持平滑需要处理任意夹角的过渡曲线构造，而本方案的过渡曲线只针对直角定义。
- 不同平滑度之间的合批：平滑度经 uniform 传入 shader，同一 draw call 内所有圆角矩形必须共享同一数值。
- 椭圆角的描边解析化：扇区分派在圆形框架进行，仿射缩放后设备空间的最近特征归属改变，宽带下实测误差达数十像素（非收敛问题，加迭代无效），椭圆角描边维持 CPU 回退。
- 尖角（零半径角）进入解析路径：上游 `hasSharpCorner` 判据本次维持不变。
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

#### 3.1.1 平滑圆角

平滑圆角（squircle / continuous corner）的动机是改善普通圆角的曲率突变：直边曲率为 0、圆弧曲率为 1/r，连接处只有切线方向连续（G1），几何上没有折点，但曲率的突然变化会影响视觉过渡。严格的曲率连续（G2）还要求连接处两侧曲率相等。本节以三次贝塞尔控制点公式定义曲线：过渡曲线与直边 G2 连续，与保留圆弧切线方向连续，但曲率一般不相等，不能把这组构造表述为全轮廓 G2 连续。

**曲线族定义。** 平滑角以 `(r, s)` 为参数（r 为半径，s ∈ [0,1] 为平滑度），轮廓为对称三段式：

```
直边 ── 入过渡曲线 ── 保留圆弧 ── 出过渡曲线 ── 直边
κ = 0   起点 κ = 0    κ = 1/r     终点 κ = 0    κ = 0
```

90° 转向角按 s 在圆弧与过渡曲线之间重新分配：

```
每段过渡曲线转角:  (π/4)·s
保留圆弧扫角:      (π/2)·(1−s)
校验: 2·(π/4)·s + (π/2)·(1−s) = π/2
```

角在每条边上的曲线连接点偏移为 `(1+s)·r`：轮廓偏离直边的起点距顶点 `(1+s)·r`，而非普通圆角的 `r`。两个退化端点：s=0 时过渡曲线消失，退化为普通圆弧圆角；s=1 时圆弧扫角为零，整角由两条过渡曲线直接对接（squircle 形态）。

**过渡曲线构造。** 在角局部坐标系（顶点为原点，x 轴沿入边指向矩形内部、y 轴为内法向）中，第一段过渡曲线是三次贝塞尔，其控制点由四个长度量决定（均随半径 r 线性缩放，系数只依赖 s）：

- `p`：曲线连接点偏移，过渡起点到顶点的边向距离，即轮廓离开直边的位置；
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

当 `r > 0、s > 0` 时，前三个控制点共线于直边且起点导数非零，保证起点曲率为 0（与直边 G2 对接）；`s = 0` 时过渡曲线消失，不使用该端点曲率公式。四个长度量随 s 的闭式定义（均为 r 的线性比例，系数只依赖 s）：

```
p/r = 1 + s
e/r = tan(π·s/8)·cos(π·s/4)
g/r = (e/r)·tan(π·s/4)
w/r = [ (1+s) − √2·sin(π·(1−s)/4) − (1 + tan(π·s/4))·(e/r) ] / 3
```

四条公式共同确定过渡曲线的位置、末端切线方向和控制点间距，但没有额外施加末端曲率等于 `1/r` 的条件。设过渡曲线转角 `θ = πs/4`：`p/r = 1+s` 是 footprint 的定义；`g/r = 1 − cosθ` 是圆弧起点在圆上的法向高度（圆心 (r,r)、保留圆弧关于角平分线对称，`(e/r)·tan(πs/4)` 是同一值的另一种写法）；`e/r = tan(θ/2)·cosθ` 由末端切向条件唯一确定——末端切线方向为 `b2→b3 = (−e, g)`，要求它与圆弧在 arcStart 处相切（相对直边转过 θ），即 `g/e = tanθ`，与 `g = r(1−cosθ)` 联立即得；`w/r` 由 footprint 预算闭合：arcStart 的边向坐标为 `r(1−sinθ)`，而 `p − 3w − e` 必须等于它（公式中的 `√2·sin(π(1−s)/4)` 即 `cosθ − sinθ` 的另一种写法）。

代入三次贝塞尔的定义 `B(t) = (1−t)³b0 + 3(1−t)²t·b1 + 3(1−t)t²·b2 + t³b3`（t ∈ [0,1]），参数方程塌缩为：

```
X(t) = (1−t)³p + 3(1−t)²t·(p−2w) + 3(1−t)t²·(p−3w) + t³·(p−3w−e)
     = p·[(1−t)³ + 3(1−t)²t + 3(1−t)t² + t³]      （Bernstein 基函数之和 = 1）
       − w·[6(1−t)²t + 9(1−t)t² + 3t³]
       − e·t³
     = p − 6w·t + 3w·t² − e·t³                     （w 的 t³ 系数 −6+9−3 = 0）
Y(t) = g·t³                                         （只有 b3 的 y 分量非零）
```

**隐式形式。** 定义两个组合坐标（它们就是曲线的参数多项式本身，把 X(t) 代入即得）：

```
ν = y/g                    = t³
β = (x − p + (e/g)·y)/(3w) = t² − 2t
```

隐式方程即 t³ 与 t²−2t 之间的代数关系——展开 `β³ = t³(t−2)³ = t⁶ − 6t⁵ + 12t⁴ − 8t³`，与 `ν² = t⁶`、`6βν = 6t⁵ − 12t⁴`、`8ν = 8t³` 恰好逐项相消：

```
f(x, y) = ν² − 6βν − 8ν − β³ = 0        内外符号的适用范围在本段中限定
```

隐式化需要区分有限曲段与实参数延拓。这里要求 r>0、s>0，因而 w>0、g>0；s=0 时过渡曲线消失，不能使用含 1/w、1/g 的式子。对任意实数 ν，定义唯一实立方根 τ=∛ν，以及对应的参数横坐标组合 β0=τ²−2τ。直接因式分解：

```
f = −(β−β0)[(β+β0/2)² + (3/4)τ²(τ+2)²]       （代入 ν=τ³、β0=τ²−2τ 后展开核对）
```

在实际曲段的纵坐标范围 0≤y≤g 内，0≤τ≤1；τ>0 时方括号严格为正，τ=0 时 f=−β³。因此 f=0 在该范围内恰好对应 t=τ 的曲线点；固定 y 时，β 随 x 递增，f<0 对应曲线右侧，也就是此局部坐标中的形状内部。是否可在纵坐标范围之外用于有限曲段的内外判定，还必须结合具体选段区域证明。

不能由 Y(t)=gt³ 对实参数可逆，推断消元方程在整个平面没有额外实零点。例如 ν=−8 时：

```
f = 128+48β−β³ = −(β−8)(β+4)²
```

唯一实参数 τ=−2 给出 β0=8，但 β=−4 也使 f=0。这是位于 (x,y)=(p+8e−12w,−8g) 的孤立实零点，不属于原曲段，也不是会造成符号翻转的额外实曲线分支。有限坐标域的数值检查不能代替这个全域区分。

组合变量 ν、β 使系数保持简单，但含有 1/g、1/w；当 s 趋零时 g/r 与 s² 同阶、w/r 与 s 同阶，不能仅凭隐式多项式的常数系数断言浮点计算稳定。退化分支与小平滑度的数值行为需要另外验证。

出过渡曲线与入过渡曲线关于角平分线对称；保留圆弧以角平分线为中轴，定义其扫角 `φ=(π/2)(1−s)`，其三次贝塞尔逼近的控制柄长度为 `(4/3)·tan(φ/4)·r`。该三次曲线只近似圆弧，不与精确圆弧逐点重合；按半径及设备变换缩放后，非零几何差异也会同比缩放。数值例：100×100 矩形、r=20、s=1 的右上角（顶点 (100, 0)），`w = 20√2/3`、`e = g = 10(2−√2)`，第一段过渡曲线的控制点为 `(60, 0)`、`(60+40√2/3, 0)`、`(60+20√2, 0)`、`(80+10√2, 20−10√2)`，约为 `(60, 0)`、`(78.86, 0)`、`(88.28, 0)`、`(94.14, 5.86)`；此时 `arcStart == arcEnd`，两段过渡曲线在角平分线上的该点对接。

**椭圆角。** 对 `rx > 0、ry > 0`，先在半径 `rx` 的圆形框架中构建平滑角，再做缩放 `(x, y) → (x, y·ry/rx)`。正的逐轴缩放保持所在象限、曲线连接关系和切线方向的连续性；圆形参数角保持为参数，但不再等于变换后的切线转角。非等比缩放会改变曲率值，却不会破坏原本成立的 G2 连接：若缩放矩阵为 A、变换前单位切线为 T、曲率为 κ，则变换后的曲率为 `det(A)·κ/|AT|³`，连接两侧的 T 相同就会乘以同一个因子。因此直边连接处仍为零曲率，圆弧连接处原有的曲率差也仍然存在。非等比缩放不保持欧氏距离、垂直关系或最近点归属，不能直接把圆形框架的法线分区当作椭圆空间的最近曲线分区。

**可实现性与投影。** 一条边长为 L 的边承载两个角的 footprint 区间，曲线族可实现的充要条件是它们不相交：

```
(1+s)·(r_a + r_b) ≤ L    （对每条边）
```

参数落在可行集之外时，先降低平滑度，半径保持不变。输入 s 已限制到 [0,1]；用 j 遍历四条边，L_j 为该边长度，r_a、r_b 为两端半径沿该边的分量。只对 r_a+r_b>0 的边计算比值，和为零的边不施加约束；若全部为零，则无需平滑角并令 s'=0。其余情况：

```
s' = max(0, min(s, min_j [L_j/(r_a+r_b) − 1]))
```

曲线连接点偏移关于 (1+s) 线性，降低 s 会缩短沿边的占用距离，但不是将整条角曲线等比缩小。当有效平滑度 s'>0 且边约束取等号时，两段过渡曲线的直边端点相遇，直线段长度为零，两侧曲率均为 0。s'=0 时过渡曲线消失，不能再声称该处为零曲率对接；若裸半径之和仍超过边长，继续使用普通圆角的半径缩放规则。

#### 3.1.2 牛顿迭代

**问题归约。** 点 Q 到三次贝塞尔曲线 B(t) 的距离定义为参数域上点对点距离的最小值：

```
dist(Q, B) = min |B(t) − Q|,  t ∈ [0,1]
```

最小值要么在区间端点取得，要么在内部驻点取得。平方根单调，最小化距离等价于最小化平方距离 `D(t) = |B(t) − Q|²`（避免开方），其驻点条件为：

```
D′(t) = 2(B(t) − Q)·B′(t) = 0        （链式法则）
```

几何含义：参数区间内部的最近点处，曲线切线与该点到 Q 的连线垂直；区间端点不要求满足这一条件。B(t) 是 t 的三次多项式、B′(t) 是二次，驻点方程最高为五次。一般五次方程没有通用根式解，数值求解时必须考虑区间内的候选根及两个端点，不能仅凭一个驻点确定全局最小值。

**迭代公式。** 对 D′(t) 应用牛顿法（t ← t − D′/D″）：

```
D″(t) = 2(|B′(t)|² + (B(t) − Q)·B″(t))                 （对 D′ 再求导，乘积法则）
t ← t − (B(t)−Q)·B′(t) / (|B′(t)|² + (B(t)−Q)·B″(t))   （代入牛顿步，约去因子 2）
```

其 Gauss-Newton 变体丢掉分母中的二阶项 (B−Q)·B″：

```
t ← t − (B(t) − Q)·B′(t) / |B′(t)|²                    （Gauss-Newton 步）
```

被丢项是否足够小取决于 `|(B−Q)·B″| / |B′|²`，不能仅由 `|B″|` 判断曲率大小，因为 B″ 还包含参数速度变化造成的切向分量。

**多驻点。** 五次驻点方程可有多个实根，可能对应局部最小值、局部最大值或高阶驻点。多个初值分别迭代、钳制到 [0,1] 后比较，可以降低遗漏最近点的风险，但固定数量的初值和迭代次数本身不构成全局正确性保证；两个端点也要作为距离候选。

**固定次数迭代与端点候选。** 定义 n 为每个初值的迭代次数，三个初值分别为 t0=0、t1=1/2、t2=1。每个初值执行 n 次 Gauss-Newton，迭代过程中不钳制参数，只在结束后钳制到 [0,1]。记最终有效参数为 t0*、t1*、t2*，有限候选集合为：

```
Tn = {0, 1, t0*, t1*, t2*}                  （两个端点独立保留，不因初值离开端点而丢失）
d̂n(Q) = min{ |B(t)−Q| : t∈Tn }              （仅使用有限、有效的参数与距离）
d̂n(Q) ≥ min{ |B(t)−Q| : 0≤t≤1 }            （有限候选是整个参数区间的子集，精确算术下）
```

先用两个端点的距离初始化最小值，非有限的迭代结果不更新它。这既覆盖端点最近的情况，也防止极小平滑度下部分初值数值失稳时丢失所有候选；仍不能保证内部最近点已经被找到。相同初值集合下，n 从 2 增至 3 只把迭代步数从 6 增至 9，不构成误差必然单调减小的定理。必须对相同轮廓、相同查询集合和同一参考解比较，不能把局部收缩因子直接当成第三次迭代的全局误差改善比例。

**收敛域。** 不动点迭代是把方程改写为 `t = G(t)` 的形式，使 G 的不动点（满足 G(t*) = t* 的点，即代入后不变的点）恰为目标方程的解，再从初值出发按 `t_{n+1} = G(t_n)` 反复代入逼近。Gauss-Newton 步正是这个形式：G(t) = t − D′(t)/(2|B′(t)|²)，且 G(t*) = t* 等价于驻点条件 D′(t*) = 0。记第 n 步的误差 e_n = t_n − t*，其逐步演化由 G 的导数决定：

```
e_{n+1} = t_{n+1} − t*                      （误差的定义）
        = G(t_n) − G(t*)                    （代入递推式 t_{n+1} = G(t_n) 与不动点定义 t* = G(t*)）
        ≈ G′(t*)·(t_n − t*)                 （在 t* 处一阶展开，丢掉 O(e_n²)；前提：t_n 已接近 t*）
        = G′(t*)·e_n                        （代入误差的定义）
```

逐步回代得 `e_n ≈ e_0·(G′(t*))^n`，即误差按 |G′(t*)| 等比变化：小于 1 时收敛、大于 1 时失稳。该结论是一阶展开下的渐近结论，在迭代进入真解附近后成立。收缩因子的具体形式由 G′(t*) 给出：

```
G′(t*) = 1 − D″(t*) / (2|B′(t*)|²)          （对 G 求导，含 D′(t*) 的项因 D′(t*) = 0 消失）
       = −(B(t*) − Q)·B″(t*) / |B′(t*)|²    （代入上文 D″ 的表达式）
```

在正则曲线的内部驻点 t* 处，定义单位切线 T、指向曲率侧的单位法线 N、参数速度函数 V(t)=|B′(t)|，并在该驻点使用 V=V(t*)>0，以及有符号法向距离 δ，使 `Q − B(t*) = δN`。曲率 κ 非负；δ > 0 表示 Q 位于曲率侧，到该驻点的距离 d=|δ|；只有确认 t* 是全局最近点时，d 才等于到曲线的最短距离。由切线与法线方向的导数分解：

```
B″(t*) = V′T + V²κN                         （V′ 为参数速度对 t 的导数）
(B(t*) − Q)·B″(t*)
        = (−δN)·(V′T + V²κN)                 （代入驻点处的法向关系）
        = −δV²κ                             （N·T = 0，N·N = 1）
G′(t*) = δκ                                 （代入 G′ 的表达式）
|G′(t*)| = dκ = d/ρ                         （κ > 0 时定义局部曲率半径 ρ = 1/κ）
D″(t*) = 2V²(1 − δκ)                       （代入 D″ 的表达式）
```

这些是驻点处的恒等式，不需要密切圆近似。κ = 0 时直接使用 `|G′(t*)| = 0`，不必计算 ρ。`dκ < 1` 保证 Gauss-Newton 在该驻点附近局部收敛，`dκ > 1` 表示该不动点不稳定；这不是任意初值都能收敛的保证，也不能保证固定迭代次数的像素误差。κ > 0 且 δ > ρ 时 D″ < 0，该驻点反而是局部最大值；δ < −ρ 时仍可能是最近点，但 Gauss-Newton 的局部迭代不稳定。

`d = ρ` 时必须区分方向：δ = ρ 才有 D″ = 0，此时二阶判定失效，普通牛顿步的分母为零；δ = −ρ 时 D″ = 4V² > 0，而 G′ = −1，需要更高阶分析。单凭 D″ = 0 不能断言最近点不唯一。端点最小值、跨连接点的迭代以及钳制后的行为也不属于该内部驻点分析。

连接点的曲率可由 3.1.1 的控制点直接计算。这里 w、e、g 保持其长度含义，θ = πs/4，且 r > 0、s > 0：

```
B′(1) = 3(b3 − b2)                                （对 Bernstein 形式求导代入 t = 1）
B″(1) = 6(b1 − 2b2 + b3)                          （再求导代入 t = 1）
κ(1) = |B′(1)×B″(1)| / |B′(1)|³
     = (2/3)·|(b3−b2)×(b1−2b2+b3)| / |b3−b2|³     （代入上面两式，系数 18/27 = 2/3）
     = (2/3)·g·w / (e·secθ)³                      （代入 b3−b2 = (−e, g)、b1−2b2+b3 = (w−e, g)，叉积化简得 g·w；θ = πs/4 为过渡曲线转角，见 3.1.1）
     = 2·w·sinθ·cos²θ / (3·e²)                    （代入 g = e·tanθ）
```

该式只给出过渡曲线的端点曲率，不能替代整段的最大曲率。以 `r = 1、s = 0.5` 代入控制点公式，得到 `κ(1) ≈ 1.5021949574`，而保留圆弧曲率为 1，证明该连接一般只有 G1 连续。最小曲率半径必须从整个参数区间取得，不能把某个平滑度下的端点结果作为所有平滑度的通用值。

**距离误差与参数误差。** 迭代给出的是参数 t 的近似值，最终关心的是距离误差。设 t* 是区间内部的全局最近点参数（D′(t*) = 0），参数误差为 Δt。在 t* 处展开：

```
D(t* + Δt) = D(t*) + D′(t*)·Δt + ½·D″(t*)·Δt² + O(Δt³)   （泰勒展开）
           = D(t*) + ½·D″(t*)·Δt² + O(Δt³)               （驻点处 D′(t*) = 0）
```

记真距离 d* = √D(t*)，并把距离平方的完整变化记为 E，保留三阶及以上余项：

```
E = D(t* + Δt) − D(t*)
  = ½D″(t*)Δt² + O(Δt³)                          （代入驻点处的展开）
Δd = √(d*² + E) − d*
   = E / (√(d*² + E) + d*)                       （分子有理化，恒等变形）
   = E/(2d*) + O(E²)                            （要求 d* > 0，且 E 相对 d*² 足够小）
   = D″(t*)Δt²/(4d*) + O(Δt³)                   （代入 E；O(E²) = O(Δt⁴) 被余项吸收）
```

因此，固定 d* > 0 的内部最近点处，距离误差的首个可能非零项为 Δt²。若 d* = 0，平方根不能这样展开：此时 `B(t* + Δt) − B(t*) = B′(t*)Δt + O(Δt²)`，所以 `Δd = |B′(t*)|·|Δt| + O(Δt²)`，一般只有一阶精度。端点最小值也不能直接使用 D′(t*) = 0 的推导。

在 d* > 0 的适用范围内，D″ 含参数速度平方项，几何尺寸按 r 缩放时该项按 r² 缩放；固定相对距离 d*/r 时，Gauss-Newton 的迭代与参数误差不随 r 改变，因此绝对距离误差按 r 缩放。距离接近局部曲率半径时，局部收缩因子接近 1，固定迭代次数可能不足。像素容差必须同时约束尺寸、查询距离和求解精度，不能仅由二阶误差或局部收敛断言。

#### 3.1.3 最近曲线选择

##### 定义与法线方程

本节研究不预先计算三段最短距离的选段方法。输入为有效半径 rx、ry、平滑度 s 和查询点 Q = (x,y)，采用 3.1.1 的控制点公式；首先以单角的两段过渡曲线与精确保留弧定义 C，再把中间段替换为 CPU 使用的三次贝塞尔，定义另一条轮廓 Cb；两条轮廓的距离不能混用。本节不包含相邻直边或其他角的最近点竞争。基本条件为 `rx > 0、ry > 0、0 < s < 1`。坐标原点位于矩形左上角顶点，x 向右、y 向下；其他角可以通过刚体变换归到这个坐标系。

依次记入过渡曲线、保留弧、出过渡曲线为 C1、C2、C3，整体曲线为 C=C1∪C2∪C3，两个唯一公共连接点分别为 PS、PE，即 C1∩C2={PS}、C2∩C3={PE}。保留弧的三次近似记为 C2b，CPU 轮廓为 Cb=C1∪C2b∪C3，到它的最短距离记为 db(Q)。C2b 保留相同端点和端点切线，因此两条轮廓使用同一对连接点法线。定义 `di(Q) = min{ |Q−P| : P∈Ci }`，整体最短距离为 d(Q) = min(d1,d2,d3)。这些距离只用于陈述与证明，不作为判定函数的输入。

延用过渡转角 θ = πs/4，定义 u = sinθ、v = cosθ。为避免与保留弧扫角 φ 混淆，用 ψ 表示圆形构造中的弧参数：

```
X(ψ) = (rx(1−sinψ), ry(1−cosψ))
θ ≤ ψ ≤ π/2−θ

PS = (rx(1−u), ry(1−v))                 （ψ = θ）
PE = (rx(1−v), ry(1−u))                 （ψ = π/2−θ）
```

该式与以椭圆中心为原点的标准参数式仅相差平移和角度替换：中心为 (rx,ry)，标准参数角 χ=3π/2−ψ，所以 cosχ=−sinψ、sinχ=−cosψ。PS 也等于 3.1.1 的 b3 经逐轴缩放后的坐标；PE 由正圆阶段的镜像点缩放得到，不是把椭圆 PS 的坐标直接交换。

对弧参数求导得到未归一化切线 TS、TE。通过连接点的法线上，任意位移都与该切线垂直：

```
X′(ψ) = (−rx cosψ, ry sinψ)              （逐坐标求导）
TS = (−rx v, ry u)
TE = (−rx u, ry v)

fs(x,y) = −(Q−PS)·TS
        = rx v(x−PS.x) − ry u(y−PS.y)
fe(x,y) = −(Q−PE)·TE
        = rx u(x−PE.x) − ry v(y−PE.y)
```

`fs = 0`、`fe = 0` 分别是完整法线 NS、NE 的方程，零值集合分别经过 PS、PE。函数值不是距离：例如到 NS 的距离为 `|fs|/√(rx²v²+ry²u²)`。朝椭圆内部的法线方向可取 `(u/rx,v/ry)`、`(v/rx,u/ry)`；是否朝内不改变完整直线。不能把正圆法线直接非等比缩放，因为变换后它通常不再垂直于椭圆切线。

两条完整法线把平面分为四个开放区域，边界不计入区域内部：

| 区域 | fs | fe | 曲线位置 |
|---|---|---|---|
| S1 | > 0 | > 0 | C1 除 PS 外所在区域 |
| S2 | < 0 | > 0 | C2 除两个端点外所在区域 |
| S3 | < 0 | < 0 | C3 除 PE 外所在区域 |
| S4 | > 0 | < 0 | 与 S2 相对，不含任何曲线段 |

这里的“曲线位置”只描述曲线本身落在哪个区域，不表示区域内任意点都以该段为最近曲线。切线总转角不超过 90°，因此沿曲线在某连接点之前的位移对该处切线的投影为负、之后为正；这给出表内曲线的侧别。直接把 C2 上 ψ = π/4 的点代入，也能核验 S2 的符号组合。

##### 全平面分区的限制

法线分区不能直接作为全平面的最近曲线分区；仅把查询坐标限制在 `[0,rx(1+s)] × [0,ry(1+s)]` 也不足以保证正确。

首先，正圆的圆心附近就可能出现 S2 内过渡曲线更近的情况。取 r = 1、s = 1/2，定义半角量 z = tan(π/16)。由 3.1.1 的系数与 3.1.2 的端点曲率公式：

```
e = z cosθ，g = z sinθ，w = (1/2+z)/3
κ(1) = (4z+2)/[9z(1+z²)]
κ(1)−1 = (1−3z)(3z²+z+2)/[9z(1+z²)] > 0
```

最后一步使用 `0 < z < tan(π/12) = 2−√3 < 1/3`。记圆心 O = (1,1)，C1 的参数曲线为 B(t)，到圆心的距离平方为 F(t) = |B(t)−O|²。这里 N 为 PS 处指向圆心的单位法线，B(1)−O=−N，B′(1) 沿切线，因此：

```
F(1) = 1
F′(1) = 2(B(1)−O)·B′(1) = 0
F″(1) = 2|B′(1)|²(1−κ(1)) < 0          （使用 3.1.2 的导数分解，法向距离为 1）
F(1−η) = 1 + ½F″(1)η² + O(η³) < 1     （η 为足够小的正参数增量）
```

固定这样的 η，记 R0 = |B(1−η)−O| < 1，选 ε = (1−R0)/4，并令 Q = O−ε(1,1)/√2。该点严格位于 S2，坐标也位于 [0,1.5]²；它对 C2 的径向最近点对应 ψ = π/4：

```
d2(Q) = 1−ε
d1(Q) ≤ |Q−B(1−η)|
      ≤ R0+ε                            （三角不等式）
      < 1−ε = d2(Q)                    （2ε = (1−R0)/2 < 1−R0）
```

这证明了受限坐标框中的正圆也不能全域按 S2 选择 C2。严格的法线侧别与距离差在小幅改变半径比例后仍保持，因而该限制不能因 rx、ry 不相等而消失。

椭圆还会在 S1 内选择到相反一侧的过渡曲线。取 rx = 2、ry = 1、s = 1/2、Q = (8/5,9/5)，此时：

```
fs(Q) = fe(Q) = 3uv − (4/5)(u+v) > 0
```

正号可不依赖小数验证：`uv = √2/4`、`(u+v)² = 1+√2/2`，两项均为正，平方后只需比较 `9/8 > 16/25+8√2/25`，等价于 `97 > 64√2`，再平方即 `9409 > 8192`。因此 Q 严格位于 S1。C1 的全部控制点纵坐标位于 [0,1−v]，而 C3 包含端点 D = (0,3/2)：

```
d1(Q) ≥ 9/5−(1−v) = 4/5+v > 17/10
                                             （贝塞尔凸包性质，且 v > 9/10）
d3(Q)² ≤ |Q−D|² = (8/5)²+(3/10)² = 53/20
```

C2 上 sinψ+cosψ ≥ u+v，故整段位于半平面 `x+2y ≤ 4−2(u+v)`。Q 在另一侧，因此：

```
d2(Q) ≥ [6/5+2(u+v)]/√5 > (19/5)/√5
                                             （点到该半平面的距离，且 u+v > 13/10）
d1(Q)² > 289/100 > 53/20 ≥ d3(Q)²
d2(Q)² > 361/125 > 53/20 ≥ d3(Q)²
```

所以该 S1 内部的点实际以 C3 为最近曲线。交换横纵坐标与 rx、ry，同时交换 C1、C3，就得到 S3 内以 C1 为最近曲线的反例。这里 v > 9/10、u+v > 13/10 都可由 `v² = (2+√2)/4`、`(u+v)² = 1+√2/2` 平方比较得到。

正圆仍保留一个全平面成立的简化：C1 与 C3 关于 y=x 镜像，且 C1 的控制点均在 x>y 一侧。对 A=(a,b)∈C1 及镜像点 A′=(b,a)∈C3，有 a>b，展开得：

```
|Q−A|²−|Q−A′|² = −2(x−y)(a−b)
```

因此 x>y 时 d1<d3，x<y 时 d3<d1，x=y 时两者相等。严格不等式可通过选取另一段的最近点及其镜像点证明，不需要计算距离。但它只比较两段过渡曲线，不能代替与 C2 的比较；非等比缩放后也没有这一镜像距离恒等式。以上反例否定指定的法线分区，不是否定所有可能的非线性判定函数。

##### 近边界选择定理

考虑按 C1→C2→C3 定向的整条曲线。用弧长 ℓ∈[0,L] 参数化为 γ(ℓ)，L 为单角曲线总长度；T(ℓ) 为单位切线，N(ℓ) 为朝曲率侧的单位法线。以 α(ℓ) 表示从起点累计的切线转角，转弯方向取正。假设曲线正则、切线连续、分段二阶连续可导，并满足：

```
0 ≤ κ(ℓ) ≤ K，K > 0                  （连接处取两侧的曲率上限）
α′(ℓ) = κ(ℓ)                          （每个光滑段内）
0 ≤ α(ℓ2)−α(ℓ1) ≤ π/2，ℓ1 ≤ ℓ2       （只朝一侧转弯，总转角不超过 90°）
R = 1/K                               （整段统一采用的安全距离）
```

**定理：若 d(Q)<R，fs、fe 的侧别可以精确确定 Q 的最近点属于哪段曲线；不需要预先求出 d1、d2、d3。** 使用统一安全距离 R 区别于 3.1.2 的局部曲率半径 ρ；d(Q)<R 是充分条件，不要求 K 恰好等于真实最大曲率。曲率在连接处可以跳变，只需切线连续、两侧曲率都受 K 约束。

**弧段位移的投影。** 取曲线上先后两点 A=γ(ℓA)、P=γ(ℓP)，ℓA<ℓP，统一记两处切线的非负转角差为 Δα=|α(ℓP)−α(ℓA)|。定义 J 为位移 P−A 在 A 处切线上的投影：

```
J = (P−A)·T(ℓA)
  = ∫[ℓA,ℓP] cos(α(ℓ)−α(ℓA)) dℓ             （积分单位切线得到弧段位移）
  ≥ R ∫[ℓA,ℓP] κ(ℓ)cos(α(ℓ)−α(ℓA)) dℓ     （Rκ ≤ 1，且转角不超过 π/2，余弦非负）
  = R sinΔα                                   （α′ = κ，逐光滑段积分并在连接处相加）
```

若 A 在 P 之后，则改取 `J = (A−P)·T(ℓA)`，积分中的余弦为 `cos(α(ℓA)−α(ℓ))`，同样得到 J ≥ RsinΔα。若 Δα=0，则这段曲线是直线，J=|ℓP−ℓA|>0。

**最近点位于整体曲线内部。** 曲线紧致，最近点 P 一定存在。若 P 不是整条曲线的两个外端点，即使它是 PS 或 PE，也有唯一连续切线，距离平方的一阶最优条件给出：

```
Q = P + δN(P)
|δ| = d(Q) < R                        （δ 为朝曲率侧取正的有符号法向距离）
```

当 A 位于 P 之前时，`N(P)·T(A) = −sinΔα`；当 A 位于 P 之后时，该点积为 sinΔα。因此：

```
A 在 P 之前：
(Q−A)·T(A) = J−δsinΔα
            ≥ (R−δ)sinΔα > 0          （Δα > 0；R−δ ≥ R−|δ| > 0）

A 在 P 之后：
(Q−A)·T(A) = −J+δsinΔα
            ≤ −(R−δ)sinΔα < 0
```

Δα=0 时法向项为零，J>0，仍有相同的严格符号；A=P 时点积为零。所以查询点位于 A 的法线哪一侧，恰好对应其最近点在 A 之前还是之后。将 A 分别取为 PS、PE，即可判断曲线段。

**最近点位于整体曲线端点。** 若 P=γ(0)，端点的一阶最优条件是 `(Q−P)·T(0) ≤ 0`。把位移分解为 `Q−P = aT(0)+δN(0)`，其中 a≤0、a²+δ²=d(Q)²<R²。对任何后续点 A：

```
(Q−A)·T(A) = −J+a cosΔα+δsinΔα
            ≤ −J+δsinΔα < 0          （a ≤ 0、cosΔα ≥ 0，零转角时由 J > 0 保证）
```

因此两个函数均为正，正确归入 C1。若 P=γ(L)，同理有 a≥0，对任何前面的 A：

```
(Q−A)·T(A) = J+a cosΔα−δsinΔα > 0
```

两个函数均为负，正确归入 C3。这覆盖两个外端点附近的点，不需要额外排除端点区域。还可以对任意 A 使用这些符号：`d|Q−γ(ℓ)|²/dℓ = −2(Q−γ(ℓ))·T(ℓ)`，距离平方在 P 之前严格减小、之后严格增大，所以最近点唯一；若 P 为公共连接点，它同时属于相邻两段。

##### 给定曲线的曲率上限

以 3.1.1 的圆形控制点为输入，先定义单位半径系数 w0=w/r、e0=e/r、g0=g/r，以及半角量 z=tan(θ/2)。利用 sinθ=(1+cosθ)tan(θ/2)，可把系数化简为：

```
e0 = z cosθ
g0 = z sinθ
3w0 = 1+s−(cosθ−sinθ)−(1+tanθ)e0
    = s+sinθ−e0                       （g0 = 1−cosθ）
    = s+z                             （sinθ−z cosθ = z）
```

记单位半径的过渡曲线为 B0(t)，定义 H(t)=2w0(1−t)+e0t²，用 3.1.1 的参数式求导：

```
B0′(t)  = 3(−H(t), g0t²)
B0″(t)  = 6(w0−e0t, g0t)
B0′(t)×B0″(t) = −18w0g0t(2−t)        （逐项展开二维叉积）
κ0(t) = |B0′×B0″|/|B0′|³
      = 2w0g0t(2−t) / {3[H(t)²+g0²t⁴]^(3/2)}
```

对 s>0，w0、e0、g0 均为正，H(t)>0，因而导数不为零；曲率非负，转弯方向固定，端点切线从水平转过 θ。C3 是镜像反向的同构曲线，C2 再承担 π/2−2θ 的转角，所以整体满足近边界选择定理的几何前提。

不能把 κ0(1) 当作整段最大值。例如在 s=1 时，z=√2−1、w0=√2/3，直接对曲率式求导：

```
κ0′(1) = 6κ0(1)(w0 cosθ/z−1)
        = 2κ0(1)(√2−2) < 0
```

因此端点之前存在更高曲率；由连续性，s 接近 1 但小于 1 时也有同样情况。正确的上限必须覆盖所有 t，而不是只计算 PS。

下面给出对所有 `0<s<1、0≤t≤1` 成立的解析上限。tan 在 [0,π/4] 上为凸函数，图像不超过连接两端的弦，同时 tan x≥x。因此：

```
θ/2 ≤ z ≤ (4/π)(θ/2) = s/2
w0 = (s+z)/3 ≥ z
w0/z = (1+s/z)/3 ≤ (1+8/π)/3          （θ/2 = πs/8）
```

速度除去因子 3 后的长度为 `√(H²+g0²t⁴)`。定义向量 U=(H,g0t²)，以及单位向量 n=(cos(θ/2),sin(θ/2))。U 是沿水平与转角 θ 两个方向的非负向量之和；投影到它们的角平分方向：

```
U = 2w0(1−t)(1,0) + zt²(cosθ,sinθ)
|U| ≥ n·U
    = cos(θ/2)[2w0(1−t)+zt²]          （两方向与 n 的夹角均为 θ/2）
    ≥ z cos(θ/2)[2(1−t)+t²]          （w0 ≥ z）
    = z cos(θ/2)[1+(1−t)²]
    ≥ z cos(θ/2)
```

代回曲率，并使用 t(2−t)≤1：

```
κ0(t) ≤ 2w0g0/[3z³cos³(θ/2)]
      = 4w0/[3z cos(θ/2)]             （g0=z sinθ，sinθ/z=2cos²(θ/2)）
      ≤ 4(1+8/π)/[9cos(π/8)]
      < 88/(27√3) < 2                （π > 3、cos(π/8) > √3/2；88² < (54√3)²）
```

单位圆弧的曲率为 1，也满足同一上限；C3 与 C1 曲率相同。故半径 r 的整条正圆角可以统一取：

```
K = 2/r
R = 1/K = r/2
```

对椭圆角，定义 m=min(rx,ry)、M=max(rx,ry)，从单位正圆角施加 A=diag(rx,ry)。设变换前单位切线为 T0，曲率为 κ0，曲率公式给出：

```
κ = |AB0′×AB0″|/|AB0′|³
  = det(A)·κ0/|AT0|³                  （二维叉积乘 det(A)，约去原速度的三次方）
  ≤ mM·κ0/m³                        （|AT0| ≥ m，det(A)=mM）
  < 2M/m²
```

正的逐轴缩放保持转弯方向与水平、竖直端点切线，整体总转角仍为 90°。因此可以统一取：

```
K = 2M/m²
R = 1/K = m²/(2M)
```

这些是为简化使用而选择的保守上限和安全距离，不是真实最大曲率及其倒数。有限次迭代能否达到目标精度仍是 3.1.2 的数值求解问题。

##### CPU 保留弧三次近似的曲率上限

以扫角 φ∈(0,π/2] 的单位圆弧为参考，在圆心位于原点、圆弧关于水平轴对称的坐标系中计算；刚体变换不改变曲率。定义 φh=φ/2 为半扫角，zA=tan(φ/4)、b=zA²，柄长为 hA=4zA/3。端点及沿遍历方向的单位切线分别为：

```
P0 = (cosφh, −sinφh)，P3 = (cosφh, sinφh)
T0 = (sinφh, cosφh)，T1 = (−sinφh, cosφh)
P1 = P0+hA·T0，P2 = P3−hA·T1
0 < b ≤ (√2−1)² = 3−2√2 < 2/3
```

这就是 CPU 保留弧的标准三次逼近。用 ξ=2t−1∈[−1,1] 代替贝塞尔参数 t，把半角恒等式 cosφh=(1−b)/(1+b)、sinφh=2zA/(1+b) 代入控制点并展开，得到曲线 BA(ξ)：

```
xA(ξ) = 1−2bξ²/(1+b)
yA(ξ) = zA[(2+b)ξ−bξ³]/(1+b)
xA′ = −4bξ/(1+b)，yA′ = zA(2+b−3bξ²)/(1+b)
xA″ = −4b/(1+b)，yA″ = −6zA bξ/(1+b)           （本段撇号均对 ξ 求导）
```

定义 VA=|BA′| 为参数速度，JA=xA′yA″−yA′xA″ 为曲率分子。逐项展开：

```
VA² = zA²[(2+b)²+(4b−6b²)ξ²+9b²ξ⁴]/(1+b)²
    ≥ 4zA²/(1+b)² > 0                          （b<2/3，各非恒定项系数非负）
JA = 4zA³(2+b+3bξ²)/(1+b)² > 0
   ≤ 8zA³(1+2b)/(1+b)²                         （ξ²≤1）
κA = JA/VA³
   ≤ (1+b)(1+2b)                               （代入分子上限与速度下限）
   ≤ (4−2√2)(7−4√2) = 44−30√2 < 2             （(1+b)(1+2b) 随 b≥0 递增）
```

VA>0 保证曲线正则，JA>0 保证只朝一侧转弯；yA′≥2zA(1−b)/(1+b)>0，排除了切线额外旋转一周的可能。回到原贝塞尔参数 t，两端导数分别为 3hA·T0、3hA·T1，转角正好为 φ，并与过渡段保持 G1 连接。于是 C2b 与两段过渡曲线都满足单位半径曲率小于 2 的统一条件。缩放到圆形半径 r，或施加 diag(rx,ry) 后，沿用本节的曲率缩放推导可得：

```
正圆 CPU 轮廓 Cb：K=2/r，R=r/2
椭圆 CPU 轮廓 Cb：K=2M/m²，R=m²/(2M)
```

因此近边界选择定理同样适用于 db(Q)<R，只需把判定中的 C2 换成 C2b；不能把精确圆弧的距离公式当作到 C2b 的精确距离。φ=0 时该段消失，应直接处理相接的两段过渡曲线，而不使用含 1/zA 的推导。

##### 判定规则与适用范围

对 C，在 d(Q)<R 的前提下使用本节定义的真实法线函数；对 CPU 轮廓 Cb，将前提改为 db(Q)<R，表中的 C2 相应替换为 C2b：

| 条件 | 最近点位置 | 距离求值对象 |
|---|---|---|
| fs>0、fe>0 | C1，包含整条曲线起点，不含 PS | C1 |
| fs<0、fe>0 | C2 内部 | C2 |
| fs<0、fe<0 | C3，包含整条曲线终点，不含 PE | C3 |
| fs=0 | PS，且 fe>0 | PS 或任一相邻段 |
| fe=0 | PE，且 fs<0 | PE 或任一相邻段 |
| fs>0、fe<0 | 不可能出现在保证范围内 | 不能按近边界规则处理 |
| fs=0、fe=0 | 不可能出现在保证范围内 | 不能按近边界规则处理 |

选段只需 fs、fe 的线性表达式，不需要先求三段距离。距离前提必须由调用方的几何范围保证，不能先求出 d 再声称省去了距离计算；函数符号本身不能证明输入满足这一前提。若已知 `d2(Q) < R`，因 C2⊂C、d(Q)≤d2(Q)，同样满足条件，但它比限制整体距离更严格。

| 半径类型与坐标范围 | 足以保证精确选段的距离条件 |
|---|---|
| 椭圆，不另外限制坐标 | d(Q) < m²/(2M) |
| 椭圆，0≤x≤rx(1+s)、0≤y≤ry(1+s) | d(Q) < m²/(2M) |
| 正圆，不另外限制坐标 | d(Q) < r/2 |
| 正圆，0≤x,y≤r(1+s) | d(Q) < r/2 |

“不另外限制坐标”仍然要求点位于曲线附近，不表示全平面都满足条件。距离条件内最近点所属曲线段选择精确；条件之外尚不能由此给出误差上限，限制到曲线连接点偏移构成的矩形也不会自动满足条件。

`s=0` 时 C1、C3 消失，应直接处理普通圆角；`s=1` 时 PS=PE、C2 消失，四区域模型不再适用，可把同一个近边界定理用于两段曲线和唯一连接点，曲率上限的推导仍成立；正圆情况下也可直接用 y=x 比较两段。零半径使缩放不可逆，不属于本节定理的定义域。

##### 像素误差与描边范围

定义选段误差为 `选中曲线段的最短距离 − d(Q)`。它非负；满足近边界条件且使用真实法线时为零，因此自动小于 0.25 像素。但这不包含所选曲线段的数值求距误差、圆弧近似误差或浮点误差。将曲线与查询点同时放大 n 倍，会把非零的选段误差也放大 n 倍，因此没有尺寸或距离限制时，不能从法线分区推出统一的 0.25 像素保证。

距离与曲率必须在同一欧氏坐标空间计算。若以像素为容差单位，半径、距离与缩放都应按最终设备空间计量；本节的推广覆盖正的逐轴缩放与刚体变换，一般剪切或透视不能直接套用总转角不超过90°的前提。不能只把局部空间距离乘一个平均比例。

对中心描边，定义 h 为描边半宽，a 为抗锯齿在描边边缘之外仍需要准确距离的最大宽度，二者均使用与 R 相同的单位。若需要精确求距的片元都满足 `d(Q)≤h+a`，则一个充分条件是：

```
h+a < R
```

对 CPU 轮廓 Cb 使用同一条件时，需要区分近处与远处的片元。在 db(Q)<R 的范围内，真实法线选段正确；在 S4 内，定理的逆否命题给出 db(Q)≥R>h+a，因此该单角对描边覆盖率的贡献为零，无需猜测 C1/C3 的分界线。

对其他区域，即使远处选段不正确，只要求距结果来自所选 CPU 曲线段上的有效候选点，3.1.2 的有限候选性质就给出 d̂(Q)≥db(Q)。因此精确算术下 db(Q)>h+a 的片元不会因距离被低估而显示描边；有限迭代仍可能高估近处距离，使覆盖率减少。这个结论不适用于 Sampson 距离，也不适用于用精确圆距替代 CPU 圆弧三次近似的情况，因为返回值未必是到同一轮廓候选点的距离。

整幅矩形中的直边/角归属、其他角竞争仍需各自保证；浮点运算也可能破坏精确算术下的单侧关系。安全距离只解决选段，不直接给出固定迭代次数的像素误差限制；最终误差必须相对于同一条目标轮廓测量，并核验有限次数迭代、退化分支与设备空间运算。

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

平滑角 SDF 按分段规则选择圆弧或过渡曲线，圆弧使用闭式距离，三次贝塞尔距离由有限次迭代近似：

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

### 5.1 圆形角与椭圆角的平滑度

平滑度的原生定义域是圆形角（3.1 节的曲线族以标量半径为参数）。依据：Figma 的 `ch()` 接受的 `bs` 是标量，`cornerRadii` 是四个标量；Ardot 的 `corner_radius` 与 tgfx 的 `Rectangle::_roundness` 同样是标量。

椭圆平滑角定义为圆形平滑角的各向异性缩放像：先按 `rx` 在圆形框架中构建，再按 `ky = ry/rx` 做 y 向缩放（仅当 ky 不接近 1 时才缩放）。三次贝塞尔在仿射变换下精确保持，隐式方程经逆坐标替换得到同一几何轮廓的方程；这不意味着欧氏距离或最近曲线归属也保持。**GPU 解析路径与 CPU 路径共用这一定义，椭圆平滑角不再一律回退**，但填充的内外判断与距离选段的正确性必须分开验证。

GPU 解析路径的现有椭圆填充方案在圆形框架中分段，AA 的距离求值在设备框架的缩放贝塞尔上进行。逆缩放只还原构造位置，不能保证选择了设备空间的最近曲线；加迭代次数不能修复选段错误。3.1.3 的近边界保证使用椭圆空间的真实法线，与该分派方法不同，不能直接作为其正确性依据。`ky` 的可用范围需要同时验证分派误差和数值求距误差，超出实测阈值时回退 CPU 路径；本节不据近边界定理扩大既定支持范围。

画布矩阵的非等比缩放仍回退 CPU 路径（轴对齐情形未来可把逐轴缩放烘焙进半径，留作后续优化）。裁剪 FP 暂保持圆形角限制，椭圆平滑裁剪走路径回退。

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

由此每个 fragment 至多求值一个角，且内部fragment 经`mix(edgeDist, cornerDist, inAnyCorner)` 完全跳过角求值。椭圆角时选择框按轴向分别加宽（`(1+s)·rx` / `(1+s)·ry`）；圆弧扇区仍按现有方案在圆形框架中判断，片元先按 `1/ky` 映回后做角平分线方向的区域测试。该测试只保持构造分段，不保证设备空间的最近曲线归属；3.1.3 的真实法线分段及距离条件不能直接套用于此测试。

#### 5.5.2 消除逐fragment 三角函数

依5.2 节，7 个系数在 CPU 侧算好后打成两个 `Float4` uniform 传入，shader 内不含任何三角函数。Figma 原版每个 fragment 需要三次 `tan`、一次 `sin`、一次 `cos`、一次 `atan`。

#### 5.5.3 内外符号

AA 与 NonAA 使用不同的符号方案，以编译期变体生成（进 GP key）：

- **AA**：需要距离幅值驱动覆盖斜坡。牛顿迭代得到最近点参数 `t*`，符号用最近点处叉积 `sign(cross(B'(t*), p - B(t*)))`，替代 Figma 的三次方程求根加射线交叉计数。椭圆角的圆弧段使用 Sampson 距离（`F/|grad F|`），与现有椭圆求值同级近似。
- **NonAA**：只需符号，不求距。过渡段使用 3.1 节的隐式形式 `f = ν² − 6βν − 8ν − β³`（约 12 次乘加，无迭代、无除法、无 sqrt，对比牛顿路径约 180 flops）；圆弧段用圆/椭圆隐式方程的符号；边线区用 `edgeDist` 的符号；纯内部直接为 1。正确性依据：隐式曲线无共轭分支（3.1 节命题，`verify_smooth_corner_math.py` 第 7 项），辖区内符号与牛顿参考逐点一致（第 8 项）。

AA 路径的化简注意事项：叉积法在最近点被 clamp 到端点时垂直性不成立，符号可能出错。设计上两个端点分别落在边线段（由线性分支接管）与圆弧接合点（由扇区分支接管），SDF 在接缝处连续故不应暴露。此项标注为推测，必须在实现阶段由单测数值验证；若不成立则退回 Figma 的原始做法。

#### 5.5.4 shader 变体

照 2.4.3 节 `GLSLRectEffect` 的模式，以构造期确定的 bool 分变体。

shader 内的 `if` 分两类，相关函数的注释需按此分类说明。全局判断：整个绘制不变的条件（AA/NonAA、圆/椭圆、是否平滑），一个绘制只取其一，不引起 GPU 计算单元分歧——这类条件一律以编译期变体实现，在 `emitCode` 里生成不同代码，并必须体现在 GP 的 key 里。局部判断：逐 fragment 变化的条件（圆弧扇区还是过渡曲线扇区等辖区分派），会引起 GPU 计算单元的指令同步开销，但这部分片元占整体比例不高，可以接受。`src/gpu/processors/RRectEffect.h`：

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

新增 `SmoothRRectsVertexProvider`，圆形变体每角只需一个标量半径（四角共 4 个 float），比现有 complex 版的 `xRadii(4) + yRadii(4)` 更省；椭圆变体每角传 `rx`、`ry` 两个标量（8 float），以 `isElliptical` 变体位区分并进 GP key。平滑度作为 uniform 而非顶点属性，因此同一个 op 内所有圆角矩形必须共享同一平滑度。

`ComplexMismatch` 之外新增一条打断条件，比较平滑度数值是否相等。代价是不同平滑度的圆角矩形无法合批（§1.2 已列为不在范围内）；实际使用中平滑度是设计系统级常量，同一界面基本统一，Figma 的 `hasUniformCorners` uniform 也反映了同样的假设。后续如需放开，可把系数移入顶点属性。

`RRectDrawOp` 增加 `hasSmoothness` 字段，`indicesPerRRect` 在其为真时取 `IndicesPerNonAARRect`，`onMakeGeometryProcessor` 在现有四路选择之前插入平滑分支。

FP 与 GP 两侧的 GLSL 生成代码抽为`src/gpu/glsl/GLSLSmoothCorner.{h,cpp}` 的自由函数共用，保证裁剪与填充的轮廓同源。

#### 5.5.7 描边解析化

##### 目标

平滑圆角描边通过计算片元到角轮廓的最短距离实现，再由距离与描边宽度换算覆盖率。每个角的轮廓依次为三段曲线：入过渡曲线 C1、保留弧 C2、出过渡曲线 C3；C1、C3 为三次贝塞尔曲线，C2 按 isCircular（rx=ry）区分：圆形角使用真圆弧，椭圆角使用椭圆弧的单条三次贝塞尔近似 C2b。CPU Path 与 GPU 求距共用同一条轮廓：圆形角两侧均为真圆弧，GPU 在保留弧区域使用精确圆距；椭圆角两侧均为三段三次曲线。目标是在 CPU Path 与 GPU 之间保持同一条几何轮廓，并通过限制设备空间描边宽度与角半径，使影响描边覆盖率的距离误差不超过 0.25px；不满足适用条件的形状使用路径回退。

误差控制需要分别解决曲线段选择和所选段的距离求解。3.1.3 给出近边界范围内的精确选段条件，3.1.2 给出 Gauss-Newton 的局部收敛性质；固定迭代次数的误差还需通过统一轮廓的数值实验验证，不能只由局部收缩因子推断。

##### 实现设计

**目标轮廓与输入数据。** 描边使用 CPU Path 的三段三次贝塞尔轮廓，包括两段过渡曲线 C1、C3 和保留弧的三次近似 C2b。三段均按相同控制点公式求值，避免几何轮廓差异随尺寸放大。CPU 侧通过统一系数生成 tangentRatio、controlStep、endOffsetX、endOffsetY、arcHandle，以 uniform 传给描边 shader；shader 结合设备空间半径生成控制点和连接点切线。

对每个角，定义有效设备半径 rx、ry，m=min(rx,ry)、M=max(rx,ry)，安全距离 R=m²/(2M)。定义 W 为有效设备空间完整描边宽度，h=W/2 为半宽。零宽 hairline 换算为 W=1px；非零宽度先按允许的等比设备缩放换算。定义 a=0.5px 为 AA 在描边外缘之外需要求距的额外宽度，NonAA 也采用同一保守范围。a 描述覆盖率函数的距离范围，不等于顶点 quad 的 aaBloat，不能用较大的 MSAA 几何外扩替代。

**最近曲线选择。** 在 h+a<R 的条件下，使用设备空间真实连接点法线 fs、fe，按 3.1.3 的符号表选择 C1、C2b 或 C3；椭圆不通过逆缩放到圆形框架选段。连接点上的查询可选择任一相邻段。s=1 时只剩两段和一条连接点法线；s=0 时直接使用普通圆角路径。

单角的 S4 内，片元到三段轮廓的距离至少为 R，超过描边及 AA 范围，因此直接返回该角贡献的覆盖率 0。对其他区域，每次只计算选中段，不预先计算三个距离再比较。整幅矩形的直边分发、角框不重叠、其他角竞争和设备变换约束由上游保证，单角定理不代替这些集成条件。

**三次曲线距离。** 使用三个初值 0、1/2、1，各执行 3 次 Gauss-Newton；只在迭代结束后把参数钳制到 [0,1]。两个端点独立加入候选并初始化最小距离，非有限候选不更新最小值。初值位于端点并不表示迭代结果仍在端点，因此不能省去端点比较。这也保证极小平滑度下部分初值数值失稳时仍有有效候选。

记 d̂ 为这些候选中的最小无符号距离。在精确算术下，候选点属于同一 CPU 轮廓，d̂ 不会低于真实最短距离，因此远处片元不会因距离被低估而误画描边；近处仍可能因距离被高估而减少覆盖率，浮点误差也需验证。该方法同时用于过渡段与保留弧的三次近似，不使用精确圆距或 Sampson 距离替代其中一段。

**覆盖率与 shader 变体。** 中心描边由无符号距离转换为环带距离：

```
ringDist = d̂−h
coverageAA = clamp(0.5−ringDist, 0, 1)
coverageNonAA = step(ringDist, 0)
```

描边的 3 次迭代函数与填充的 2 次迭代函数分别生成，作为 stroke 编译期变体，不增加逐 fragment 的迭代次数判断；填充的求值方式保持不变。描边每次求距共 9 个迭代步，2 次迭代的对照为 6 步，增加的是迭代部分的 50%，不是整个 shader 成本的 50%。保留弧也需要三次曲线求距，实际性能须由 GPU 测量确认。

##### 实验验证

**误差与参考解。** 定义 dref 为到被测 CPU 轮廓（三段三次曲线及相邻两条直线射线）的最短距离，测量 `|d̂−dref|`，目标为不超过 0.25px。dref 与 3.1.3 仅针对单角三段曲线的 db 不同。参考几何使用 float64 构造；每条三次曲线通过五次驻点方程求出 [0,1] 内全部实根，加入两个端点后比较，各段及两条射线再取最小。独立复核采用贝塞尔细分与控制点到弦段的距离，给出包含真值的距离区间，不与驻点根法共享求解过程。

统计集合包含参考 AA 范围、算法计算出的 AA 范围、错误显示描边的点和内部覆盖率被错误减少的点；未支持区域中存在参考非零覆盖率也记为失败。不影响覆盖率的深处距离误差不计入。不同宽度对应的统计区域不同，最大误差不必随宽度单调增加；判断宽度上限时必须同时检查更细的描边。

**统一误差总表。** 表内比较的是同一套三次曲线求距方法的 2 次、3 次迭代参数，不是两套实现方案。每行分别搜索两种迭代次数的最差平滑度、绝对半径和长轴方向，不共用最差 s。

测量配置为 float32 数值模型（统一 CPU 三次轮廓、三个初值、独立端点候选；设备空间；AA 额外宽度 0.5px；筛选条件 (W/2+0.5px)M/m²≤0.49、M≤16384px；M/m 取 1、1.25、1.5、2、3、4、6、8，W 取 1、4、16、32、48、50、64、96、100、104、112、128px，s 在 1e−8～1 内合并 136 个取值）。共 27,080 组不同配置、154,981,575 条查询记录，记录数包含加密重测，不包含迭代次数和浮点精度的重复求值。每段均匀参数样本从 33 加密至 129，候选极值采用 513/1025 点复核，变化较大或新增的配置追加 1025/2049 点复核；查询同时覆盖端点附近、曲线两侧法向距离及二维网格。float32 模型不等同于实际 GPU、FMA 或 C++ 数学库执行，也没有覆盖 CPU Path 每一步浮点运算的差异。

| M/m | W（px） | 2次最大误差（px） | 2次最差 s | 2次最差 (rx,ry)（px） | 3次最大误差（px） | 3次最差 s | 3次最差 (rx,ry)（px） |
|---:|---:|---:|---:|---|---:|---:|---|
| 1 | 1 | **19.829916** | 0.97 | (16384.000000, 16384.000000) | 0.007828 | 0.97 | (16384.000000, 16384.000000) |
| 1 | 4 | **18.206549** | 0.995 | (16384.000000, 16384.000000) | 0.011056 | 0.0485 | (5.102041, 5.102041) |
| 1 | 16 | **18.206549** | 0.995 | (16384.000000, 16384.000000) | 0.037592 | 0.0485 | (17.346939, 17.346939) |
| 1 | 32 | **18.206549** | 0.995 | (16384.000000, 16384.000000) | 0.072969 | 0.0485 | (33.673469, 33.673469) |
| 1 | 48 | **6.426497** | 0.995 | (16384.000000, 16384.000000) | 0.108356 | 0.0485 | (50.000000, 50.000000) |
| 1 | 50 | **10.013223** | 0.975 | (16384.000000, 16384.000000) | 0.112658 | 0.0495 | (52.040816, 52.040816) |
| 1 | 64 | **5.026260** | 0.995 | (16384.000000, 16384.000000) | 0.143730 | 0.0485 | (66.326531, 66.326531) |
| 1 | 96 | **3.613861** | 0.9945 | (16384.000000, 16384.000000) | 0.214497 | 0.0485 | (98.979592, 98.979592) |
| 1 | 100 | **3.432220** | 0.995 | (16384.000000, 16384.000000) | 0.223106 | 0.0495 | (103.061224, 103.061224) |
| 1 | 104 | **3.723915** | 0.985 | (16384.000000, 16384.000000) | 0.232178 | 0.0485 | (107.142857, 107.142857) |
| 1 | 112 | **3.461014** | 0.985 | (16384.000000, 16384.000000) | 0.249870 | 0.0485 | (115.306122, 115.306122) |
| 1 | 128 | **3.057396** | 0.989 | (16384.000000, 16384.000000) | **0.285248** | 0.0485 | (131.632653, 131.632653) |
| 1.25 | 1 | **21.029062** | 1 | (13107.200000, 16384.000000) | 0.007828 | 0.991 | (13107.200000, 16384.000000) |
| 1.25 | 4 | **21.029062** | 1 | (13107.200000, 16384.000000) | 0.006763 | 5.05e-07 | (7.971939, 6.377551) |
| 1.25 | 16 | **21.029062** | 1 | (13107.200000, 16384.000000) | 0.022992 | 1e-08 | (27.104592, 21.683673) |
| 1.25 | 32 | **21.029062** | 1 | (13107.200000, 16384.000000) | 0.044632 | 5.05e-07 | (42.091837, 52.614796) |
| 1.25 | 48 | **12.277437** | 1 | (16384.000000, 13107.200000) | 0.066269 | 1e-08 | (78.125000, 62.500000) |
| 1.25 | 50 | **12.032593** | 1 | (13107.200000, 16384.000000) | 0.068975 | 1e-08 | (81.313776, 65.051020) |
| 1.25 | 64 | **7.082237** | 1 | (13107.200000, 16384.000000) | 0.087902 | 1e-08 | (103.635204, 82.908163) |
| 1.25 | 96 | **4.751348** | 1 | (16384.000000, 13107.200000) | 0.131176 | 5.05e-07 | (154.655612, 123.724490) |
| 1.25 | 100 | **4.555907** | 1 | (13107.200000, 16384.000000) | 0.136608 | 5.05e-07 | (161.033163, 128.826531) |
| 1.25 | 104 | **4.376274** | 1 | (16384.000000, 13107.200000) | 0.142014 | 5.05e-07 | (167.410714, 133.928571) |
| 1.25 | 112 | **3.976913** | 1 | (16384.000000, 13107.200000) | 0.152847 | 5.05e-07 | (180.165816, 144.132653) |
| 1.25 | 128 | **3.520653** | 1 | (13107.200000, 16384.000000) | 0.174477 | 5.05e-07 | (164.540816, 205.676020) |
| 1.5 | 1 | **21.379063** | 1 | (10922.666667, 16384.000000) | 0.007828 | 0.999 | (10922.666667, 16384.000000) |
| 1.5 | 4 | **21.379063** | 1 | (10922.666667, 16384.000000) | 0.006466 | 1e-08 | (11.479592, 7.653061) |
| 1.5 | 16 | **21.379063** | 1 | (10922.666667, 16384.000000) | 0.021986 | 1e-06 | (39.030612, 26.020408) |
| 1.5 | 32 | **21.379063** | 1 | (10922.666667, 16384.000000) | 0.042683 | 1e-08 | (75.765306, 50.510204) |
| 1.5 | 48 | **13.016118** | 1 | (10922.666667, 16384.000000) | 0.063372 | 1e-08 | (75.000000, 112.500000) |
| 1.5 | 50 | **12.285254** | 1 | (10922.666667, 16384.000000) | 0.061279 | 0.01675 | (78.061224, 117.091837) |
| 1.5 | 64 | **6.950685** | 1 | (16384.000000, 10922.666667) | 0.084072 | 1e-08 | (149.234694, 99.489796) |
| 1.5 | 96 | **4.705638** | 1 | (10922.666667, 16384.000000) | 0.125458 | 1e-08 | (148.469388, 222.704082) |
| 1.5 | 100 | **4.517566** | 1 | (16384.000000, 10922.666667) | 0.130627 | 1e-08 | (154.591837, 231.887755) |
| 1.5 | 104 | **4.621429** | 1 | (16384.000000, 10922.666667) | 0.135803 | 1e-08 | (160.714286, 241.071429) |
| 1.5 | 112 | **4.065033** | 1 | (16384.000000, 10922.666667) | 0.146141 | 5.05e-07 | (259.438776, 172.959184) |
| 1.5 | 128 | **3.596214** | 1 | (16384.000000, 10922.666667) | 0.166840 | 1e-08 | (197.448980, 296.173469) |
| 2 | 1 | **21.629290** | 1 | (8192.000000, 16384.000000) | 0.007816 | 1 | (8192.000000, 16384.000000) |
| 2 | 4 | **21.629290** | 1 | (8192.000000, 16384.000000) | 0.005869 | 1e-08 | (10.204082, 20.408163) |
| 2 | 16 | **21.629290** | 1 | (8192.000000, 16384.000000) | 0.019954 | 1e-08 | (69.387755, 34.693878) |
| 2 | 32 | **21.629290** | 1 | (8192.000000, 16384.000000) | 0.038729 | 1e-08 | (67.346939, 134.693878) |
| 2 | 48 | **12.778376** | 1 | (8192.000000, 16384.000000) | 0.057512 | 1e-06 | (100.000000, 200.000000) |
| 2 | 50 | **12.524750** | 1 | (16384.000000, 8192.000000) | 0.059845 | 1e-06 | (208.163265, 104.081633) |
| 2 | 64 | **6.792366** | 1 | (8192.000000, 16384.000000) | 0.076283 | 1e-06 | (265.306122, 132.653061) |
| 2 | 96 | **4.770370** | 1 | (8192.000000, 16384.000000) | 0.113850 | 1e-08 | (197.959184, 395.918367) |
| 2 | 100 | **4.595734** | 1 | (16384.000000, 8192.000000) | 0.118523 | 1e-06 | (412.244898, 206.122449) |
| 2 | 104 | **4.662829** | 1 | (8192.000000, 16384.000000) | 0.123238 | 1e-06 | (428.571429, 214.285714) |
| 2 | 112 | **4.137020** | 1 | (16384.000000, 8192.000000) | 0.132626 | 1e-08 | (461.224490, 230.612245) |
| 2 | 128 | **3.649513** | 1 | (16384.000000, 8192.000000) | 0.151352 | 5.05e-07 | (526.530612, 263.265306) |
| 3 | 1 | **21.784258** | 1 | (5461.333333, 16384.000000) | 0.009766 | 1 | (5461.333333, 16384.000000) |
| 3 | 4 | **21.784258** | 1 | (5461.333333, 16384.000000) | 0.004748 | 1e-08 | (45.918367, 15.306122) |
| 3 | 16 | **21.784258** | 1 | (5461.333333, 16384.000000) | 0.016135 | 5.05e-07 | (52.040816, 156.122449) |
| 3 | 32 | **21.784258** | 1 | (5461.333333, 16384.000000) | 0.031349 | 5.05e-07 | (303.061224, 101.020408) |
| 3 | 48 | **12.871098** | 1 | (16384.000000, 5461.333333) | 0.046509 | 1e-06 | (450.000000, 150.000000) |
| 3 | 50 | **12.617945** | 1 | (5461.333333, 16384.000000) | 0.048405 | 5.05e-07 | (468.367347, 156.122449) |
| 3 | 64 | **6.809261** | 1 | (5461.333333, 16384.000000) | 0.061726 | 5.05e-07 | (596.938776, 198.979592) |
| 3 | 96 | **4.769482** | 1 | (5461.333333, 16384.000000) | 0.092075 | 5.05e-07 | (296.938776, 890.816327) |
| 3 | 100 | **4.677607** | 1 | (5461.333333, 16384.000000) | 0.095913 | 5.05e-07 | (927.551020, 309.183673) |
| 3 | 104 | **4.587979** | 1 | (16384.000000, 5461.333333) | 0.099735 | 1e-06 | (964.285714, 321.428571) |
| 3 | 112 | **4.135578** | 1 | (5461.333333, 16384.000000) | 0.107250 | 5.05e-07 | (345.918367, 1037.755102) |
| 3 | 128 | **3.647530** | 1 | (5461.333333, 16384.000000) | 0.122429 | 5.05e-07 | (394.897959, 1184.693878) |
| 4 | 1 | **21.917593** | 1 | (16384.000000, 4096.000000) | 0.032004 | 0.15 | (4096.000000, 16384.000000) |
| 4 | 4 | **21.917593** | 1 | (16384.000000, 4096.000000) | 0.004082 | 1e-08 | (20.408163, 81.632653) |
| 4 | 16 | **21.917593** | 1 | (16384.000000, 4096.000000) | 0.013875 | 5.05e-07 | (277.551020, 69.387755) |
| 4 | 32 | **21.917593** | 1 | (16384.000000, 4096.000000) | 0.026985 | 1e-08 | (538.775510, 134.693878) |
| 4 | 48 | **13.191243** | 1 | (4096.000000, 16384.000000) | 0.040009 | 1e-06 | (200.000000, 800.000000) |
| 4 | 50 | **12.991009** | 1 | (16384.000000, 4096.000000) | 0.041637 | 1e-06 | (832.653061, 208.163265) |
| 4 | 64 | **7.268630** | 1 | (4096.000000, 16384.000000) | 0.053085 | 5.05e-07 | (1061.224490, 265.306122) |
| 4 | 96 | **4.826092** | 1 | (16384.000000, 4096.000000) | 0.079197 | 1e-08 | (395.918367, 1583.673469) |
| 4 | 100 | **4.730692** | 1 | (16384.000000, 4096.000000) | 0.082458 | 5.05e-07 | (1648.979592, 412.244898) |
| 4 | 104 | **4.593346** | 1 | (4096.000000, 16384.000000) | 0.085777 | 1e-06 | (1714.285714, 428.571429) |
| 4 | 112 | **4.183998** | 1 | (16384.000000, 4096.000000) | 0.092278 | 1e-08 | (1844.897959, 461.224490) |
| 4 | 128 | **3.689140** | 1 | (16384.000000, 4096.000000) | 0.105339 | 5.05e-07 | (2106.122449, 526.530612) |
| 6 | 1 | **42.216644** | 0.12 | (16384.000000, 2730.666667) | **0.262794** | 0.12 | (2730.666667, 16384.000000) |
| 6 | 4 | **41.584900** | 0.099 | (2730.666667, 16384.000000) | 0.022786 | 0.1 | (2730.666667, 16384.000000) |
| 6 | 16 | **41.583359** | 0.099 | (16384.000000, 2730.666667) | 0.014856 | 5.05e-07 | (624.489796, 104.081633) |
| 6 | 32 | **41.583359** | 0.099 | (16384.000000, 2730.666667) | 0.028688 | 1e-06 | (1212.244898, 202.040816) |
| 6 | 48 | **41.583359** | 0.099 | (16384.000000, 2730.666667) | 0.042648 | 1e-08 | (1800.000000, 300.000000) |
| 6 | 50 | **41.516964** | 0.1375 | (2730.666667, 16384.000000) | 0.044395 | 5.05e-07 | (1873.469388, 312.244898) |
| 6 | 64 | **41.583359** | 0.099 | (16384.000000, 2730.666667) | 0.056572 | 5.05e-07 | (2387.755102, 397.959184) |
| 6 | 96 | **24.706733** | 0.105 | (16384.000000, 2730.666667) | 0.084328 | 1e-08 | (3563.265306, 593.877551) |
| 6 | 100 | **20.321307** | 0.099 | (16384.000000, 2730.666667) | 0.087875 | 5.05e-05 | (3710.204082, 618.367347) |
| 6 | 104 | **20.205528** | 0.099 | (2730.666667, 16384.000000) | 0.091305 | 5.05e-07 | (642.857143, 3857.142857) |
| 6 | 112 | **17.184693** | 0.105 | (2730.666667, 16384.000000) | 0.098469 | 1e-08 | (4151.020408, 691.836735) |
| 6 | 128 | **14.555637** | 0.15 | (2730.666667, 16384.000000) | 0.112488 | 5.05e-07 | (4738.775510, 789.795918) |
| 8 | 1 | **63.682705** | 0.084 | (2048.000000, 16384.000000) | **0.787821** | 0.084 | (16384.000000, 2048.000000) |
| 8 | 4 | **63.508175** | 0.105 | (2048.000000, 16384.000000) | 0.199610 | 0.105 | (2048.000000, 16384.000000) |
| 8 | 16 | **63.508175** | 0.105 | (2048.000000, 16384.000000) | 0.047977 | 0.105 | (2048.000000, 16384.000000) |
| 8 | 32 | **63.876411** | 0.095 | (2048.000000, 16384.000000) | 0.027890 | 0.095 | (2048.000000, 16384.000000) |
| 8 | 48 | **63.508175** | 0.105 | (2048.000000, 16384.000000) | 0.035744 | 5.05e-05 | (3200.000000, 400.000000) |
| 8 | 50 | **61.818604** | 0.0735 | (16384.000000, 2048.000000) | 0.037077 | 5.05e-05 | (3330.612245, 416.326531) |
| 8 | 64 | **63.508175** | 0.105 | (2048.000000, 16384.000000) | 0.047382 | 5.05e-07 | (530.612245, 4244.897959) |
| 8 | 96 | **63.508175** | 0.105 | (2048.000000, 16384.000000) | 0.070858 | 5.05e-07 | (6334.693878, 791.836735) |
| 8 | 100 | **63.508175** | 0.105 | (2048.000000, 16384.000000) | 0.073673 | 1e-06 | (6595.918367, 824.489796) |
| 8 | 104 | **63.508175** | 0.105 | (2048.000000, 16384.000000) | 0.076420 | 5.05e-05 | (6857.142857, 857.142857) |
| 8 | 112 | **63.508175** | 0.105 | (2048.000000, 16384.000000) | 0.082699 | 5.05e-07 | (7379.591837, 922.448980) |
| 8 | 128 | **63.876411** | 0.095 | (2048.000000, 16384.000000) | 0.094002 | 1e-06 | (1053.061224, 8424.489796) |

加粗表示该组合已出现超过 0.25px 的误差，或存在未支持的可见样本。两列最差 (rx,ry) 保留长短轴方向；最大值仅为实际样本记录，不是连续参数域的精确上限。

每行两种迭代次数的最差查询点分别以 1e−5px、5e−6px 细分容差复核，共 384 项全部通过，细容差下的最大距离区间宽度小于 0.000004913px。逐行 513→1025 点加密时，2 次、3 次迭代的最大读数变化分别为 2.690878px、0.014633px；追加 1025→2049 点检查的两种迭代次数各覆盖 54 行，最大变化分别为 1.119445px、0.000801px。2 次迭代的部分极值仍未充分稳定，已发现的超差点可以否定通过条件，但不能把记录值当作精确误差上限。

**数值实验脚本与结果。** 仅保留支撑统一矩阵的结果入口，逐次扫描清单、最差查询点与加密记录由结果文件承载。

| 文件 | 内容 |
|---|---|
| `tools/sweep_newton_stroke_width.py` | 参数化扫描、2/3 次迭代、float64/float32 求值、全根参考解、独立细分复核及 `--matrix-inputs` 总表汇总 |
| `designs/data/corner-stroke/matrix-errors.md` | 96 行统一矩阵的独立阅读版本 |
| `designs/data/corner-stroke/matrix-errors.json` | 每行最差参数、查询点、误差、原始文件清单及双密度检查 |
| `designs/data/corner-stroke/matrix-errors.validation.json` | 96 行×2 种迭代次数×2 种参考容差的 384 项独立距离区间校验 |

##### 取值建议

**迭代次数取 3。** 总表中 W=50px 的八个半径比例组均出现 2 次迭代超差，不能通过把宽度上限降到 50px 保留 2 次迭代。3 次迭代在 M/m≤4、已测 W≤100px 的组合中没有发现超过 0.25px 的样本，填充仍保持 2 次。

**设备空间宽度与半径条件同时限制。** 对每个角使用归一化后的有效半径，满足已有的无尖角、无角框竞争及变换要求后，采用：

```
绝对宽度限制：W ≤ 96px
半径关系限制：(W/2 + 0.5px)·M ≤ 0.49·m²       （每个角分别检查）
```

第二条等价于 h+0.5px≤0.98R<R，使需要准确距离的范围处于近边界定理的安全距离之内；正圆时等价于 W≤0.98r−1px。它同时约束长短轴，不可替换成只比较最小半径的 W≤m。W=100px 的最大采样误差为 0.223106px，也低于目标，但本节采用更保守的 96px 上限；这两个采样结果均不构成全部中间参数或实际 GPU 的误差证明。

**长短轴比暂取 M/m≤4。** 3 次迭代在 M/m=6、8 的大半径 1px 描边上分别出现 0.262794px、0.787821px 的误差，因此不能因为某个较宽描边档位通过，就取消比例限制。

实测适用范围同时限定为 `1px≤W≤96px、M≤16384px、M/m≤4、1e−8≤s≤1`。零宽 hairline 对应 W=1px，s=0 使用普通圆角路径；非零但不足 1px 的描边、更小的正平滑度、范围外尺寸与更大长短轴比使用路径回退。非等比 CTM、剪切、透视、尖角及未验证的描边连接样式不在本节支持范围内。

这些取值针对统一 CPU 三次轮廓、真实法线选段、独立端点候选和远处片元处理共同组成的方法，不能只修改分发阈值而省略求值部分。选段正确性由 3.1.3 证明，固定迭代次数的精度由实验支持；接入后还必须通过实际 GPU 与 CPU Path 对照验证 0.25px 距离目标，单角实验不代替整幅圆角矩形的集成验证。

### 5.6 分发策略与性能结论

结论：**GPU 解析为主路径，CPU 路径为降级**，与现有常规圆角矩形的分发结构一致。

依据（区分事实与推测）：

已验证事实。其一，阴影扩散的 mask 由 `MakeSpreadShapeImage` 经 `PictureRecorder` 加`canvas->drawRRect` 生成，只有 `drawRRect` 走解析路径时扩散计算才留在 GPU shader 内完成；若对平滑度一律降级走 CPU，扩散 mask 就退回 CPU 光栅化。其二，解析路径可把至多 1024 个尺寸与半径各异的圆角矩形合入一个 draw call（`MaxNumRRects = 1024`），而 CPU 路径的合批要求 `getUniqueKey()` 完全相同且矩阵仅差平移（`OpsCompositor.cpp:202-224`），异构场景下无法合批，这是数量级差异。其三，CPU 路径依赖 `getUniqueKey()` 缓存，几何逐帧变化（尺寸或半径动画）时缓存收益为零，反而每帧付出三角化与缓存分配成本。其四，CPU 贝塞尔构造无论如何都要实现，因为它是 SVG/PDF 导出、椭圆角、非等比缩放的必需项，故"少写一份实现"不能作为支持 CPU 路径的论据。

推测部分。经 5.5节的三项优化后，逐 fragment 成本中只有命中过渡曲线段的部分需要牛顿迭代，圆弧扇区只需一次 `length()`（比现有椭圆 Sampson 距离更省，无梯度校正），边与中心区被 `mix` 跳过。过渡曲线区占比随半径与平滑度变化，需实测确认。

一并说明：单个大面积静态形状上，CPU 路径缓存命中后的 fragment 成本确实更低。本方案判断该收益不足以换取双主路径分发的复杂度与轮廓一致性风险，§1.2 据此把这项专项优化列为不在范围内；若后续实测证明它是热点，可作为独立优化项在 `UseDrawPath` 中按面积与静态性追加降级规则，不影响本方案结构。

`UseDrawPath` 在平滑度大于零时追加的降级条件：椭圆扁平比超出阈值；矩阵非等比缩放；描边宽度超过阈值（`stroke->width` 大于任一非零角半径，依据见 5.5.7 节）时描边回退；以及现有全部条件，其中对角重叠判定的半径改用 `(1+s)r`。注意椭圆角本身不再回退（5.1 节）。

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

路径与真源一致性：平滑 rrect 的 path 的 verb 数落在预期区间（锁定 5.6 节的三角化前提）；其`isRRect` 与 `isOval` 均返回假（锁定 5.4 节的隐式依赖）；`startIndex` 对 0 到 7 的起点落点正确且 `reversed` 方向相反；按 5.2 节系数在 C++ 侧实现与 GLSL 等价的 SDF，沿 CPU 贝塞尔曲线采样点求值应近似为零，容差取亚像素，覆盖平滑度 0.2/0.5/0.8/1.0，这是轮廓一致性的核心锁定；验证 5.5.3 节的叉积符号法在过渡曲线段、圆弧接缝、边线接缝三处均给出正确符号，含端点 clamp 情形；NonAA 变体的隐式符号与牛顿参考在过渡段辖区内逐点一致（对应 `verify_smooth_corner_math.py` 第 8 项检查，含椭圆角 ky ≠ 1 情形）；系数在平滑度 0 与 1 两端的退化值正确。

变体与合批：平滑度为 0 与 0.5 的两个 `RRectEffect` 的 processor key 不同（锁定 5.5.5 节）；平滑度 0.3 与 0.7 的 key 相同（锁定数值不进key）；连续绘制平滑度 0、0.5、0.5 三个圆角矩形时前两者之间打断、后两者合批。

扩散：`MakeSpreadRRect` 对零半径角在正扩散下保持为零（尖角不被圆化，对齐 3.3.6 节实测）；平滑度原值传递；扩散量较大时由两级降级处理且结果自洽；负扩散使半径归零时角变尖且平滑度为 0；同 rect 与 radii、不同平滑度的 path 包围盒完全相同（锁定 5.7 节 `filterBounds` 无需改动的依据）。

裁剪与导出：`ConservativeIntersect` 对含平滑度的输入返回 `nullopt`；`ContainsPoint` 与 `ContainsRect` 返回假；SVG 导出结果不含 `<rect rx` 而改用 `<path`。

描边解析化：阈值判据单测（宽度等于非零角最小半径时走解析路径、超出时回退）；椭圆角描边回退单测；含尖角的平滑矩形描边仍回退 drawPath（`hasSharpCorner` 判据不变）。截图用例：平滑描边的平滑度阶梯（s 取 0.25/0.5/1.0，w/2r 约 0.3）；阈值边界并排（w/2r = 0.5 走解析、0.6 走回退）；解析描边与 CPU 描边并排对照（w/2r = 0.3，验证环带重合）；超阈值对照 cell（w/2r = 0.6 时强制解析渲染与 CPU 渲染并排，裁决阈值可否放宽）。

截图测试（归入既有绘制/裁剪测试的 cell 网格，不设独立测试文件，内容居中、坐标取整；平滑用例统一叠加无平滑度的品红底层，平滑轮廓内收处呈现新月形色差以便肉眼校验）：`CanvasTest/AARRectOp` 扩为四列并新增平滑用例——简单满平滑填充（解析 Op）与描边（描边一律回退 CPU 路径，但平滑轮廓须生效）、四角圆形半径不等加平滑度（锁定解析 Op，对角的曲线连接点偏移需留出 `(1+s)` 余量）、边界竞争两级降级（削平滑度仍走解析 Op、缩半径后等同普通圆角）、仅因平滑度才发生的对角重叠回退、解析路径与 CPU 路径的并排对照（关键，验证轮廓重合）、非等比缩放的回退（结果为拉伸的平滑圆角）、椭圆角平滑的解析绘制（锁定 5.1 节契约，圆形框架分派 + 设备框架求距）；`CanvasTest/NonAARRectOp` 网格化并新增 NonAA 平滑填充（走隐式符号判定，5.5.3 节）；`CanvasTest/NonAARRectOpStroke` 网格化并新增描边加平滑度的回退用例；`CanvasTest/RRectOpBatchSmooth` 以 30 个以上尺寸与半径各异但平滑度相同的形状端到端验证合批；`ClipTest/ClipOverview` 新增平滑圆角裁剪 cell（含等比缩放变体），锁定裁剪 FP 与绘制 GP 轮廓同源；平滑圆角配 `DropShadowStyle` 在扩散 0/10/30 下阴影保持平滑角，且零半径角在扩散后仍为尖角；配 `InnerShadowStyle` 在扩散 0/10 下的表现；负扩散使内腔消失时的退化表现。

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
| `src/gpu/glsl/processors/GLSLSmoothRRectGeometryProcessor.h` / `.cpp` | 新增，复用 `GLSLSmoothCorner`；`emitCode` 按 AA/NonAA 与圆/椭圆变体分叉，NonAA 过渡段用隐式符号判定 |
| `src/gpu/RRectsVertexProvider.h` / `.cpp` | 新增 `SmoothRRectsVertexProvider`（单 quad、圆形变体每角标量半径、椭圆变体每角 `rx`/`ry`），`MakeFrom` 增加分派维度 |
| `src/gpu/ops/RRectDrawOp.h` / `.cpp` | 新增 `hasSmoothness` 字段，调整 `indicesPerRRect` 与 GP 选择 |
| `src/gpu/OpsCompositor.cpp` | 新增平滑度合批打断条件 |
| `src/core/Canvas.cpp` | `UseDrawPath` 新增降级条件（椭圆扁平比阈值、非等比缩放），椭圆角改为解析支持，对角重叠判定改用 `(1+s)r` |
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

### 阶段 8 描边解析化

| 文件 | 说明 |
|---|---|
| `src/gpu/glsl/GLSLSmoothCorner.h` / `.cpp` | 角区求值的三档分级（5.5.7 节的模式表） |
| `src/gpu/processors/SmoothRRectGeometryProcessor.h` / `.cpp`、`src/gpu/glsl/processors/GLSLSmoothRRectGeometryProcessor.cpp` | 新增 stroke 变体、环形覆盖率，processor key 加入 stroke 位 |
| `src/gpu/RRectsVertexProvider.h` / `.cpp` | stroke 变体的 quad 外扩与 halfStroke 顶点属性 |
| `src/gpu/ops/RRectDrawOp.h` / `.cpp` | 变体选择 |
| `src/core/Canvas.cpp` | `UseDrawPath` 描边阈值判据 |
| `test/src/…` | 阈值单测与描边截图 |

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
| 描边宽度阈值扫参 | `tools/sweep_newton_stroke_width.py` |
| 平滑圆角背景文章 | https://www.figma.com/blog/desperately-seeking-squircles/ |
