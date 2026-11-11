# 图层变形

## 1 背景

设计工具需要为文字、矩形和矢量图形提供统一的变形能力：用户可以拖动对象四角产生透视与斜切，也可以套用弯曲、扇形、流动等预设模板，并且变形后的对象仍保留原始内容的可编辑性。需求来源于变形工具交互方案（透视 / 斜切统一入口 + 变形预设模板），交互原型为一个 HTML Demo。本文档描述 tgfx 为该需求提供的渲染能力，编辑器侧的手柄、参数面板与撤销重做由调用方实现，本文只给出调用方式。

需求：

1. 图层可以按四个角的目标位置做透视变形，四角可以任意移动，包括只平移一条边形成斜切。
2. 图层可以应用高度拉伸、宽度拉伸、弯曲、扇形、高度挤压、流动、透视七种预设变形，并连续调整模板参数。
3. 预设变形作用于图层内容、子图层与图层样式；图层滤镜效果（如投影）在变形之前还是之后生效，由调用方决定。
4. 四角透视与预设变形可以同时作用于同一个图层，效果与 Demo 一致：先做预设变形，再做四角透视。
5. 修改文字内容或矢量路径后，图层的变形设置保持不变并作用于新内容。
6. 画布缩放后，变形结果按新的显示倍率重新绘制，保持清晰。
7. 调用方可以查询图层局部坐标中任意一点变形后的位置，用于绘制变形后的轮廓和判断点击。

## 2 现状

### 2.1 透视变换

图层的变换矩阵保存在 `Layer::_matrix3D` 中。`Layer::setMatrix()` 把 3×3 矩阵转成 4×4 矩阵保存（`src/layers/Layer.cpp:323`），`setMatrix3D()` 直接保存 4×4 矩阵（`src/layers/Layer.cpp:333`）。非 preserve3D 的子图层绘制时，`Layer::drawChild()` 把矩阵转换回 3×3 矩阵后 `concat` 到画布（`src/layers/Layer.cpp:1798`），因此带透视分量的矩阵会让图层内容直接在透视矩阵下绘制，矢量与文字不经过离屏光栅化。

四角透视所需的渲染能力已经存在，缺少两部分：

- 没有"把矩形映射到任意四边形"的矩阵计算接口，`Matrix` 只提供平移、缩放、旋转、斜切与逐项设置（`include/tgfx/core/Matrix.h:57` 起）。
- `setMatrix()` 收到带透视的矩阵时仍把 `bitFields.matrix3DIsAffine` 设为 true（`src/layers/Layer.cpp:329`）。该标志为 true 时，`matrix()` 只返回矩阵的仿射部分（`src/layers/Layer.cpp:312`），`setPosition()` 直接改写平移项（`src/layers/Layer.cpp:295`）。透视矩阵的平移项与"把图层原点移到指定位置"并不等价，所以对透视图层调用 `setPosition()` 会改变透视形状。

### 2.2 非线性变形

tgfx 没有非线性变形能力。可以承载它的现有机制是图层滤镜链路与 Mesh 绘制。

**滤镜链路：** `Layer::drawLayer()` 在图层带滤镜时走离屏绘制（`src/layers/Layer.cpp:1185`）。`OffscreenRenderer::RenderContent()` 先把图层内容、子图层与图层样式按各向同性缩放 `density = MakeScale(contentScale)` 绘制成输入图像（`src/layers/OffscreenRenderer.cpp:59`），其中 `contentScale` 取画布矩阵的最大缩放；再由 `Layer::applyFilters()` 依次调用每个滤镜的 `LayerFilter::filterImage()`，每个滤镜的输出作为下一个滤镜的输入，输出图像相对输入图像的偏移逐个累加（`src/layers/Layer.cpp:1111`）。最终图像由 `drawOffscreen()` 按画布矩阵绘制（`src/layers/Layer.cpp:1570`）。因此滤镜作用在图层局部坐标系中，先于图层矩阵生效。

子树缓存是第二个应用滤镜的入口：`Layer::createSubtreeCacheImage()` 把子树按 `contentScale` 录制为图像后同样调用 `applyFilters()`（`src/layers/Layer.cpp:1458`、`:1483`）。

`LayerFilter` 的接口（`include/tgfx/layers/filters/LayerFilter.h:50`、`:62`）：

```cpp
std::shared_ptr<Image> filterImage(std::shared_ptr<Image> input, float scale,
                                   const Rect& contentBounds, Point* offset = nullptr);
virtual Rect filterBounds(const Rect& srcRect, float contentScale,
                          MapDirection direction = MapDirection::Forward);
```

