# 矢量图层渐变填充与描边效果优化

> 状态：方案讨论中
> 分支：`bugfix/jasonrjchen_vector_layer_gradient_stroke`
> 负责人：jasonrjchen / Dom

## 1. 背景

### 1.1 任务目标

矢量图层（`VectorLayer`）的**渐变填充**与**描边效果**在退化几何（任一轴尺寸为 0）以及描边外扩区域上的渲染行为需要对齐 Figma 语义。`Canvas` 层 API 行为对齐 Skia。

### 1.2 演进历史（已合入 main 的 commits）

本次任务是若干轮迭代后剩余问题的收尾，前置工作已经合入 main：

#### commit `999427a` (2026-04-29) — Stroke gradient fit 对齐 SVG / Figma + Rectangle 退化为线段

- `StrokeStyle.cpp`：移除按 `strokeAlign` 与 `LineCap` 把 fit bounds 外扩的 `buildFitShader` / `expandFitForArea` / `expandFitForLine` 共约 95 行代码。改为：
  ```cpp
  paint->shader = wrapShaderWithFit(innerShape->getPath().getBounds());
  ```
  即 fit 区域 = 原始填充几何 bounds，不考虑描边宽度、stroke alignment、dash。这与 SVG / Figma 一致：渐变只在原始几何范围内做颜色过渡，描边外扩区域由 Shader 的 `Clamp` tile mode 自然填充为首/尾色标的纯色。
- `Gradient.cpp::getFitMatrix`：对零宽/高 bounds 用 `1e-3` 兜底保证矩阵可逆。
- `Rectangle.cpp`：当 `width == 0 || height == 0` 时退化为 `moveTo + lineTo` 开放路径（让 `LineCap` 接管端点渲染）。

#### commit `d5e56b70f` (2026-04-30) — Canvas 空 rect 描边对齐 Skia

- `Canvas::drawRect`：rect 为空时不再直接 return：
  - 单边为 0：沿 0 维度 `outset(stroke.width / 2)` 转为普通矩形绘制；
  - 双边为 0 + `Round` cap：绘制为 oval；
  - 双边为 0 + `Square` cap：转为普通矩形绘制；
  - 双边为 0 + `Butt` cap：不绘制。
- `Canvas::drawRRect`：空 rect 时转发到 `drawRect`。
- 该 commit 只改 Canvas 层，`VectorLayer` 路径不受影响。

#### commit `c9ae4c9ce` (2026-05-01) — 回退 Rectangle 退化策略

- `Rectangle.cpp`：**不再**退化为开放线段。改为把 0 维度替换成 `5e-4` 极小值，仍走 `addRoundRect` 闭合矩形。
  - 注释里给出的回退理由：保持闭合 area 形状，让 stroker、trim、dash、inside/outside align 等 area 语义在 0 维度下仍然有定义。
  - `5e-4 > FLOAT_NEARLY_ZERO (1/4096 ≈ 2.44e-4)`：stroker 不会判定退化。
  - 远小于采样网格：视觉上仍读为线/点。
- `Gradient.cpp::getFitMatrix`：`1e-3` 改为 `1/16`（GPU 子像素网格），避免 `1/sx` 过大导致 `tu+tv` 灾难性抵消产生阶梯走样。

### 1.3 当前 main 的状态矩阵

| 场景 | Canvas 直接调用 | VectorLayer（StrokeStyle） |
|---|---|---|
| 0 尺寸 Rectangle + 正常描边宽度 | ✅ Skia 语义（fast path） | ⚠️ 5e-4 兜底，但 LineCap 语义丢失 |
| 矢量几何（Ellipse/Polystar/ShapePath）0 尺寸 + 外描边 | N/A | ❌ 无任何兜底，外描边不可见 |
| 0 尺寸 Rectangle + 渐变 + 旋转矩阵 | N/A | ⚠️ 待复现验证 |
| 非退化几何 + 描边 + 渐变 + 外扩区域 | ✅ | ✅ Clamp 自然延伸（`999427a`） |

### 1.4 剩余问题

#### 问题 A：0 尺寸 Rectangle + 渐变带旋转矩阵

