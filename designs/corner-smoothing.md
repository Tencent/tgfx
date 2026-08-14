# 圆角平滑度（Corner Smoothing）技术方案

## 1 背景

### 1.1 问题描述

tgfx 的圆角矩形（`RRect`）目前只支持标准圆角：每角是一段椭圆弧，弧与相邻边在切点处一阶连续（G1），但曲率在切点处从 `1/r` 突变为 `0`，视觉上能看出"圆弧接直线"的转折。

设计工具普遍提供"圆角平滑度"参数来消除这一转折：在圆弧两侧各插入一段过渡曲线，让曲率连续变化，得到俗称squircle 的外观。本方案将该能力迁移到 tgfx，并让阴影扩散（shadow spread）同时支持它。

### 1.2 需求

1. `RRect` 支持圆角平滑度参数，四角统一单值，半径仍每角独立。
2. 普通 `Canvas` 绘制圆角矩形时支持平滑度。
3. `DropShadowStyle` 与 `InnerShadowStyle` 的 spread 支持平滑度，且扩散计算走 GPU shader。
4. 矢量元素层 `Rectangle` 暴露平滑度接口，使平滑度能贯通到阴影链路。

`DropShadowFilter` 是 `LayerImageFilter`，只对已光栅化的图像做位移与模糊，既无 spread 参数也拿不到矢量几何，不在本次范围内。

### 1.3 术语

| 术语 | 含义 |
|---|---|
| `smoothness` | 平滑度，无量纲，取值 `[0, 1]`，四角统一单值 |
| `r` | 某角的圆形半径（标量） |
| `L = (1+s)·r` | 平滑角在边上的切点到角顶点的距离，下称"角足迹" |
| `φ = (π/2)(1-s)` | 直角处保留的圆弧扫过角 |
| `a = (π/4)·s` | 单侧过渡曲线的转角 |
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

### 2.2 GPU 解析渲染路径

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

`src/gpu/ops/RRectDrawOp.cpp:67-82` 按 `aaType` 与 `isComplex` 两个维度在四个 GeometryProcessor 之间选择。`RRectDrawOp.h:33-47` 定义 `MaxNumRRects = 1024`、`IndicesPerAAFillRRect = 54`、`IndicesPerAAStrokeRRect = 48`、`IndicesPerNonAARRect = 6`。

`src/gpu/OpsCompositor.cpp:127-133` 的 `ComplexMismatch` 决定 `drawRRect`（150行起）是否打断合批，该函数只比较 `isComplex`。

### 2.3 shader 变体机制

`src/gpu/glsl/processors/GLSLRectEffect.cpp:68-77` 展示了本项目的变体做法：以构造期确定的 `needTransform()` 决定是否添加 `DeviceToLocal` uniform 并改变取坐标的代码，`onSetData` 以同样条件传值。`RectEffect.h:52` 在构造函数里由 `!deviceToLocal.isIdentity()` 求出 `_needTransform`。

这是编译期分变体而非运行期 uniform 分支，使不需要该能力的绘制不承担任何额外成本。

### 2.4 Canvas 分发

`src/core/Canvas.cpp:397-434` 的 `drawRRect`：空 rect转 `drawRect`；全部角在设备空间不足半像素时转 `drawRect`；`UseDrawPath` 为真时转 `drawPath`；否则进入 `drawContext->drawRRect` 的解析路径。

`HasSharpCorner`（298-309 行）判定任一半径乘设备缩放后小于 `0.5`。`UseDrawPath`（330-395 行）在尖角、对角重叠、厚描边等情形返回真。

`drawPath`（469-481 行）会用 `path.isOval` 与 `path.isRRect` 反推`RRect` 再走解析路径。

### 2.5 Path 与 RRect 互转

`src/core/Path.cpp:173-175` 的 `isRRect` 经 pathkit 的 rrect 检测反推；`addRRect`（520-524 行）把 `RRect` 转为 pathkit 的 rrect。两者都没有承载平滑度的通道。

### 2.6 Path 通用路径成本

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

### 2.7 阴影扩散

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

### 2.8 其他 RRect 消费点