- `scale` 是输入图像相对图层局部坐标的缩放，即上文的 `contentScale`。
- `contentBounds` 为 NoiseFilter 的噪声锚点设计：两个入口在计算时都把内容在图层局部坐标中的左上角偏移减掉了（`src/layers/OffscreenRenderer.cpp:68`、`src/layers/Layer.cpp:1479`），只保证中心点相对内容稳定，不能用来确定图层局部坐标中某一点落在输入图像的哪个像素上。
- 只有 `LayerImageFilter`（`src/layers/filters/LayerImageFilter.cpp:24`）与 `NoiseFilter`（`src/layers/filters/NoiseFilter.cpp:139`）实现 `onFilterImage()`，其余滤镜都继承 `LayerImageFilter`。

`filterBounds()` 有三类调用：`Layer::computeBounds()` 用 Forward 方向计算图层包围盒（`src/layers/Layer.cpp:788`）；`computeContentBounds()` 与子树缓存用 Reverse 方向把裁剪区域反推为需要绘制的输入区域，以免光栅化屏幕外的内容（`src/layers/Layer.cpp:1583`、`:1459`）；`FilterRegionTransformer` 用 Forward 方向把内容脏区映射为输出脏区（`src/layers/RegionTransformer.cpp:54`）。

**Mesh 绘制：** `Mesh::MakeCopy()` 接受顶点位置、像素坐标系的纹理坐标与 16 位索引，使用索引时顶点数不超过 65536（`include/tgfx/core/Mesh.h:66`）。`Canvas::drawMesh()` 在画笔带着色器时用纹理坐标采样着色器（`src/core/Canvas.cpp:531`），`RenderContext` 同样实现了 `drawMesh()`。

**ImageFilter 离屏机制：** `FilterImage::MakeFrom()` 用滤镜的 Forward 边界确定输出图像范围与偏移（`src/core/images/FilterImage.cpp:27`），GPU 侧在 `lockTextureProxy()` 中把结果画进新的渲染目标（`src/core/ImageFilter.cpp:39`）。`PictureImage::drawPicture()` 展示了在 `lockTextureProxy()` 中用 `RenderContext` 往 `RenderTargetProxy` 上绘制任意内容的写法（`src/core/images/PictureImage.cpp:162`）。

### 2.3 光栅化倍率

离屏输入图像的倍率 `contentScale` 由 `Matrix::getMaxScale()` 计算，该函数只使用矩阵左上角 2×2 部分（`src/core/Matrix.cpp:544`），不考虑透视分量。画布缩放变化时 `contentScale` 随之变化，滤镜按新倍率重新执行（`LayerImageFilter` 按倍率缓存生成的 ImageFilter，`src/layers/filters/LayerImageFilter.cpp:42`）。输入图像在变形中被放大的区域，像素密度会低于屏幕密度。

### 2.4 点击测试与轮廓

`Layer::hitTestPoint()` 把全局坐标转换到图层局部坐标后测试内容形状，不考虑滤镜（`src/layers/Layer.cpp:837`）。滤镜接口也没有映射单个点的方法，调用方无法得到变形后的轮廓。

## 3 参考设计

### 3.1 HTML Demo

Demo 是单文件交互原型（`transform-tool-demo.html`），全部变形在 JavaScript 中计算，用 Canvas 2D 逐三角形绘制。

**数据结构：** 每个对象保存 `x, y, w, h`（对象框），`deltas` 保存左上、右上、右下、左下四个角相对对象框角点的世界坐标偏移，`warp.type` 与 `warp.params` 保存预设类型与各类型的参数。参数在界面上取 -100～100 的整数，计算时除以 100。

**四角投影：** `quadOf()` 把对象框四角加上 `deltas` 得到目标四边形 q（`:777`）。`homo(q)` 求单位正方形到 q 的单应映射（homography，即带透视除法的 3×3 矩阵映射）（`:781`）：

```text
dx1 = x1 − x2,  dx2 = x3 − x2,  dx3 = x0 − x1 + x2 − x3（dy1、dy2、dy3 同理）
若 dx3、dy3 都为零：g = h = 0
否则：det = dx1·dy2 − dx2·dy1
      g = (dx3·dy2 − dx2·dy3) / det
      h = (dx1·dy3 − dx3·dy1) / det
a = x1 − x0 + g·x1,  b = x3 − x0 + h·x3,  c = x0
d = y1 − y0 + g·y1,  e = y3 − y0 + h·y3,  f = y0
(u, v) → ((a·u + b·v + c) / w, (d·u + e·v + f) / w)，w = g·u + h·v + 1
```

其中 (x0, y0)～(x3, y3) 依次是 q 的左上、右上、右下、左下角。拖动边中点时，Demo 把鼠标位移投影到原始边方向后同时加到该边两个端点上，形成斜切；按住 Option 时不做投影。`isConvex()` 用相邻边叉积拒绝非凸或接近退化的四边形（`:834`）。

**预设映射：** `warpFn()` 返回对象框归一化坐标 (u, v) ∈ [0, 1]² 上的映射 Wp（`:793`）。记 s 为强度参数，ar = w / h：