用户复现脚本（伪代码）：
```cpp
auto rect = Rectangle::Make();
rect->setSize({0, 100});  // zero-width
auto gradient = Gradient::MakeLinear(...);
gradient->setMatrix(Matrix::MakeRotate(10, 0.5f, 0.5f));  // rotate around fit-normalized center
gradient->setFitsToGeometry(true);
```

可能的根因：`getFitMatrix` 在 0 维度被 floor 到 `1/16` 后，渐变 `_matrix` 的旋转中心 `(0.5, 0.5)` 是在归一化 fit 坐标系里指定的，叠加 `1/16` 兜底框后，屏幕空间里的实际旋转中心不是原始几何中心，导致渐变方向偏移。

注：用户原话"以实际代码为准"，需要先在当前 main 上跑一个最小复现 demo，确认问题在 commit `c9ae4c9c` 之后是否仍然存在。

#### 问题 B：矢量几何（Ellipse / Polystar / ShapePath）0 尺寸 + 外描边不可见

`c9ae4c9c` 的 `5e-4` 兜底**只在 `Rectangle.cpp` 里**。其他几何类未做任何兜底：

- **Ellipse**：`addOval(rect)`，rect 退化时 oval 退化为 0 面积闭合 contour；
- **Polystar**：用 `outerRadius` 算顶点 + `path->close()`；`outerRadius == 0` 时所有顶点重合成 0 面积闭合 contour；
- **ShapePath**：用户传入的任意 Path，TGFX 不兜底（ShapePath 由用户负责，本方案不处理）。

即使经过 stroker，这些 0 面积闭合 contour 也产生不了可见像素。

### 1.5 关键澄清（来自和用户的对话）

#### Cap 与 Join 的精确语义

| 场景 | Cap 是否生效 | Join 是否生效 |
|---|---|---|
| 实线 + 封闭路径（正常矩形） | ❌ 无开放端点 | ✅ 所有拐角 |
| 实线 + 开放路径 | ✅ 起点 / 终点 | ✅ 中间拐角 |
| Dash + 任意路径 | ✅ 每段 dash 的两头 | ✅ dash 内跨越的拐角 |
| 0 维度退化为线段 | ✅ 两头 | ❌ 无拐角 |
| 0 维度退化为点 | ✅ 端点 | ❌ 无拐角 |

**关键**：Cap / Join 的生效与"路径是否封闭"无关，只与"是否存在开放端点 / 是否存在连续拐角"有关。Dash 模式下封闭路径同样有 Cap。

#### Figma 渲染引擎背景

Figma 不基于 Skia，是自研 WebGL（2025 年 9 月起切到 WebGPU）渲染器。对齐 Figma 不能直接映射 Skia API，需把 Figma 行为当成黑盒规格实现。

从 background2 截图反推，Figma 对退化几何做了 **「area → 开放 line / point」的显式变换**，让 LineCap 接管端点渲染：
- 矩形 W=0, H>0：退化为竖线段 → Cap 决定上下端点形状；
- 矩形 W=H=0：退化为点 → Cap 决定形状（Round=圆，Square=方块，Butt=无）；
- 三角形 / 五角星顶点重合：同样按 Figma 内部退化规则映射为线 / 点。

#### Skia 在退化场景的行为

Skia `SkPathStroker::finishContour(close=true, ...)` 在面对 4 点共点的封闭矩形时，由 `degenerate_vector` 判定为退化向量，最终输出空。即 **Skia 默认不会让 Cap 在退化封闭路径上生效**——这正是当前 TGFX 矢量层 `5e-4` 兜底闭合矩形（`c9ae4c9c`）下，外描边仍然几乎不可见的原因。

### 1.6 术语

| 术语 | 含义 |
|---|---|
| 退化几何 / degenerate geometry | 任一轴尺寸为 0 的几何形状（含 Polystar `outerRadius == 0`） |
| 概念尺寸 / conceptual size | 用户原始声明的尺寸（如 `Rectangle::_size`），不经退化兜底 |
| 概念 fit bounds | 由概念尺寸构成的渐变 fit 矩形，可能宽或高为 0 |
| 退化为线段 | 把退化几何的几何路径映射为单条 `moveTo + lineTo` |
| 退化为点 | 退化几何只剩一个点（W=H=0），映射为单点 |