`src/core/RRectUtils.cpp:95-102` 的 `IsInnerToCorners` 写死椭圆方程：

```cpp
const float ellipseTest =
    centerOffset.x * centerOffset.x * ry * ry + centerOffset.y * centerOffset.y * rx * rx;
const float limit = (rx * ry) * (rx * ry);
return ellipseTest <= limit;
```

它被 `ConservativeIntersect`（165 行）与 `ContainsPoint` / `ContainsRect`（193-200 行）使用，调用方为 `src/core/ClipStack.cpp:294`、`:365`、`:376` 与 `src/core/GeometryShape.cpp:131`、`:145`。

导出侧：`src/svg/SVGExportContext.cpp` 对 simple 类型输出 `<rect rx ry>`，complex 类型转path；`src/pdf/PDFExportContext.cpp` 一律走 `path.addRRect`。

### 2.9 矢量元素层

`include/tgfx/layers/vectors/Rectangle.h` 的 `_roundness` 是 `std::array<float, 4>`（标量半径）。`src/layers/vectors/Rectangle.cpp:73-99` 的 `apply()` 把它转为 `std::array<Point, 4>`，经 `RRect::MakeRectRadii` 归一化后 `path.addRRect(rRect, _reversed, 2)`，结果缓存在 `_cachedShape`。

`RoundCorner`（`src/layers/vectors/RoundCorner.cpp`）走 `PathEffect::MakeCorner`，是对任意 path 的通用倒角，与矩形圆角是两套机制。

---

## 3 参考设计

### 3.1 Ardot 的CPU 实现

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

### 3.2 Figma 的 GPU 实现

以下代码由 `tools/extract_figma_smooth_shader.py` 从压缩 shader 中提取并展开，变量名保持提取原样（均为压缩后的短名）。

填充路径的 uniform 声明含平滑度，且为每角独立的 `vec4`：

```glsl
uniform vec4 cornerRadii;
uniform vec4 cornerSmoothings;
uniform int hasUniformCorners;
```

分发函数按四角是否统一选择快路：

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
  float cj = bs * (1.0 + bt);     // 角足迹
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
  float bz = (1.0 + bt) * bs;        // 角足迹
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

### 3.3 Figma 的 spread 口径

Figma 的阴影 shader 与填充 shader 是两套。阴影侧的 uniform 块（由 `figma_shaders/ShadowShaders_all.txt` 提取，共四个块）为：

```
mat3 vertexTransform; vec4 quad; vec4 color; float blurRadius; ...
mat3 vertexTransform; vec4 shadowBounds; vec4 maskBounds; vec4 color; float blurRadius; int useAAAMask; ...
mat3 vertexTransform; vec4 quad; vec4 color; vec2 halfSizeOverBlurRadius; float blurRadius; float cornerRadiusOverBlurRadius;
mat3 vertexTransform; vec4 shadowBounds; vec4 maskBounds; vec4 color; vec2 maskHalfSize; vec2 shadowHalfSize; float blurRadius; float maskCornerRadius; float shadowCornerRadius; float invBlurRadius; int useAAAMask; ...
```

关键点是第四个块：阴影与遮罩**各自携带独立的 halfSize 与 cornerRadius**（`shadowHalfSize` / `shadowCornerRadius` 对 `maskHalfSize` / `maskCornerRadius`）。内阴影主函数据此分别求值：

```glsl
void main() {
  float bx = 1.0 - x(_centeredNormShadowBlurCoords, shadowHalfSize * invBlurRadius,
                     shadowCornerRadius * invBlurRadius);
  float by = bv(_centeredMaskRectCoords, maskHalfSize, maskCornerRadius);
  float bz = bool(useAAAMask) ? bp(by) : 1.0 - step(0.0, by);
  float ca = bx * bz;
  fig_FragColor = d(color * ca);
}
```

即 spread 的实现方式是**给阴影传一组独立的形状参数重新求值**，而不是对内容几何做同心膨胀。

这一点由截图实测独立验证。`tools/measure_figma_shadow_corner.py` 对一张 `圆角 = 0/30/0/0`、`spread = 17`、`blur = 4`、阴影偏移 `(50,50)` 的截图，以 50% 等值线追踪未被遮挡的左下角（该角圆角为 0）：