| 模板 | 映射 |
|---|---|
| 高度拉伸 | u′ = u，v′ = 0.5 + (v − 0.5)·(1 + s) |
| 宽度拉伸 | u′ = 0.5 + (u − 0.5)·(1 + s)，v′ = v |
| 弯曲 | f = 1 − (2u − 1)²；u′ = u，v′ = v − 0.22·s·ar·f |
| 高度挤压 | f = 1 − (2u − 1)²；u′ = u，v′ = 0.5 + (v − 0.5)·(1 − 0.62·s·f) |
| 流动 | u′ = u，v′ = v + 0.075·s·ar·sin(2πu) |
| 扇形 | A = s·π，R = max(w / \|A\|, 1.45·h)；A > 0 时 θ = (u − 0.5)·A，r = R − v·h，u′ = (w/2 + r·sinθ) / w，v′ = (R − r·cosθ) / h；A < 0 时 θ = (u − 0.5)·(−A)，r = R + v·h，u′ = (w/2 + r·sinθ) / w，v′ = (r·cosθ − R) / h；\|A\| < 0.001 时不变形 |
| 透视 | δ = min(0.92, 1.3·\|s\|)，m = 1 + t（t 为"高度"参数）；s ≥ 0 时 q = [(0,0), (1,δ/2), (1,1−δ/2), (0,1)]，否则 q = [(0,δ/2), (1,0), (1,1), (0,1−δ/2)]；(a, b) = homo(q)(u, v)，u′ = a，v′ = 0.5 + (b − 0.5)·m |

弯曲与高度挤压中的 f 在对象框左右两端为 0、中间为 1。各模板的默认参数（强度 30～50，扇形角度 39）只是界面初始值。

**合成与网格：** `computeGeom()` 在 41 × 17 个网格顶点上计算 Hq(Wp(i/40, j/16))，Hq 为 `homo(quadOf(o))`，即先预设映射、后四角投影（`:818`、`:820`）。每个网格单元拆成两个三角形，`tri()` 由三对纹理坐标与屏幕坐标解出仿射系数，裁剪到三角形后绘制纹理（`:929`）；三角形从重心向外扩 0.7 像素以遮住相邻三角形之间的缝隙。网格外圈顶点组成的轮廓 `boundary` 同时用于绘制绿色轮廓和点击判断（`:1464`）。

**纹理：** `textureOf()` 把对象框范围内的内容画到离屏 Canvas，倍率为 min(4, 设备像素比 × 画布缩放 × 1.6)，边长超过 4096 时按比例缩小（`:918`）。投影与模糊效果在变形绘制完成后才施加（`drawObj()`），所以投影跟随变形后的外形，但投影偏移本身不被弯曲。

### 3.2 CSS Transform

CSS Transforms Level 2 的 `matrix3d()` 接受 4×4 齐次矩阵，映射点后除以 w 回到二维坐标，可以表达任意四角投影；Chromium 中该路径为 `TransformOperations::Apply()` 合成 `gfx::Transform`，`gfx::Transform::MapPointInternal()` 在映射点时做 w 除法（`ui/gfx/geometry/transform.cc:1022`）。CSS 不提供四角拖动交互，应用需要自己由四角解出矩阵；一个元素只有一个矩阵，无法表达弯曲、流动、扇形这类随位置变化的映射。

## 4 对比分析

| 实现 | 四角透视 | 非线性预设 | 合成顺序 | 内容光栅化倍率 | 网格 | 网格边缘处理 | 变形后轮廓与点击 |
|---|---|---|---|---|---|---|---|
| HTML Demo | 单应映射烘焙进网格顶点 | ✅ | 先预设后投影，烘焙在同一网格 | 设备倍率 × 1.6<br>（上限 4 倍、边长 4096） | 固定 40 × 16 | 三角形外扩 0.7 像素 | 网格外圈多边形 |
| CSS / Chromium | matrix3d 矩阵 | ❌ | — | 浏览器决定 | 无 | — | 浏览器按矩阵处理 |
| tgfx 现状 | 图层透视矩阵，矢量直接绘制<br>（缺少四角求矩阵接口） | ❌ | 滤镜在图层局部坐标中先于图层矩阵生效 | 画布矩阵 2×2 部分的最大缩放 | 无 | — | 不考虑滤镜 |

## 5 实现方案

### 5.1 总体设计

四角透视用图层矩阵表达，预设变形用新增的图层滤镜表达：