## 2. 现有实现分析

### 2.1 Rectangle 当前实现（commit c9ae4c9c 之后）

`src/layers/vectors/Rectangle.cpp:67-91`

```cpp
void Rectangle::apply(VectorContext* context) {
  if (_cachedShape == nullptr) {
    constexpr float MinExtent = 5e-4f;
    auto width = std::max(_size.width, MinExtent);
    auto height = std::max(_size.height, MinExtent);
    auto halfWidth = width * 0.5f;
    auto halfHeight = height * 0.5f;
    auto radius = std::min({_roundness, halfWidth, halfHeight});
    auto rect = Rect::MakeXYWH(_position.x - halfWidth, _position.y - halfHeight, width, height);
    Path path;
    path.addRoundRect(rect, radius, radius, _reversed, 2);
    _cachedShape = Shape::MakeFrom(path);
  }
  context->addShape(_cachedShape);
}
```

**问题**：`5e-4` 闭合矩形传给 stroker 后，4 个角的 Join 退化（向量长度过短），Cap 在闭合路径上不生效，外描边几乎完全消失。

### 2.2 Ellipse 当前实现

`src/layers/vectors/Ellipse.cpp:56-70`

```cpp
void Ellipse::apply(VectorContext* context) {
  if (_cachedShape == nullptr) {
    auto rect = Rect::MakeXYWH(...);  // 直接用原始 _size
    Path path;
    path.addOval(rect, _reversed);
    _cachedShape = Shape::MakeFrom(path);
  }
  context->addShape(_cachedShape);
}
```

**问题**：无任何 0 尺寸兜底，`addOval` 在 empty rect 上退化为 0 面积闭合 contour。

### 2.3 Polystar 当前实现

`src/layers/vectors/Polystar.cpp:36-137`

`ConvertStarToPath` / `ConvertPolygonToPath` 都按 `outerRadius`（与 `innerRadius`）算极坐标顶点 + `path->close()`。`outerRadius == 0` 时所有顶点重合于 `(centerX, centerY)`，最终是 0 面积闭合 contour。

### 2.4 StrokeStyle 当前实现

`src/layers/vectors/StrokeStyle.cpp:50-79`

```cpp
std::shared_ptr<Shape> prepareShape(...) override {
  paint->shader = wrapShaderWithFit(innerShape->getPath().getBounds());
  // ... apply pathEffect, then strokeAlign 处理 ...
}
```

`apply` 入口的早退条件：
```cpp
if (_colorSource == nullptr || _stroke.width <= 0.0f || context->geometries.empty()) {
  return;
}
```

**渐变 fit 的来源**：`innerShape->getPath().getBounds()`。当几何退化时（`5e-4` 矩形或 0 面积星形），这个 bounds 也是退化的，传给 `Gradient::getFitMatrix` 后由 `1/16` floor 接管 → 渐变坐标系与原始几何的语义脱节，问题 A 由此产生。

### 2.5 Gradient::getFitMatrix 当前实现

`src/layers/vectors/Gradient.cpp:93-106`

```cpp
Matrix Gradient::getFitMatrix(const Rect& bounds) const {
  constexpr float MinFitScale = 1.0f / 16.0f;
  float sx = std::max(bounds.width(), MinFitScale);
  float sy = std::max(bounds.height(), MinFitScale);
  auto matrix = Matrix::MakeScale(sx, sy);
  matrix.postTranslate(bounds.left, bounds.top);
  return matrix;
}
```

**问题**：用户传入的渐变 `_matrix`（在 fit-normalized 坐标系里的旋转 / 缩放 / 平移）会与 `getFitMatrix` 生成的 `1/16` 兜底矩阵复合。当 bounds 退化时，等价于在一个 `1/16` 像素的小框里做旋转，旋转中心 `(0.5, 0.5)` 落到屏幕上 `1/32` 像素的位置，与原始几何的几何中心不重合。

## 3. 设计原则