```
y=1221..1301 共 34 个采样点，最左边界恒为 x=181
y=1306x=182
y=1311  x=183
y=1316  x=191
```

左边界在 155 像素范围内保持为一条直线直到贴近角部，最后约 10 像素的内收宽度与 `blur = 4` 的软边一致。若 spread 是"bounds 外扩且半径加 distance"的同心膨胀，该尖角会被圆化为半径 17 的圆角，实测不存在这一圆化。

因此结论是：**Figma 的 spread 不改变角的性质，尖角仍是尖角**；平滑度随形状参数一起重新指定，不需要额外的传递公式。

### 3.4 两种圆弧截断口径

Ardot 与 Figma 的控制点构造是同构的。以下等价关系由 `tools/verify_smooth_corner_math.py` 数值验证通过：

| 量 | Ardot | Figma | 关系 |
|---|---|---|---|
| 角足迹 | `tangent_distance·(1+s)`，直角下 `tan(45°)=1` 故为 `(1+s)r` | `bz = (1+bt)·bs` | 相同 |
| 过渡曲线第二控制点 | 两切线交点 | `an = al + 3w·û`，即切线交点 | 相同 |
| 过渡曲线第一控制点 | 起点到交点的 `2/3` | `am = al + 2w·û` | 相同，均为 `2/3` |
| 过渡曲线转角 | 隐含 `(π/2 - φ)/2` | `bx = (π/4)·bt` | 代入 `φ=(π/2)(1-s)` 后相等 |
| 圆弧控制柄 | `4(1-cos θ)/(3 sin θ)·r` | `(4/3)·tan(θ/2)·r` | 代数恒等 |

唯一的实质差异是圆弧的截断口径：**Ardot 按弧长截断**（对圆弧的三次贝塞尔近似做`getSegment`），**Figma 按角度截断**（`abs(cd) < by*0.5` 的扇区判定）。由于贝塞尔近似的弧长与圆心角并非严格成正比，两者存在亚像素级偏差。

### 3.5 Skia 对照

Skia core 没有圆角平滑度概念。`include/core/SkRRect.h:536-537` 的私有成员只有 `fRect`、`fRadii[4]`、`fType`，`Type` 枚举（:70-78）为 Empty/Rect/Oval/Simple/NinePatch/Complex；`SkCanvas` 只提供 `drawRRect(const SkRRect&, const SkPaint&)`（:1454）与 `drawRoundRect(rect, rx, ry, paint)`（:1565），均无平滑度入参。仓库中 squircle 与 superellipse 的命中都在 GM 测试内：`gm/fwidth_squircle.cpp` 的注释自述用途是验证 `fwidth()` 在各 GPU 配置上的行为，`gm/runtimeshader.cpp:432-452` 的 `ClipSuperRRect` 是超椭圆 `x^n + y^n = 1` 的 RuntimeShader 示例，与分段贝塞尔模型不同。故无可借鉴处。

---

## 4 对比分析

| 维度 | Ardot | Figma | 本方案 |
|---|---|---|---|
| 求值位置 | CPU 生成贝塞尔路径 | GPU fragment shader 求SDF | GPU 为主，CPU 为降级与导出 |
| 平滑度粒度 | 四角统一单值 | 每角独立`vec4` | 四角统一单值 |
| 半径形态 | 标量 | 标量 | 标量（椭圆角降级走 CPU） |
| 圆弧截断 | 按弧长 | 按角度 | 按角度 |
| 两级降级 | ✅ | ✅ | ✅ |
| 阴影扩散共存 | ❌ 明确禁止 | ✅ 参数化重新求值 | ✅ 参数化重新求值 |

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

Figma 的 `cn()` 对四个角无条件各求一次再取 max。本方案复用现有 `GLSLRRectEffect` 的 arc-box one-hot 选角机制（2.2 节 Step 2），只把选择框从 `radii[i]` 加宽到 `tangentRatio·radii[i]`，因为平滑角轮廓延伸到 `(1+s)r`。