- 新增 `Matrix::setRectToQuad()`，调用方用它由参考矩形与四角目标位置求出透视矩阵，再通过 `Layer::setMatrix()` 设置到图层。没有预设变形时图层不走离屏，矢量与文字直接在透视矩阵下绘制。
- 新增图层滤镜 `WarpFilter`，保存预设类型、参数与参考矩形。它在 `onFilterImage()` 中生成覆盖整张输入图像的网格，交给新增的 `ImageFilter::MeshWarp()` 按网格重新绘制输入图像。
- 滤镜在图层局部坐标中先于图层矩阵生效（2.2 已说明），所以"预设滤镜 + 透视矩阵"的组合天然满足"先预设后投影"。两者使用同一个参考矩形，与 Demo 中 Wp 和 Hq 都定义在对象框上一致。

```text
图层内容 + 子图层 + 图层样式
  → 离屏输入图像（倍率 contentScale × k）
  → WarpFilter 之前的滤镜
  → WarpFilter：生成网格 → ImageFilter::MeshWarp 绘制
  → WarpFilter 之后的滤镜
  → drawOffscreen 按图层矩阵（含四角透视）绘制到画布
```

k 是滤镜请求的输入倍率放大系数，变形放大输入图像的局部区域时 k 大于 1。WarpFilter 的输入是滤镜列表中排在它前面的所有结果：把它放在滤镜列表首位时，投影等滤镜在变形后的外形上计算、自身不被弯曲，与 Demo 的效果一致；放在末位时，滤镜效果连同内容一起被弯曲。图层样式总在滤镜之前生效，因此总是被变形。

### 5.2 四角透视

**接口：**

```cpp
class Matrix {
 public:
  /**
   * Sets the Matrix to map the four corners of src to the four points of dst. The dst points are
   * ordered as the images of the top-left, top-right, bottom-right, and bottom-left corners of
   * src. Returns false and leaves the Matrix unchanged if src is empty or the resulting mapping is
   * not invertible.
   */
  bool setRectToQuad(const Rect& src, const Point dst[4]);
};
```

**计算：** 记 src = (L, T, W, H)。先把 src 映射到单位正方形，再用 3.1 中 `homo()` 的公式把单位正方形映射到 dst：

```text
S = Scale(1/W, 1/H) · Translate(−L, −T)          // src → 单位正方形
Q = | a  b  c |                                  // 单位正方形 → dst，a～h 按 3.1 的 homo() 公式
    | d  e  f |
    | g  h  1 |
M = Q · S
```

`homo()` 中 g、h 的方程组来自"(1, 1) 映射到 (x2, y2)"这一条件：

```text
x2 = (a + b + c) / (g + h + 1)                     // 代入 u = v = 1
x2·(g + h + 1) = x1 + x3 − x0 + g·x1 + h·x3        // 代入 a、b、c 的定义
g·(x1 − x2) + h·(x3 − x2) = x0 − x1 + x2 − x3       // 移项
g·dx1 + h·dx2 = dx3                                // 代入 dx 的定义
```

y 方向同理得到 g·dy1 + h·dy2 = dy3，用克拉默法则解出 g、h；另外三个角代入 a～f 的定义可直接验证。det 为零或 M 不可逆时返回 false。四边形是否为凸由调用方检查（Demo 的 `isConvex()`）：非凸四边形对应的映射会让单位正方形跨过 w = 0 的直线，`setRectToQuad()` 只保证返回的矩阵可逆，不拒绝这类输入。

**setMatrix() 修正：** `Layer::setMatrix()` 改为 `bitFields.matrix3DIsAffine = !value.hasPerspective()`，使带透视的矩阵与经 `setMatrix3D()` 设置时行为一致：`matrix()` 按现有注释返回单位矩阵，`position()` 与 `setPosition()` 走映射原点的分支。`matrix()` 的注释同步说明"通过 setMatrix 或 setMatrix3D 设置了透视矩阵时返回单位矩阵"。

### 5.3 网格变形 ImageFilter

**接口：**

```cpp
class ImageFilter {
 public:
  /**
   * Creates a filter that redraws the input image through the given mesh. Each vertex samples the
   * input image at its texture coordinate and is placed at its position. Both positions and texture
   * coordinates are in the pixel space of the input image. Areas outside the input image are
   * sampled as transparent. Returns nullptr if the mesh is nullptr or has no texture coordinates.
   */
  static std::shared_ptr<ImageFilter> MeshWarp(std::shared_ptr<Mesh> mesh);
};
```

`ImageFilter::Type` 增加 `MeshWarp`。实现类 `MeshWarpImageFilter` 位于 `src/core/filters/`：

- `onFilterBounds()`：Forward 返回 `mesh->bounds()`，与输入矩形无关，输出图像的范围与偏移由此决定；Reverse 返回全部纹理坐标的包围盒。
- `lockTextureProxy()`：按 `renderBounds`（存在 `drawScale < 1` 时按其缩小）创建 `RenderTargetProxy`，用 `RenderContext` 绘制网格后返回纹理，写法同 `PictureImage::drawPicture()`。