1. **退化几何走 Cap 路径**：与 Figma 行为一致，把 0 维度几何显式转换为开放 `line / point` 路径，让 `LineCap` 接管端点形状。
2. **保留 area 语义边界**：dash / trim 在线段上自然有定义，沿用 stroker 既有逻辑；inside-align 在退化几何上无意义，需要明确降级策略。
3. **渐变 fit 用概念尺寸**：渐变坐标系永远基于"未退化前的概念矩形"，与描边几何路径解耦。
4. **集中改动，最小侵入**：每个几何类只做"是否退化 + 概念 bounds"的判定，不在每个类里重复实现兜底矩阵；StrokeStyle / Gradient 单独承接概念 bounds。
5. **ShapePath 由用户负责**：用户传入任意 Path，TGFX 不做退化判定。

## 4. 实现方案

### 4.1 路线选择：路线 3（混合方案）

- **几何类层**：
  - 在 `apply` 阶段判定几何是否退化；
  - 退化时生成开放 `line / point` 路径（沿用 `999427a` 的 `moveTo + lineTo` 思路），让 stroker 走 cap 流程；
  - 同时把"概念 fit bounds"（基于原始 `_size`）通过 Geometry 的扩展字段传递。
- **StrokeStyle / Gradient**：
  - 渐变 fit 优先使用 Geometry 携带的概念 bounds，回退到 `path.getBounds()`；
  - `Gradient::getFitMatrix` 移除 `1/16` floor（因为概念 bounds 不会退化到 0；若用户真的传入 0×0 的概念尺寸，再保留必要的 epsilon 防除零）。
- **退化几何的 inside-align / 修饰策略**：
  - inside-align 降级为 center（参考 Figma：内描边在退化几何上等同于居中）；
  - dash / trim 沿用 stroker 在线段上的既有行为，不特殊处理。

### 4.2 数据流图

```
Rectangle / Ellipse / Polystar
       │
       │  apply()
       ▼
  ┌──────────────────────────┐
  │  判定退化 (W==0 || H==0  │
  │  或 outerRadius == 0)    │
  └──────────────────────────┘
       │
       ├── 非退化：生成正常 area path（addRoundRect / addOval / star+close）
       │
       └── 退化：生成开放 line / point path（moveTo + lineTo / moveTo）
       │
       ▼
  Geometry { shape, conceptualFitBounds }
       │
       ▼
  VectorContext::addShape(shape, conceptualFitBounds)
       │
       ▼
  StrokeStyle::prepareShape
       │
       ▼
  fit bounds = geometry.conceptualFitBounds (优先)
             | innerShape->getPath().getBounds() (fallback)
       │
       ▼
  Gradient::getFitMatrix(fitBounds) → 渐变坐标系（不再依赖退化后的 path）
       │
       ▼
  stroker → LineCap 接管退化端点
```

### 4.3 关键接口设计

#### Geometry 扩展

`src/layers/vectors/Geometry.h`

```cpp
class Geometry {
 public:
  // ... 既有字段 ...

  // 概念 fit bounds：用于渐变 fitsToGeometry 的坐标系。
  // 由几何类设置为"未退化前的概念矩形"，0 维度场景下仍然记录原始尺寸。
  // 为空（!hasValue）时表示无概念尺寸提示，由消费者回退到 path bounds。
  std::optional<Rect> conceptualFitBounds = std::nullopt;
};
```

#### VectorContext 扩展

`src/layers/vectors/VectorContext.h`

为避免破坏既有调用点，新增重载：
```cpp
class VectorContext {
 public:
  // 既有：
  void addShape(std::shared_ptr<Shape> shape);
  void addShape(std::shared_ptr<Shape> shape, const Point& position);

  // 新增：附带概念 fit bounds
  void addShape(std::shared_ptr<Shape> shape, const Rect& conceptualFitBounds);
};
```

#### StrokeStyle::prepareShape 修改

```cpp
std::shared_ptr<Shape> prepareShape(std::shared_ptr<Shape> innerShape, size_t index,
                                    LayerPaint* paint) override {
  Rect fitBounds = geometries[index]->conceptualFitBounds.value_or(
      innerShape->getPath().getBounds());
  paint->shader = wrapShaderWithFit(fitBounds);
  // ... 其余逻辑不变 ...
}
```