正确性前提是加宽后的四个角框互不相交，这由两处保证。同一条边上相邻两角由 5.3 节的降级保证 `(1+s)(r_a + r_b) <= edgeLength`。对角方向需分类型讨论：`Simple` 类型四角半径相同，同一条边上的两角即对角方向的两角，故边约束已等价于对角约束，加宽后不可能重叠；`Complex` 类型的对角重叠由 `HasDiagonalCornerAxisOverlap` 判定，其半径需同步改用 `(1+s)r`，命中则降级走 CPU 路径。注意该函数首行有 `if (!rRect.isComplex()) return false;`，因此它只覆盖 `Complex`，`Simple` 的安全性完全依赖上述边约束等价性，此结论由 `tools/verify_smooth_corner_math.py` 的对角框检查覆盖。

由此每个 fragment 至多求值一个角，且内部fragment 经`mix(edgeDist, cornerDist, inAnyCorner)` 完全跳过角求值。

#### 5.5.2 消除逐fragment 三角函数

依5.2 节，7 个系数在 CPU 侧算好后打成两个 `Float4` uniform 传入，shader 内不含任何三角函数。Figma 原版每个 fragment 需要三次 `tan`、一次 `sin`、一次 `cos`、一次 `atan`。

#### 5.5.3 内外符号

牛顿迭代已得到最近点参数 `t*`，可用 `sign(cross(B'(t*), p - B(t*)))` 求符号，替代 Figma 的三次方程求根加射线交叉计数。

该化简在最近点被clamp 到端点时垂直性不成立，符号可能出错。设计上两个端点分别落在边线段（由线性分支接管）与圆弧接合点（由扇区分支接管），SDF 在接缝处连续故不应暴露。此项标注为推测，必须在实现阶段由单测数值验证；若不成立则退回 Figma 的原始做法。

#### 5.5.4 shader 变体

照 2.3 节 `GLSLRectEffect` 的模式，以构造期确定的 bool 分变体。`src/gpu/processors/RRectEffect.h`：

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

现有 AA 路径用 4×4 顶点网格，其切线位于 `bounds.left + radii`，而平滑角轮廓延伸到 `(1+s)r`，会被网格裁掉外侧。故平滑 RRect 在 AA 与 NonAA 下统一采用单 quad 布局（4 顶点，6 索引），复用现有 NonAA 路径的索引缓冲形态，与 Figma 的形态一致。5.5.1 节的 one-hot 选角已消除单 quad 带来的多角求值成本。

新增 `SmoothRRectsVertexProvider`，每角只需一个标量半径（四角共 4 个 float），比现有 complex 版的 `xRadii(4) + yRadii(4)` 更省。平滑度作为 uniform 而非顶点属性，因此同一个 op 内所有圆角矩形必须共享同一平滑度。

`ComplexMismatch` 之外新增一条打断条件，比较平滑度数值是否相等。代价是不同平滑度的圆角矩形无法合批；实际使用中平滑度是设计系统级常量，同一界面基本统一，Figma 的 `hasUniformCorners` uniform 也反映了同样的假设。后续如需放开，可把系数移入顶点属性。

`RRectDrawOp` 增加 `hasSmoothness` 字段，`indicesPerRRect` 在其为真时取 `IndicesPerNonAARRect`，`onMakeGeometryProcessor` 在现有四路选择之前插入平滑分支。

FP 与 GP 两侧的 GLSL 生成代码抽为`src/gpu/glsl/GLSLSmoothCorner.{h,cpp}` 的自由函数共用，保证裁剪与填充的轮廓同源。

### 5.6 分发策略与性能结论

结论：**GPU 解析为主路径，CPU 路径为降级**，与现有常规圆角矩形的分发结构一致。

依据（区分事实与推测）：