```cpp
RenderContext renderContext(renderTarget, args.renderFlags, true, nullptr, source->colorSpace());
auto matrix = Matrix::MakeScale(textureScaleX, textureScaleY);
matrix.preTranslate(-renderBounds.left, -renderBounds.top);
Brush brush = {};
brush.shader = Shader::MakeImageShader(source, TileMode::Decal, TileMode::Decal,
                                       SamplingOptions(FilterMode::Linear, MipmapMode::None));
renderContext.drawMesh(mesh, matrix, {}, brush);
renderContext.flush();
```

- `asFragmentProcessor()`：调用 `makeFPFromTextureProxy()`，与 `RuntimeImageFilter` 相同。

网格三角形本身不做边缘抗锯齿。调用方让网格覆盖输入图像外至少 1 像素的透明区域，Decal 采样加双线性插值使内容边缘在变形后仍然平滑；相邻三角形共享顶点，不存在 Demo 中逐三角形裁剪产生的缝隙。

### 5.4 LayerFilter 接口改造

**输入原点：** `filterImage()` 与 `onFilterImage()` 在 `contentBounds` 之后增加参数 `const Point& inputOrigin`，含义为"输入图像像素 (0, 0) 在图层局部坐标 × scale 坐标系中的位置"。滤镜借助它把图层局部坐标中的几何定位到输入像素上，2.2 已说明 `contentBounds` 做不到这一点。两个入口的取值：

- `OffscreenRenderer::RenderContent()`：带滤镜时 density 为各向同性缩放，`inputOrigin = result.imageMatrix.mapXY(0, 0) × density.getMaxScale()`，该式同时适用于 Surface 与 Picture 两条录制路径。
- `Layer::createSubtreeCacheImage()`：`ToImageWithOffset()` 返回的 `offset` 已经在缩放后的坐标系中，直接作为 `inputOrigin`。

`Layer::applyFilters()` 增加同名参数，每个滤镜执行后把 `filterOffset` 累加到 `inputOrigin` 上再传给下一个滤镜，与现有 `offset` 的累加方式相同。需要同步修改签名的只有 `LayerFilter`、`LayerImageFilter`、`NoiseFilter` 三处，后两者忽略该参数。

**输入倍率：** `LayerFilter` 增加受保护的虚函数 `virtual float inputScaleFactor() const`，默认返回 1，`Layer` 声明为友元。`Layer` 增加私有方法，取所有滤镜 `inputScaleFactor()` 的最大值 k。`OffscreenRenderer::RenderContent()` 与 `Layer::createSubtreeCacheImage()` 都把 density 乘以 k，其他滤镜收到的 `scale` 同样包含 k，模糊半径等尺寸参数随之换算；最终绘制矩阵已经包含 density 的逆，放大不改变图层在画布上的大小。

### 5.5 预设变形滤镜

**接口：**

```cpp
enum class WarpType { HeightStretch, WidthStretch, Bend, Fan, HeightSqueeze, Flow, Perspective };

/**
 * A filter that deforms its input with a preset warp defined on a reference rectangle in the
 * layer's local coordinates. The input includes the layer content, children, layer styles, and
 * all filters placed before it.
 */
class WarpFilter : public LayerFilter {
 public:
  /**
   * Creates a WarpFilter. Returns nullptr if referenceRect is empty.
   * @param warpType The preset warp to apply.
   * @param referenceRect The rectangle in layer-local coordinates that the warp is defined on.
   * @param strength The warp strength. Values are clamped to [-1, 1].
   * @param heightStrength The height adjustment used by WarpType::Perspective. Values are clamped
   * to [-1, 1].
   */
  static std::shared_ptr<WarpFilter> Make(WarpType warpType, const Rect& referenceRect,
                                          float strength, float heightStrength);

  WarpType warpType() const;
  void setWarpType(WarpType value);

  Rect referenceRect() const;
  void setReferenceRect(const Rect& value);

  float strength() const;
  void setStrength(float value);

  float heightStrength() const;
  void setHeightStrength(float value);

  /**
   * Returns the position of the given layer-local point after the warp.
   */
  Point mapPoint(const Point& point) const;

  Rect filterBounds(const Rect& srcRect, float contentScale, MapDirection direction) override;

 protected:
  Type type() const override;

  std::shared_ptr<Image> onFilterImage(std::shared_ptr<Image> input, float scale,
                                       const Rect& contentBounds, const Point& inputOrigin,
                                       Point* offset) override;

  float inputScaleFactor() const override;
};
```

`LayerFilter::Type` 增加 `WarpFilter`。`inputOrigin` 参数与 `inputScaleFactor()` 由 5.4 定义。每个 setter 在值变化时调用 `invalidateFilter()` 触发重绘。