#### Gradient::getFitMatrix 简化

```cpp
Matrix Gradient::getFitMatrix(const Rect& bounds) const {
  DEBUG_ASSERT(_fitsToGeometry);
  // 概念 bounds 由几何类保证非空（Rectangle 用原始 _size，即使 W/H=0 仍带位置）。
  // 极小 epsilon 仅用于防止真正 0 宽 / 0 高输入导致除零。
  constexpr float Eps = 1e-6f;
  float sx = std::max(bounds.width(), Eps);
  float sy = std::max(bounds.height(), Eps);
  auto matrix = Matrix::MakeScale(sx, sy);
  matrix.postTranslate(bounds.left, bounds.top);
  return matrix;
}
```

> 注：此处的 epsilon 仅用于矩阵可逆性，由于上游 conceptualFitBounds 直接对应几何中心，旋转中心不再偏移。问题 A 在此层面解决。

### 4.4 各几何类改造

#### 4.4.1 Rectangle

```cpp
void Rectangle::apply(VectorContext* context) {
  if (_cachedShape == nullptr) {
    bool zeroW = _size.width == 0.0f;
    bool zeroH = _size.height == 0.0f;
    auto halfW = _size.width * 0.5f;
    auto halfH = _size.height * 0.5f;
    Path path;
    if (zeroW || zeroH) {
      // 退化：生成开放 line / point path，让 LineCap 接管端点。
      Point p0 = {_position.x - halfW, _position.y - halfH};
      Point p1 = {_position.x + halfW, _position.y + halfH};
      if (_reversed) std::swap(p0, p1);
      path.moveTo(p0);
      if (!(zeroW && zeroH)) {
        path.lineTo(p1);
      }
    } else {
      auto radius = std::min({_roundness, halfW, halfH});
      auto rect = Rect::MakeXYWH(_position.x - halfW, _position.y - halfH,
                                 _size.width, _size.height);
      path.addRoundRect(rect, radius, radius, _reversed, 2);
    }
    _cachedShape = Shape::MakeFrom(path);
  }
  // 概念 fit bounds：基于原始 _size，与退化无关。
  Rect fitBounds = Rect::MakeXYWH(_position.x - _size.width * 0.5f,
                                  _position.y - _size.height * 0.5f,
                                  _size.width, _size.height);
  context->addShape(_cachedShape, fitBounds);
}
```

#### 4.4.2 Ellipse

```cpp
void Ellipse::apply(VectorContext* context) {
  if (_cachedShape == nullptr) {
    bool zeroW = _size.width == 0.0f;
    bool zeroH = _size.height == 0.0f;
    auto halfW = _size.width * 0.5f;
    auto halfH = _size.height * 0.5f;
    Path path;
    if (zeroW || zeroH) {
      // 退化处理：椭圆退化为水平 / 垂直线段或单点
      Point p0 = {_position.x - halfW, _position.y - halfH};
      Point p1 = {_position.x + halfW, _position.y + halfH};
      path.moveTo(p0);
      if (!(zeroW && zeroH)) {
        path.lineTo(p1);
      }
    } else {
      auto rect = Rect::MakeXYWH(_position.x - halfW, _position.y - halfH,
                                 _size.width, _size.height);
      path.addOval(rect, _reversed);
    }
    _cachedShape = Shape::MakeFrom(path);
  }
  Rect fitBounds = Rect::MakeXYWH(_position.x - _size.width * 0.5f,
                                  _position.y - _size.height * 0.5f,
                                  _size.width, _size.height);
  context->addShape(_cachedShape, fitBounds);
}
```

#### 4.4.3 Polystar

退化定义：`_outerRadius == 0`（所有顶点重合）。