已验证事实。其一，需求要求阴影扩散走 GPU shader，而 `MakeSpreadShapeImage` 是通过 `PictureRecorder` 加`canvas->drawRRect` 生成 mask 的，只有 `drawRRect` 走解析路径时该要求才成立；若对平滑度一律降级走 CPU，扩散 mask 就退回 CPU 光栅化。其二，解析路径可把至多 1024 个尺寸与半径各异的圆角矩形合入一个 draw call（`MaxNumRRects = 1024`），而 CPU 路径的合批要求 `getUniqueKey()` 完全相同且矩阵仅差平移（`OpsCompositor.cpp:202-224`），异构场景下无法合批，这是数量级差异。其三，CPU 路径依赖 `getUniqueKey()` 缓存，几何逐帧变化（尺寸或半径动画）时缓存收益为零，反而每帧付出三角化与缓存分配成本。其四，CPU 贝塞尔构造无论如何都要实现，因为它是 SVG/PDF 导出、椭圆角、非等比缩放的必需项，故"少写一份实现"不能作为支持 CPU 路径的论据。

推测部分。经 5.5节的三项优化后，逐 fragment 成本中只有命中过渡曲线段的部分需要牛顿迭代，圆弧扇区只需一次 `length()`（比现有椭圆 Sampson 距离更省，无梯度校正），边与中心区被 `mix` 跳过。过渡曲线区占比随半径与平滑度变化，需实测确认。

一并说明：单个大面积静态形状上，CPU 路径缓存命中后的 fragment 成本确实更低。本方案判断该收益不足以换取双主路径分发的复杂度与轮廓一致性风险，若后续实测证明它是热点，可作为独立优化项在 `UseDrawPath` 中按面积与静态性追加降级规则，不影响本方案结构。

`UseDrawPath` 在平滑度大于零时追加的降级条件：任一非零角 `rx != ry`；矩阵非等比缩放；以及现有全部条件，其中对角重叠判定的半径改用 `(1+s)r`。

### 5.7 阴影扩散

采用 Figma 的参数化路线（3.3 节）：扩散不对内容几何做同心膨胀，而是构造一组独立的形状参数重新求值。

这带来两项与现状不同的行为，均与 Figma 实测一致：其一，尖角在扩散后仍是尖角，不会被圆化；其二，平滑度随形状参数一起指定，不需要额外的传递公式。

`MakeSpreadRRect` 改为按参数化口径构造扩散后的形状参数：bounds 仍 `outset(distance)`；每个**非零**半径加上 `distance`（零半径角保持为零，即尖角不被圆化，这是现有实现已有的行为）；平滑度**原值传递**。若结果违反 `(1+s)(r_a + r_b) <= edgeLength`，交由 5.3 节 `setRectRadii` 内的两级降级自然处理。

关于平滑度原值传递的说明。`tools/verify_smooth_corner_math.py` 的第 4 项检查实测到：保持平滑度不变时角足迹之和的增长快于边长增长，例如半径20 与 30、边长 100、平滑度 0.6，在扩散 20 时角足迹之和为 144 而可用边长为 140，此时两级降级把平滑度降至 0.556；扩散 100 时降至 0.200，扩散 1000 时降至 0.024。即平滑度随扩散量增大而自然消退，这与大尺度膨胀趋于胶囊形的几何事实一致。本方案接受该行为，因为它与参数化语义相符：新参数只需自身满足约束，超出时按既有降级规则处理，无需引入两个参考实现都不具备的自创衰减公式。

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

`RRectUtils` 的 `IsInnerToCorners` 写死椭圆方程（2.8 节），对平滑角不保守：椭圆测试可能把落在平滑轮廓之外的点判为在内，使 `ContainsPoint` 或 `ContainsRect` 错误返回真，进而让裁剪被错误地优化掉。

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

`RoundCorner` 是对任意 path 的通用倒角，本次不改动。

---

## 6 测试计划

### 6.1 单元测试

几何与降级：平滑度 clamp 到 `[0,1]`；第一级降级只削平滑度不动半径（200×200、四角 60、平滑度 1，期望平滑度降为 `200/120 - 1`、半径保持 60）；第二级接棒（四角 120、平滑度 0.8，期望平滑度为 0 且半径被缩至 100）；四角半径不等时取四条边的最小允许值；`MakeOval` 加平滑度后平滑度为 0；全零半径时类型为 `Rect` 且平滑度为 0；`scale` 与 `offset` 后平滑度不变且不变式仍成立；rect 与 radii 相同而平滑度不同时 `operator!=` 成立。