**参考矩形：** 记参考矩形 R = (L, T, W, H)，取值为调用方的对象框，例如文字框或形状框，与 5.2 中 `setRectToQuad()` 的 src 相同。参考矩形由调用方显式给出而不是取图层内容包围盒，原因是图层样式与前序滤镜会扩大输入范围，而预设形状只应由对象框决定。修改文字或路径后对象框变化时，调用方同步调用 `setReferenceRect()`，满足需求 5。

**映射定义：** 对图层局部坐标中的点 p：

```text
c = clamp(p, R)                                   // p 在 R 内的最近点；p 在 R 内时 c = p
(u, v) = ((c.x − L) / W, (c.y − T) / H)           // R 的归一化坐标
(u′, v′) = Wp(u, v)                               // 3.1 的预设映射，s = strength，t = heightStrength
D(c) = (L + u′·W − c.x, T + v′·H − c.y)           // c 点的位移
p′ = p + D(c)
```

R 内的点按 Demo 的公式变形。R 外的点（来自图层样式、前序滤镜或超出对象框的描边）跟随其在 R 上的最近点整体平移：位移在 R 的边界上连续，所以变形结果在边界上没有断裂；Demo 的公式在 R 外直接外推会遇到奇异点，例如透视模板 s ≥ 0 时 homo(q) 解出 g = δ/(1 − δ)、h = 0，δ = 0.92 时齐次分母 w = 1 + g·u 在 u ≈ −0.087 处为零，R 左侧约 9% 宽度处的点会被映射到无穷远。`mapPoint()` 按上式解析计算。

strength 为 0 时七种模板都是恒等映射，Perspective 还要求 heightStrength 为 0；此时 `onFilterImage()` 原样返回输入，`filterBounds()` 返回 srcRect。滤镜存在本身会使图层走离屏，调用方在"无变形"状态下应从滤镜列表移除 WarpFilter，以保持矢量直接绘制。

### 5.6 网格生成

`onFilterImage()` 在输入图像的像素坐标系中生成网格。记 sc 为参数 `scale`，O 为 `inputOrigin`，像素坐标 g 与图层局部坐标 p 的换算为 p = (g + O) / sc。

**网格线：** 网格线位于"R 的等分线"与"R 外的段端点"上，使每个顶点的位移都等于 R 上某个等分点的位移：

```text
nx = HasAffineWarp ? 1 : min(NextPowerOfTwo(ceil(W·sc / CellSize)), MaxInteriorCells)
ny = HasAffineWarp ? 1 : min(NextPowerOfTwo(ceil(H·sc / CellSize)), MaxInteriorCells)
x 网格线 = { R 左右边之间的 nx 等分线 } ∪ { 网格范围左边界, 网格范围右边界 }（落在 R 外时）
y 网格线 同理
```

- 等分数取 2 的幂，使任意等分数下的网格交点都是 MaxInteriorCells 等分网格交点的子集。
- 网格范围取输入图像外扩 1 像素后的矩形，再向外对齐到最近的 R 等分线，使分块渲染（tiled rendering）时不同分块对同一位置生成相同的顶点。对齐后多出来的区域位于输入图像之外，按 Decal 采样为透明；输入图像由 Reverse 方向的边界裁出（2.2），已包含所有变形后落入裁剪范围的点，所以这些区域变形后不会落入裁剪范围。
- R 外的区域在法线方向上是整体平移，一段即可；在切线方向上与 R 的边界共用等分线。
- `HasAffineWarp` 对高度拉伸与宽度拉伸成立，二者是线性映射，一个单元即可精确表达。
- CellSize 为单元边长上限，首版取 8 像素，借用 Demo 默认对象尺寸下的网格密度（334 像素宽的矩形分 40 列，约 8.4 像素一列），最终取值由弯曲、扇形、流动在不同缩放下的截图对照决定。取 2 的幂后实际单元边长在 CellSize 的一半到 CellSize 之间。
- MaxInteriorCells 取 128：加上 R 外两段，每个方向最多 130 个单元，顶点数不超过 131 × 131 = 17161，低于 16 位索引的 65536 上限；下一档 256 加两段后为 259 × 259，超过上限。R 在输入像素中的边长超过 128 × CellSize（1024 像素）时，单元边长随之超过 CellSize。

**顶点：** 对每个网格交点 g：

```text
texCoord = g
p        = (g + O) / sc
position = (p + D(clamp(p, R)))·sc − O
```

每个单元按 Demo 的拓扑拆成两个三角形，用 `Mesh::MakeCopy(MeshTopology::Triangles, ...)` 构造带索引的网格，再调用 `FilterImage::MakeFrom(input, ImageFilter::MeshWarp(mesh), offset)` 得到输出。网格由 (sc, O, 输入图像尺寸, 参数) 唯一确定，WarpFilter 缓存最近一次的网格，这些值不变时直接复用；Mesh 存活期间其 GPU 顶点缓冲也被缓存。

透视模板的映射是单应映射，网格单元内按三角形做仿射插值会引入误差，与非线性模板一样由网格密度控制。