```cpp
void Polystar::apply(VectorContext* context) {
  if (_cachedShape == nullptr) {
    if (_outerRadius == 0.0f) {
      // 退化为单点（位于 _position）。
      Path path;
      path.moveTo(_position);
      _cachedShape = Shape::MakeFrom(path);
    } else {
      // 既有逻辑：通过 PolystarPathProvider 懒构造。
      auto pathProvider = std::make_shared<PolystarPathProvider>(...);
      _cachedShape = Shape::MakeFrom(std::move(pathProvider));
    }
  }
  Rect fitBounds = Rect::MakeXYWH(_position.x - _outerRadius, _position.y - _outerRadius,
                                  _outerRadius * 2, _outerRadius * 2);
  context->addShape(_cachedShape, fitBounds);
}
```

> 注：`_outerRadius != 0 && _innerRadius == 0` 时（仅 Star 类型），形状是有意义的（外角刺向中心），不算退化。

#### 4.4.4 ShapePath

按用户约定，**不处理**。ShapePath 不传 `conceptualFitBounds`，StrokeStyle 自动回退到 `path.getBounds()`。

### 4.5 退化几何的 stroke align 降级

在 `StrokeStyle::prepareShape` 中：

```cpp
auto effectiveAlign = strokeAlign;
if (innerShape->getPath().isLine() || isSinglePoint(innerShape->getPath())) {
  // 线段 / 点上 inside-align 无意义，降级为 center。
  if (effectiveAlign == StrokeAlign::Inside) {
    effectiveAlign = StrokeAlign::Center;
  }
}
```

`isSinglePoint` 辅助：`path.countPoints() == 1` 或等价判定（具体 API 待确认 `Path` 是否提供）。

### 4.6 渐变外扩区域取色（已通过 `999427a` 解决，本方案不重复改动）

由于 fit shader 用 `Clamp` tile mode，且 fit bounds = 原始几何 bounds，描边外扩区域的取色自然是首/尾色标的纯色延伸，无需额外代码。本方案的改动只是把"原始几何 bounds"从 `path.getBounds()` 替换为 `conceptualFitBounds`，让退化几何也能正确应用这套规则。

## 5. 测试计划

### 5.1 复现验证（先于实现）

新增最小复现 demo（仅写 .cpp 测试，不入提交）：

1. **问题 A 复现**：Rectangle `setSize({0, 100})` + linear gradient `setMatrix(MakeRotate(10°, 0.5, 0.5))`，截图与 Figma 对比，确认当前 main 是否仍然异常。
2. **问题 B 复现**：Polystar / Ellipse `setSize({0, 100})` + outside stroke，确认外描边在当前 main 完全不可见。

### 5.2 截图测试（VectorLayerTest）

新增用例（`test/src/VectorLayerTest.cpp`）：

| 用例名 | 场景 | 关键断言 |
|---|---|---|
| `Rect_ZeroWidth_OutsideStroke_Cap` | 矩形 W=0 + outside + Round/Square/Butt cap | Cap 形状可见 |
| `Rect_ZeroSize_OutsideStroke_Cap` | 矩形 W=H=0 + outside + 各 cap | Round=圆，Square=方，Butt=空 |
| `Rect_ZeroWidth_RotatedGradient` | 矩形 W=0 + linear gradient + 旋转 matrix | 渐变方向以原始几何中心为参考 |
| `Ellipse_ZeroWidth_OutsideStroke` | 椭圆 W=0 + outside | 退化为线段，cap 生效 |
| `Polystar_ZeroOuterRadius_OutsideStroke` | 五角星 outerRadius=0 + outside | 退化为点，cap 生效 |
| `Rect_NonDegenerate_GradientFit_Unchanged` | 非退化矩形 + 渐变 | baseline 不变（回归保护） |

每个用例同时覆盖 Inside / Center / Outside 三种 align（退化几何的 Inside 视觉等同于 Center）。

### 5.3 单元测试（无截图）

- `Gradient::getFitMatrix` 在概念 bounds 为 0 维度时仍返回可逆矩阵；
- `Geometry::conceptualFitBounds` 默认值为 `std::nullopt`；
- `VectorContext::addShape(shape, bounds)` 正确写入 `conceptualFitBounds`。

### 5.4 回归保护

- 既有 `VectorLayerTest` 全部通过；
- `CanvasTest::DrawEmptyRect_*`（commit `d5e56b70` 引入）保持不变。