路径与真源一致性：平滑 rrect 的 path 的 verb 数落在预期区间（锁定 5.6 节的三角化前提）；其`isRRect` 与 `isOval` 均返回假（锁定 5.4 节的隐式依赖）；`startIndex` 对 0 到 7 的起点落点正确且 `reversed` 方向相反；按 5.2 节系数在 C++ 侧实现与 GLSL 等价的 SDF，沿 CPU 贝塞尔曲线采样点求值应近似为零，容差取亚像素，覆盖平滑度 0.2/0.5/0.8/1.0，这是轮廓一致性的核心锁定；验证 5.5.3 节的叉积符号法在过渡曲线段、圆弧接缝、边线接缝三处均给出正确符号，含端点 clamp 情形；系数在平滑度 0 与 1 两端的退化值正确。

变体与合批：平滑度为 0 与 0.5 的两个 `RRectEffect` 的 processor key 不同（锁定 5.5.5 节）；平滑度 0.3 与 0.7 的 key 相同（锁定数值不进key）；连续绘制平滑度 0、0.5、0.5 三个圆角矩形时前两者之间打断、后两者合批。

扩散：`MakeSpreadRRect` 对零半径角在正扩散下保持为零（尖角不被圆化，对齐 3.3 节实测）；平滑度原值传递；扩散量较大时由两级降级处理且结果自洽；负扩散使半径归零时角变尖且平滑度为 0；同 rect 与 radii、不同平滑度的 path 包围盒完全相同（锁定 5.7 节 `filterBounds` 无需改动的依据）。

裁剪与导出：`ConservativeIntersect` 对含平滑度的输入返回 `nullopt`；`ContainsPoint` 与 `ContainsRect` 返回假；SVG 导出结果不含 `<rect rx` 而改用 `<path`。

截图测试（key 前缀 `SmoothRRectTest/`，内容居中、四边约 50 像素边距、坐标取整）：平滑度阶梯（一行 5 个同尺寸方块，平滑度 0到 1，其中 0 应与标准圆角像素一致）；四角半径不等加平滑度；两级降级的三种状态并排；平滑度描边的三种对齐方式；用平滑圆角裁剪渐变图以验证 FP 与 GP 轮廓同源；解析路径与 CPU 降级路径的并排对照（关键，验证轮廓重合）；非等比缩放下的降级结果应为拉伸的平滑圆角而非圆角矩形；平滑圆角配`DropShadowStyle` 在扩散 0/10/30 下阴影保持平滑角，且零半径角在扩散后仍为尖角；配 `InnerShadowStyle` 在扩散 0/10 下的表现；负扩散使内腔消失时的退化表现；同一画布上 30 个以上尺寸与半径各异但平滑度相同的形状以端到端验证合批。

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

## 8 不在本次范围内

`DropShadowFilter` 无spread 且拿不到矢量几何；`RoundCorner` 的通用多边形平滑需要任意夹角支持；不同平滑度之间的合批；放宽平滑角描边的 `UseDrawPath` 判据（首版与椭圆保持同样保守）；大面积静态单形状的 CPU 路径专项优化（5.6 节已说明为可选项）。

---

## 9 参考资料

| 内容 | 位置 |
|---|---|
| Ardot 平滑圆角算法 | `Ardot/packages/common/editor/geometry/src/path/utils/corner_builder.cc` |
| Ardot 扩散适用性判定 | `Ardot/packages/common/editor/style/src/effect/utils/effect.cc:114` 及其测试 |
| Figma 填充 shader | 由 `tools/extract_figma_smooth_shader.py` 从 `shader_blocks.txt` 提取 |
| Figma 阴影 shader | `FigmaSource/figma_shaders/ShadowShaders_all.txt` |
| Figma 扩散口径实测 | `tools/measure_figma_shadow_corner.py` |
| 数学论断验证 | `tools/verify_smooth_corner_math.py` |
| 平滑圆角背景文章 | https://www.figma.com/blog/desperately-seeking-squircles/ |