### 5.7 边界映射

`filterBounds()` 用位移极值计算边界。记 Dx_min、Dx_max、Dy_min、Dy_max 为 R 的 MaxInteriorCells × MaxInteriorCells 等分网格上所有交点位移 D 的分量极值（图层局部单位），随参数缓存。这组极值对任何缩放下实际绘制的网格都成立：

```text
实际网格每个顶点的位移 = R 上某个等分交点的位移      // 5.6 的网格线规则
                       ∈ MaxInteriorCells 等分网格的交点位移  // 等分数为 2 的幂，交点是其子集
三角形内部的位移 = 三个顶点位移的凸组合               // 位置与纹理坐标都按重心坐标线性插值
              ∈ [Dx_min, Dx_max] × [Dy_min, Dy_max]
```

```text
Forward(rect) = (rect.left + Dx_min·sc, rect.top + Dy_min·sc,
                 rect.right + Dx_max·sc, rect.bottom + Dy_max·sc) 外扩 1 像素
Reverse(rect) = (rect.left − Dx_max·sc, rect.top − Dy_max·sc,
                 rect.right − Dx_min·sc, rect.bottom − Dy_min·sc) 外扩 1 像素
```

其中 sc 为参数 `contentScale`，rect 位于"图层局部坐标 × sc"坐标系。Forward 外扩 1 像素对应网格多覆盖的透明边，Reverse 外扩 1 像素对应双线性采样读取的相邻像素。两者都是包含真实范围的保守结果：Forward 用于包围盒与脏区，偏大只会多重绘；Reverse 用于裁剪输入，偏大只会多光栅化。

### 5.8 光栅化倍率

变形会放大输入图像的局部区域，例如高度拉伸 s = 1 时纵向放大 2 倍，按 `contentScale` 光栅化的输入在该方向只有屏幕像素密度的一半。WarpFilter 的 `inputScaleFactor()` 返回：

```text
σ(tri) = 三角形从纹理坐标到位置的仿射映射的线性部分（2×2）的最大奇异值
k = clamp(max over tri σ(tri), 1, MaxInputScaleFactor)
```

σ 在图层局部单位下计算，与 sc 无关，在 5.7 的 MaxInteriorCells 等分网格上逐三角形求得并随参数缓存。MaxInputScaleFactor 首版取 2：拉伸模板在参数上限处恰好需要 2 倍；输入图像内存随 k² 增长，弯曲、扇形在极端参数下是否需要更高倍率，由截图对照决定。k 经 5.4 的两个离屏入口生效。

图层矩阵带透视时，`contentScale` 仍按 2.3 的方式只取 2×2 部分，透视近端放大的区域在离屏后会偏模糊。这是所有带滤镜的透视图层的共同现状，不只出现在变形图层上，单独处理；只有四角透视、没有预设变形的图层不走离屏，不受影响。

### 5.9 图层集成与调用方式

**包围盒、脏区与缓存：** WarpFilter 通过 `filterBounds()` 进入 `computeBounds()`、`computeContentBounds()` 与 `FilterRegionTransformer` 的现有调用，不需要额外改动。参数变化经 `invalidateFilter()` 触发 `invalidateTransform()`，与其他滤镜一致。

**preserve3D：** WarpFilter 与其他滤镜一样使图层的 preserve3D 按 false 处理（`include/tgfx/layers/Layer.h:205` 的注释）。

**点击测试：** `Layer::hitTestPoint()` 维持不考虑滤镜的语义。调用方沿参考矩形边界采样若干点，经 `WarpFilter::mapPoint()` 与 `Layer::localToGlobal()` 得到变形后的轮廓多边形，用它绘制轮廓并判断点击，与 Demo 用网格外圈多边形判断点击的方式相同。轮廓采样点的密度由调用方按显示需要决定。

**调用方式：**

```cpp
// Corner pin: quad holds the four target corners in the parent's coordinates.
Matrix matrix = {};
if (matrix.setRectToQuad(referenceRect, quad)) {
  layer->setMatrix(matrix);
}

// Preset warp placed first so that the drop shadow follows the warped outline.
auto warp = WarpFilter::Make(WarpType::Bend, referenceRect, 0.5f, 0.0f);
layer->setFilters({warp, DropShadowFilter::Make(0, 6, 0, 0, shadowColor)});

// Parameter changes and content edits.
warp->setStrength(0.8f);
warp->setReferenceRect(newTextBounds);
```

四角拖动时由调用方维护四角位置并检查凸性，每次拖动重新调用 `setRectToQuad()`；撤销与重做由调用方保存四角位置、预设类型与参数后重新设置。

## 6 测试计划

### 6.1 单元测试