## 6. 实现步骤

| 步骤 | 文件 | 描述 |
|---|---|---|
| 1 | `test/src/VectorLayerTest.cpp` | 写问题 A / B 复现用例（不入提交，仅本地确认现状） |
| 2 | `src/layers/vectors/Geometry.h` | 新增 `conceptualFitBounds` 字段（`std::optional<Rect>`） |
| 3 | `src/layers/vectors/VectorContext.h/.cpp` | 新增 `addShape(shape, conceptualFitBounds)` 重载 |
| 4 | `src/layers/vectors/Rectangle.cpp` | 退化时生成 line / point 路径；传 conceptualFitBounds |
| 5 | `src/layers/vectors/Ellipse.cpp` | 同上 |
| 6 | `src/layers/vectors/Polystar.cpp` | `outerRadius == 0` 退化为点；传 conceptualFitBounds |
| 7 | `src/layers/vectors/StrokeStyle.cpp` | `prepareShape` 优先用 conceptualFitBounds；inside-align 在退化几何上降级为 center |
| 8 | `src/layers/vectors/Gradient.cpp` | `getFitMatrix` 把 floor 从 `1/16` 改为 `1e-6`（仅防除零） |
| 9 | `test/src/VectorLayerTest.cpp` | 新增 5.2 节的截图测试用例 |
| 10 | 编译验证 | `cmake --build cmake-build-debug --target TGFXFullTest` |
| 11 | 用户执行 `/accept-baseline` | 接受新截图基准（不可由 Agent 操作） |

## 7. 风险与未决事项

### 7.1 风险

1. **Polystar 的退化语义**：本方案只把 `outerRadius == 0` 视为退化，未处理 `pointCount <= 1` 等更边缘情况。`pointCount == 0` 在 `ConvertStarToPath` / `ConvertPolygonToPath` 入口已 return，安全。
2. **stroker 在 line / point 路径上的实际行为**：需在实现阶段验证 `Path::moveTo` 单点路径能否正确生成 Round / Square cap 几何。如果 stroker 对单点路径返回空，需要在几何类层提前对 W=H=0 的情况做"用 `lineTo(p0)` 制造零长度段"的处理（Skia 习惯用法）。
3. **Path 是否提供 `isLine()` / `countPoints() == 1` 判定**：需在实现阶段确认 TGFX `Path` API。如果不存在，可在 Geometry 上加个标记字段 `isDegenerate: bool` 由几何类设置，避免在 StrokeStyle 里重新判定。

### 7.2 未决事项

1. 问题 A 在当前 main 上是否真的还存在，需复现确认（步骤 1）。如果 commit `c9ae4c9c` 已经修了这个问题，本方案的"概念 fit bounds"机制仍然有价值（让退化几何的渐变坐标系更稳定），但优先级可降为 nice-to-have。
2. dash 在退化线段上的端点表现需要专门验证（Figma 截图中 dash + cap 行为是关键参考）。

## 8. 参考资料

- 已合入 commits：`999427a`、`d5e56b70f`、`c9ae4c9ce`
- 用户提供的 Figma 行为参考截图：`/Users/chenjie/Desktop/Work/项目/2026/0506 矢量图层渐变填充和描边效果优化/background1.png`、`background2.png`、`/Users/chenjie/Desktop/test.png`
- Figma 渲染引擎背景：https://www.figma.com/blog/figma-rendering-powered-by-webgpu/
- Skia stroker 退化处理：`SkPathStroker::finishContour` / `degenerate_vector`（`/Users/chenjie/Desktop/Develop/skia/src/core/SkStroke.cpp`）
- 相关源码路径：
  - `src/layers/vectors/StrokeStyle.cpp`
  - `src/layers/vectors/Gradient.cpp`
  - `src/layers/vectors/Rectangle.cpp`
  - `src/layers/vectors/Ellipse.cpp`
  - `src/layers/vectors/Polystar.cpp`
  - `src/layers/vectors/ShapePath.cpp`
  - `src/layers/vectors/Geometry.h`
  - `src/layers/vectors/VectorContext.h`