| 测试对象 | 验证内容 |
|---|---|
| `Matrix::setRectToQuad()` | 参考矩形四角映射到 dst 四点；dst 为平行四边形时透视分量为零；三点共线时返回 false 且矩阵不变 |
| `Layer::setMatrix()` | 设置透视矩阵后 `matrix()` 返回单位矩阵，`matrix3D()` 保留透视分量；`setPosition()` 后原点映射到目标位置且透视形状不变 |
| `WarpFilter::mapPoint()` | 七种模板在参考矩形四角、四边中点与中心点的结果与 Demo 公式一致；strength 为 0 时恒等；参考矩形外的点位移等于最近边界点的位移 |
| 网格生成 | 等分数为 2 的幂且不超过 128；拉伸模板只生成一个内部单元；不同裁剪范围下同一输入点映射到相同位置 |
| `filterBounds()` | Forward 结果包含实际网格位置包围盒；Reverse 结果包含所有变形后落入目标矩形的输入点 |
| `inputScaleFactor()` | 高度拉伸 s = 1 时为 2，高度挤压时为 1，恒等时为 1 |
| `inputOrigin` | Surface 与 Picture 两条录制路径、子树缓存路径下，参考矩形的四角映射到输入图像的正确像素 |

### 6.2 截图测试

截图用例加入 `test/src/LayerFilterTest.cpp`，内容居中、四边留约 50 像素边距：

1. 文字、矩形、普通矢量、图片填充矢量、带描边的开放路径各应用七种模板，strength 取 0.5、-1 与 1。
2. 同一图层同时设置四角透视矩阵与预设变形，检查先预设后透视的顺序。
3. WarpFilter 分别放在投影滤镜之前与之后，检查投影跟随外形与投影被弯曲两种结果；图层样式（投影样式）始终被变形。
4. 画布缩放 0.5、1、4 倍时，弯曲与扇形边缘的折线程度与清晰度；拉伸模板在 s = 1 时与未放大倍率的结果对照。
5. 参考矩形外有投影样式的图层应用透视模板 strength = 1，检查参考矩形外的内容没有发散。

### 6.3 端到端验证

在 DisplayList 中放置多个图层，按固定序列执行：设置与修改预设参数、切换模板、移除与重新添加 WarpFilter、修改文字内容并更新参考矩形、拖动四角、缩放与平移画布、在分块渲染模式下局部重绘。序列结束后截取整图，与同一最终状态下全新创建的 DisplayList 的截图对比，检查脏区、子树缓存与分块渲染没有残留旧的变形结果。

## 7 实现步骤

| 阶段 | 文件 | 内容 |
|---|---|---|
| 1 四角透视 | `include/tgfx/core/Matrix.h`、`src/core/Matrix.cpp`、`src/layers/Layer.cpp`、`include/tgfx/layers/Layer.h` | 实现 `setRectToQuad()`；修正 `setMatrix()` 的仿射标志与 `matrix()` 注释 |
| 2 网格滤镜 | `include/tgfx/core/ImageFilter.h`、`src/core/filters/MeshWarpImageFilter.h/.cpp` | 实现 `ImageFilter::MeshWarp()` 的边界与离屏绘制 |
| 3 滤镜接口 | `include/tgfx/layers/filters/LayerFilter.h`、`LayerImageFilter.h/.cpp`、`NoiseFilter.h/.cpp`、`src/layers/Layer.cpp`、`src/layers/OffscreenRenderer.cpp` | 增加 `inputOrigin` 参数与 `inputScaleFactor()`；两个离屏入口计算 inputOrigin 并把 density 乘以 k |
| 4 预设滤镜 | `include/tgfx/layers/filters/WarpFilter.h`、`src/layers/filters/WarpFilter.cpp` | 预设映射、R 外平移、网格生成与缓存、位移极值、倍率 k、`mapPoint()` |
| 5 测试 | `test/src/LayerFilterTest.cpp`、`test/src/LayerTest.cpp` | 6.1～6.3 的用例 |

## 8 参考资料

- 交互原型：`~/Downloads/transform-raw_back/transform-tool-demo.html`，操作说明同目录 `README.txt`，原型分析 `transform-tool-analysis.md`
- tgfx：`src/layers/Layer.cpp`、`src/layers/OffscreenRenderer.cpp`、`include/tgfx/layers/filters/LayerFilter.h`、`src/layers/filters/LayerImageFilter.cpp`、`src/layers/filters/NoiseFilter.cpp`、`src/core/ImageFilter.cpp`、`src/core/images/FilterImage.cpp`、`src/core/images/PictureImage.cpp`、`include/tgfx/core/Mesh.h`、`src/core/Matrix.cpp`
- CSS Transforms Level 2：https://drafts.csswg.org/css-transforms-2/
- Chromium（相对 `/Users/chenjie/Desktop/Develop/chromium/src`）：`third_party/blink/renderer/platform/transforms/transform_operations.h`、`ui/gfx/geometry/transform.cc`