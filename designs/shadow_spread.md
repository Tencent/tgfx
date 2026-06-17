# Shadow Spread 参数设计

## 1 背景

为 DropShadow 和 InnerShadow 添加 Spread 参数，控制阴影在模糊之前的膨胀或收缩范围。该功能对标 Figma 的 Drop Shadow Spread 行为：

- Spread > 0：阴影形状向外扩展（DropShadow）/ 向内扩展（InnerShadow）
- Spread < 0：阴影形状向内收缩（DropShadow）/ 向外收缩（InnerShadow）
- Spread = 0：无额外形变，保持现有行为

参数为单一 float 值，取值无限制，单位像素。X/Y 方向使用相同 radius。

**术语定义：**

- Dilate（膨胀）：形态学操作，取邻域最大值，扩展非透明区域
- Erode（腐蚀）：形态学操作，取邻域最小值，收缩非透明区域
- Outset（外扩）：几何操作，对已知形状的轮廓向外平移指定距离
- Inset（内缩）：几何操作，对已知形状的轮廓向内平移指定距离
- StyledShape：用于 spread 的几何源描述，包含形状 + 样式信息
- L∞ 距离（切比雪夫）：max(|Δx|, |Δy|)，对应正方形邻域
- L² 距离（欧式）：√(Δx² + Δy²)，对应圆形邻域
- 解析 SDF（Signed Distance Function）：在 shader 中用代数公式直接计算给定点到形状边界的有符号距离，仅适用于能用闭式表达的简单几何（rect、rrect、circle、ellipse 等），不依赖纹理采样
- 距离场 SDF（Signed Distance Field）：和原图同尺寸的纹理，每像素存储到形状边界的有符号距离，需要预处理（如 JFA）构造，可用于任意形状但有额外内存和 pass 开销

## 2 现有实现分析

### 2.1 架构分层

```
公开 API 层（include/tgfx/layers/）
  ├─ DropShadowStyle      → ImageFilter::DropShadowOnly()
  ├─ InnerShadowStyle     → ImageFilter::InnerShadowOnly()
  ├─ DropShadowFilter     → ImageFilter::DropShadow() / DropShadowOnly()
  └─ InnerShadowFilter    → ImageFilter::InnerShadow() / InnerShadowOnly()

核心 ImageFilter API（include/tgfx/core/ImageFilter.h）
  ├─ DropShadow / DropShadowOnly
  └─ InnerShadow / InnerShadowOnly

核心实现层（src/core/filters/）
  ├─ DropShadowImageFilter    基于 FragmentProcessor 的 GPU 渲染
  └─ InnerShadowImageFilter   基于 FragmentProcessor 的 GPU 渲染
```

### 2.2 当前 DropShadow 渲染管线

```
源图像 → 偏移(dx, dy) → 模糊(blurFilter) → 着色(SrcIn) → 合成(SrcOver)
```

`getShadowFragmentProcessor` 通过 UV 矩阵偏移实现位移，通过 `blurFilter->asFragmentProcessor()` 实现模糊，通过 `XfermodeFragmentProcessor(SrcIn)` 实现着色。

### 2.3 当前 InnerShadow 渲染管线

```
源图像 → 偏移(dx, dy) → 模糊(blurFilter) → 反转(SrcOut) → 着色 → 裁剪到源(SrcATop/SrcIn)
```

### 2.4 LayerStyle 调用流程

Layer 调用 LayerStyle 的关键路径：

```
Layer::drawDirectly()
  → Layer::getLayerStyleSource()     // 生成 content image（光栅化后的 bitmap）
  → Layer::drawContents()
    → Layer::drawLayerStyles()       // 遍历 _layerStyles
      → Layer::drawLayerStyleDefault()
        → layerStyle->draw(canvas, contentImage, contentScale, offset, alpha)
```

`getLayerStyleSource()` 中 content image 的生成：通过 `OpaqueContext` 录制 + `drawContents()` 绘制 + `ToImageWithOffset()` 光栅化为 Image。**到达 LayerStyle 时，几何信息已经丢失**——这是当前实现下添加 spread 的核心障碍。

`drawLayerStyleDefault()` 在调用 `layerStyle->draw()` 前设置了 canvas 变换（`scale(1/contentScale)` + `translate(offset)`），然后根据 `extraSourceType` 选择调用 `draw()` 或 `drawWithExtraSource()`。

## 3 参考设计分析

### 3.1 CSS 标准中的两种阴影

CSS 定义了两种阴影机制，它们在输入、spread 支持和实现层级上完全不同：

| 维度 | box-shadow | filter: drop-shadow() |
|------|-----------|----------------------|
| 输入 | 元素的几何形状（rect + border-radius） | 元素内容的像素图像（alpha 通道） |
| 阴影形状 | 跟随 box 几何 | 跟随 alpha 轮廓 |
| Spread 支持 | ✅ | ❌ |
| 实现方式 | 几何 outset + 模糊 | 像素级 ImageFilter |

`box-shadow` 能支持 spread 是因为它已知元素的几何形状，可以直接对 rect/rrect 做 outset。`drop-shadow()` 的输入是任意像素图像，没有几何信息可以 outset，因此 CSS 规范未为其定义 spread 参数。

#### box-shadow 中 "box" 的含义

`box-shadow` 中的 "box" 不是元素的外接矩形，而是 CSS box model 定义的**元素轮廓**——可以是矩形，也可以是圆角矩形（包括退化为完整椭圆/圆的情况）：

| 元素样式 | box 形状 | box-shadow 形状 |
|---------|---------|----------------|
| 默认 | 矩形 | 矩形 |
| `border-radius: 10px` | 圆角矩形 | 圆角矩形（角半径 + spread） |
| `border-radius: 50%` 且元素为正方形 | 圆 | 圆 |
| `border-radius: 50%` 且元素为非正方形 | 椭圆 | 椭圆 |

但 box 始终是**单一连通的凸形状**（rect + border-radius 能描述的形状），CSS box model 没有"中空"或"多区域"的概念，因此 box-shadow 无法表达：

- 环形阴影（带洞的拓扑）
- 任意 Path 阴影
- 多个分离区域的阴影

#### CSS 对环形场景的限制

通过 HTML 实测（`designs/shadow_spread_test.html`）验证：

- 用 `border: 20px solid` 模拟环形（背景透明，border 实心）
- 给该元素加 `box-shadow` + spread：阴影是**实心椭圆**，完全忽略 border 中间的透明孔洞
- 给该元素加 `filter: drop-shadow()`：阴影正确跟随环形 alpha 轮廓，但 **drop-shadow 没有 spread 参数**

CSS 中不存在"环形阴影 + spread 调整"的能力组合。

### 3.2 Chrome（Chromium/Blink）实现

Chrome 对两种阴影采用完全不同的渲染路径：

**box-shadow 路径（几何绘制，不走 ImageFilter）：**

```
元素 box (rect + border-radius)
  → Outset(spread)                    ← Blink 层做几何扩展
  → ApplySpreadToShadowShape()        ← 圆角矩形用 OutsetWithCornerCorrection 保证角的正确性
  → DrawLooper(offset, blur, color)   ← Skia 的 SkDrawLooper 处理偏移和模糊
  → 直接绘制扩展后的 rect/rrect
```

关键代码（`box_painter_base.cc`）：

```cpp
gfx::RectF fill_rect = border.Rect();
fill_rect.Outset(shadow.Spread());                  // 几何扩展，纯数学操作
ApplySpreadToShadowShape(rounded_fill_rect, shadow.Spread()); // 圆角补正
```

Spread 和 blur 分开处理：spread 在绘制前改变几何形状，blur 由 Skia DrawLooper 在绘制时应用。DrawLooper 只接收 offset 和 blur，不处理 spread。

**filter: drop-shadow() 路径（像素级 ImageFilter）：**

```
元素内容渲染为 bitmap
  → FEDropShadow::CreateImageFilter()
  → SkImageFilters::DropShadow(dx, dy, sigmaX, sigmaY, color, input)
  → 对 bitmap 的 alpha 通道做 blur + offset + colorize
```

直接调用 Skia 的 `SkImageFilters::DropShadow()`，没有 spread 参数。即使 `ShadowData` 结构体中有 `spread_` 字段，在构建 `FEDropShadow` 时也被完全忽略。

### 3.3 Skia 实现

Skia 的 `SkImageFilters::DropShadow()` 没有 spread 参数：

```cpp
static sk_sp<SkImageFilter> DropShadow(
    SkScalar dx, SkScalar dy,
    SkScalar sigmaX, SkScalar sigmaY,
    SkColor4f color, sk_sp<SkColorSpace>,
    sk_sp<SkImageFilter> input,
    const CropRect& cropRect = {});
```

内部实现是滤镜组合链：

```
input → Blur(sigmaX, sigmaY) → ColorFilter(SrcIn) → MatrixTransform(offset) → [Merge(original)]
```

Skia 提供了独立的 `SkImageFilters::Dilate()` / `Erode()` 形态学滤镜，可通过滤镜链组合来模拟 spread 效果。其膨胀采用可分离两 pass 架构：

- **Linear pass**（R ≤ 14）：1D 可分离卷积，每像素 (2R+1) 次采样
- **Sparse pass**（R > 14）：利用前一 pass 已经聚合的结果，每像素仅 2 次采样即可让有效核半径翻倍

通过 Linear + 多次 Sparse 串联，复杂度从 O(R) 降到 O(log R) pass。但这种可分离架构本质上是 L∞（切比雪夫）距离的膨胀——邻域形状是正方形，**圆经过 dilate 会变成圆角矩形**，与几何 outset 的语义不同。

### 3.4 Figma 的 Spread 行为

Figma 明确限制：**Spread is only supported for rectangles, ellipses, frames, and components。** 对 Group 和自由 Path 不支持 spread。

通过多组实测验证：

**实测 1（test2.png）— 图片填充的 Frame：**

对一个包含窗户图片的 frame，设置 Drop Shadow spread = 1 后，阴影直接变成整块实心矩形，窗户内部的透明区域被完全忽略。说明 spread 作用于 frame 的矩形边框，不是内容像素。

**实测 2（test3.png）— 椭圆 fill vs 椭圆 inner stroke（环形）：**

- 椭圆 fill + spread = 10：阴影仍然是**完美的圆**，半径增大 10px — 几何 outset，不是像素级 dilate（dilate 会让圆变方）
- 椭圆 inner stroke 20（环形）+ spread = 10：外圈向外扩大、内圈向内缩小，形状仍然是完美的圆 — Figma 知道这是 ellipse + stroke，分别对外半径 +spread、内半径 -spread
- 图片填充 + spread = 0：阴影保持窗户的 alpha 轮廓
- 图片填充 + spread = 10：阴影变成实心矩形（退化为 frame bounds）

**实测 3（test4.png）— Frame/Group 容器：**

两个圆环组成 Frame 或 Group，给容器整体添加 Drop Shadow：

- 阴影保持了两个圆环各自的形状（包括环中间的洞）— 容器的阴影用的是 content alpha
- **Spread 字段灰色不可编辑**，固定为 0 — 容器整体阴影不支持 spread

**Figma 的两条阴影路径：**

```
spread == 0:
  阴影源 = 图层内容的 alpha 通道（对任何图层类型都工作）
  → offset → blur → colorize

spread != 0（仅 rectangle/ellipse/frame/component）:
  阴影源 = 图层的几何形状 outset(spread) 后光栅化
  → offset → blur → colorize
```

阴影滤镜本身不感知 spread，只接收 (bitmap, offset, blur, color)。spread != 0 时上层替换阴影源为 outset 后的几何形状。

#### 3.4.1 WASM 反向分析印证

通过对 Figma WASM（`https://static.figma.com/.../compiled_wasm.wasm.br`）的字符串和 shader 反向分析，进一步印证上述行为推断。

**关键的内部 assertion 字符串：**

```
"Failed to find geometryNode for spread shadow subtree"
"Failed to unwrap shadow spread node for tree"
"Unexpected effect type for spread shadow"
"Unexpected wrapper node type for spread shadow"
visual_bell.shadow_spread_is_not_supported_for_vector_type
visual_bell.style_s_spread_field_no_longer_applies_when_converting_to_vector_type
containsOnlySpreadEligibleNodes
clear_spread_for_type
```

这些字符串明确表达了 spread 在 Figma 内部的实现位置：

- `geometryNode for spread shadow subtree` — spread shadow 是一棵以"几何节点"为基础的子树
- `containsOnlySpreadEligibleNodes` — 节点级别的 spread 资格判断
- 错误信息以"vector_type"作为不支持的类型边界 — 与 UI 中"Spread 仅支持 rect / ellipse / frame / component"的限制完全对应

**形状专属阴影 shader：**

WASM 中包含多套形状专属的阴影 shader，按 uniform 结构归类：

| Shader | 关键 uniform | 用途 |
|--------|------------|------|
| RoundedRectangleBlur | `halfSizeOverBlurRadius`, `cornerRadiusOverBlurRadius`, `blurRadius` | 圆角矩形 Drop Shadow（几何路径） |
| RectangleInnerShadow | `shadowBounds`, `maskBounds`, `blurRadius` | 矩形 Inner Shadow |
| RoundedRectangleInnerShadow | `shadowBounds`, `shadowHalfSize`, `shadowCornerRadius`, `maskBounds`, `maskHalfSize`, `maskCornerRadius`, `blurRadius` | 圆角矩形 Inner Shadow |
| MaskInnerShadowTile | （采样源 alpha 纹理） | 通用 alpha 路径（Inner Shadow） |
| MaskKnockoutShadowTile | （采样源 alpha 纹理） | 通用 alpha 路径（knockout Shadow） |

**shader 不接收 spread 参数。** 所有阴影 shader 的 uniform 都没有 `spread` 字段。spread 在 CPU 端被 outset 进了 `shadowHalfSize` / `shadowCornerRadius` / `shadowBounds`：

```
对一个 100×60、cornerRadius=10、spread=20 的矩形 DropShadow，CPU 端计算：
  shadowHalfSize     = (50 + 20, 30 + 20) = (70, 50)
  shadowCornerRadius = 10 + 20 = 30
  shadowBounds       = outset(原 bounds, 20)
GPU shader 接收的就是这些扩展后的值，shader 自己不知道 spread 的存在。
```

InnerShadow 的双矩形结构（`shadowBounds` + `maskBounds`，配合两套 cornerRadius）是 spread 的几何编码方式：`mask*` 描述图层本身的形状作为裁剪范围，`shadow*` 描述 spread 形变后的阴影"穿透"形状。

**shader 的核心算法是 Distance Function（解析 SDF）。** shader 在 fragment 阶段直接用代数公式计算"当前像素到圆角矩形的距离"，再用积分公式算出 box blur 的覆盖率，单 pass 完成。整个过程不涉及：

- 预渲染源形状到纹理
- 距离场（distance field）
- 形态学卷积
- 模糊卷积

这种做法的前提是形状必须能用解析 SDF 表达——这从架构层面解释了为什么 Figma 限制 spread 仅适用于 rect / rrect / ellipse / 容器。

**dilate / erode / feMorphology 关键字仅出现在 SVG 导出代码：**

```
<feMorphology radius="%g" operator="%s" in="SourceAlpha" result="%s"/>
```

是导出 SVG 时用 `feMorphology` 近似 Figma 阴影效果的兜底，**不是 Figma 内部渲染时的实现**。这反过来说明 Figma 内部的实现比 SVG morphology 表达力更强（能保持形状精度），导出时只能降级为 morphology。

**总结：** Figma 的 spread 实现 = **CPU 几何 outset + 形状专属 SDF shader 单 pass 渲染**。spread 不存在于 GPU 层面，shader 只画"已经 outset 后的形状 + blur"。和 Chrome box-shadow 的 `Outset(spread) + DrawLooper(blur)` 思路完全一致，区别仅在 Figma 用解析 SDF shader 代替 DrawLooper 走的高斯卷积，性能更优。

## 4 对比分析

### 4.1 阴影效果的附着位置

三种框架中，阴影效果的附着对象不同，直接影响了 spread 实现时对几何信息的可达性：

**Figma — 挂在节点（Node）上：**

Figma 的场景树中每个节点本身就是一个形状（Rectangle、Ellipse、Vector）或容器（Frame、Group、Component）。Effect 通过 `node.effects[]` 数组挂在节点上。节点自己知道自己的几何类型，因此在处理 spread 时可以直接访问几何信息做 outset。

```
RectangleNode (width=100, height=60, cornerRadius=10)
  └─ effects: [DropShadowEffect(spread=10, ...)]   ← 节点知道自己是矩形，可以 outset
```

**Chrome — 挂在元素的 CSS 属性上：**

box-shadow 是元素 ComputedStyle 的直接属性（`ShadowList*`），drop-shadow 是 filter 属性中的一个操作（`DropShadowFilterOperation`）。两者独立存储，走不同的渲染路径。box-shadow 在绘制时可以访问元素的 box model（rect + border-radius），因此能做几何 outset。

```
Element (box: 100x60, border-radius: 10px)
  ├─ style.boxShadow: ShadowList [ShadowData(spread=10, ...)]    ← 绘制时能访问 box 几何
  └─ style.filter: [DropShadowFilterOperation(no spread)]         ← 只看像素，无几何信息
```

**TGFX — 挂在图层（Layer）上：**

LayerStyle 通过 `layer->layerStyles` 挂在 Layer 上。LayerStyle 的 `onDraw` 收到的是整个图层已光栅化的 content image（`std::shared_ptr<Image>`），没有原始几何信息。

```
VectorLayer
  ├─ elements: [Element(ellipse), Element(rect), ...]   ← 多个形状
  ├─ layerStyles: [DropShadowStyle(spread=10, ...)]     ← 收到的是合并后的 bitmap
  └─ (content image = 所有 elements 光栅化后的结果)
```

### 4.2 三种框架对比总结

| 维度 | Figma | Chrome (box-shadow) | Chrome (drop-shadow) | TGFX 现状 |
|------|-------|---------------------|---------------------|----------|
| 附着对象 | 节点 | 元素属性 | filter 操作 | Layer |
| 节点/图层粒度 | 单一形状或容器 | 单一 box | 元素整体 | 可含多个 Element |
| 阴影处理时几何可达 | ✅ | ✅ | ❌ | ❌ |
| Spread 支持 | ✅ | ✅ | ❌ | ❌（待添加） |
| Spread 实现方式 | CPU 端 outset 进 shader uniform | CPU 端 Outset(spread) | N/A | 待设计 |
| Spread 是否走滤镜 | ❌（与几何融合在 SDF shader） | ❌（直接画扩展形状） | N/A | 待设计 |
| 渲染管线 | 形状专属解析 SDF shader 单 pass | 几何 outset + Skia DrawLooper（高斯卷积） | SkImageFilters::DropShadow（采样源 alpha） | 现状走 ImageFilter 多 pass |
| 多形状容器 spread | ❌（用 content alpha） | N/A | N/A | 同左原则 |

### 4.3 关键结论

1. **spread 的本质是几何操作**（对已知形状做 outset），不是像素操作（对 bitmap 做 dilate / scale）
2. **支持 spread 的前提是阴影处理时可达几何信息**——三种框架在能拿到几何时都做几何 outset，拿不到时都不支持
3. **阴影滤镜本身不应感知 spread**——spread 在滤镜上游处理，通过替换/构造阴影源实现
4. **多形状/复杂图层场景应降级为 bounds**——Figma 在 Frame/Group 场景禁用 spread UI（Spread 字段灰色），TGFX 在这类场景以图层 bounds Rect 作为几何源，spread 作用于矩形而非各子形状，视觉等价于 spread 不可用，但避免了"开关型"的强制降级

这些结论决定了 TGFX 的实现方向：必须在 Layer 层（保留几何信息的层）处理 spread，而不是在 ImageFilter 层（已经是 bitmap）。

## 5 实现方案

实现 spread 有两种思路。先并列说明两种方案及其权衡，再给出选型结论。保留方案 A 是为了清晰论证为什么走 Layer 层几何路径而不是看起来更"通用"的滤镜路径。

### 5.1 方案 A：膨胀滤镜（像素级）

在阴影管线中插入一个膨胀（dilate）/ 腐蚀（erode）滤镜，对源图像的 alpha 通道做形态学操作。该滤镜接收 bitmap，输出 bitmap，与 ImageFilter 管线天然契合。

#### 方案 A.1：Shader 暴力卷积

直接在 fragment shader 里采样邻域并取 max（dilate）或 min（erode）。

**矩形核（L∞ 距离，可分离）：**

```glsl
// X 方向 1D pass，Y 方向 1D pass
half4 aggregate = sample(coord);
for (int i = 1; i <= R; ++i) {
    aggregate = max(aggregate, max(sample(coord + offset * i),
                                   sample(coord - offset * i)));
}
```

可分离为两个 1D 卷积，每方向 (2R+1) 次采样。Skia 的 `SkImageFilters::Dilate()` 在此基础上用 Linear + Sparse 优化，把大半径的 pass 数从 O(R) 降到 O(log R)。

**Skia 的策略不能直接为我们所用：** Skia 的可分离架构本质上是 L∞ 距离的膨胀，邻域形状是正方形。一个圆经过两个 1D pass 的可分离 dilate 后，得到的是 X、Y 两个方向各自外扩的并集——即圆角矩形而非更大的圆。这是 L∞ 邻域的固有结果，与几何 outset"圆仍是圆"的语义不同。

而 Figma 的 spread 对椭圆要求"扩大后仍是椭圆"——这需要 L²（欧式）距离的膨胀，邻域形状是圆。L² 核函数 `K(x,y) = (x² + y² ≤ R²)` 不可分离（X 和 Y 在判定时耦合），因此 Skia 的 Linear+Sparse 双层可分离架构无法等效成欧式距离膨胀。

**圆形核暴力卷积的代价：**

```glsl
for x in [-R, R]:
    for y in [-R, R]:
        if (x*x + y*y <= R*R):
            result = max(result, sample(coord + (x,y)));
```

每像素 πR² ≈ 3.14R² 次采样，且不可分离，单 pass 完成。R = 10 时约 314 次采样，R = 50 时约 7850 次——大半径下完全不可行。

#### 方案 A.2：基于 SDF 的距离阈值

构造一张和原图同尺寸的 **Signed Distance Field**（每像素存储到最近非透明像素的欧式距离），然后用阈值操作得到任意半径的膨胀结果。

**核心管线：**

```
原图 alpha
  → JFA (Jump Flooding Algorithm) 构造距离场，log₂(N) 个 pass
  → SDF 纹理（每像素存欧式距离）
  → threshold(R) 单 pass → 膨胀结果
```

**JFA 原理：** 每个非透明像素初始记录"我自己是最近非透明点"，然后以 step = N/2、N/4、…、1 的步长（log₂(N) 轮）迭代，每像素查看 8 个跳跃方向上的邻居，更新自己的"最近非透明点"记录。约 log₂(1024) ≈ 10 个 pass 后即可完成 1024 像素图的距离场构造。

**优势：**

- **半径无关**：膨胀任意半径都是单次阈值操作，O(1) 开销
- **欧式距离正确**：圆膨胀后仍是圆，环形外圈外扩、内圈缩小，对齐 Figma 几何路径在简单形状上的视觉效果

**代价：**

- 额外内存：一张同尺寸的 SDF 纹理（RG16F 或类似格式）
- 固定开销：log₂(N) 个 pass 的 JFA 构造，对小图（如 100×100）开销不一定比暴力卷积小
- 实现复杂度：需要写多个 shader、管理多个 render target、处理 JFA 的边界精度问题

#### 方案 A 共同的根本问题

无论暴力卷积还是 SDF，方案 A 都是**像素级操作**，存在两个根本问题：

1. **无法编码图层语义**：膨胀算法对像素操作，无法区分"图片填充"和"几何形状"。在 ImageLayer 上膨胀会沿图片 alpha 通道扩展（窗户图片的轮廓被膨胀），需要的是按图层外接矩形 outset；在多形状 VectorLayer 上膨胀会沿合成 alpha 扩展各子形状，需要的是按容器 bounds Rect outset。方案 B 在 Layer 层让每种图层自描述其几何源，天然区分这些场景
2. **无法表达 stroke / fill 不同的 outset 语义**：spread 对 fill 是路径 outset，对 stroke 是环变宽——像素操作无法识别 source image 哪些区域是 fill 哪些是 stroke

### 5.2 方案 B：基于几何的 Layer 层实现

spread 在 Layer 层处理：Layer 向 LayerStyle 传递几何描述（StyledShape），LayerStyle 在绘制时根据 spread 值对几何做 outset，把扩展/收缩后的形状光栅化为一张 alpha image，再喂给现有的阴影 ImageFilter（DropShadow）或 mask-shader 路径（InnerShadow）。

#### 设计原则

1. **阴影滤镜不感知 spread**：`ImageFilter::DropShadowOnly` / `InnerShadowOnly` 不加 spread 参数，保持纯粹的像素操作 `(alpha image, offset, blur, color) → image`，与 Skia、Chrome 一致。spread 只改变喂给滤镜的源 image
2. **spread 在 Layer 层处理**：Layer 把几何描述（StyledShape）经 `LayerStyleInput` 传给 LayerStyle，LayerStyle 在绘制时根据 spread 值对几何做 outset / inset，把结果形状光栅化为 alpha image 后送入现有滤镜，不需要新增 GPU shader
3. **几何 outset 而非像素 dilate**：对已知几何形状做 outset，保持形状精度（圆仍是圆）
4. **所有需要 spread 的图层都返回 StyledShape**：单一可识别形状（Rect / RRect / Oval / 单 path）的图层返回该形状 + 样式信息；复杂或不可解析的图层返回 bounds Rect（type = Fill）。spread 在所有场景下都有一致的兜底视觉，无需引入"禁用 spread"的开关。

#### StyledShape 数据结构

封装"用于 spread 的几何源"，描述阴影的原始形状及其渲染方式。定义在 `include/tgfx/layers/layerstyles/StyledShape.h`：

```cpp
enum class StyledShapeType {
  Fill,        // 仅填充
  Stroke,      // 仅描边
  FillStroke   // 填充 + 描边
};

struct StyledShape {
  std::shared_ptr<Shape> shape = nullptr;            // 几何形状
  StyledShapeType type = StyledShapeType::Fill;      // fill / stroke / fill+stroke
  float strokeWidth = 0.0f;                          // stroke 时的描边宽度
  StrokeAlign strokeAlign = StrokeAlign::Center;     // 描边对齐方式
};
```

`StyledShape::Make(shape, hasFill, hasStroke, strokeWidth, strokeAlign)` 静态方法按 fill / stroke 的有无映射出对应的 `StyledShapeType`。

阴影绘制时根据 spread 值对 StyledShape 做几何操作：

| 样式 | spread > 0 | spread < 0 | 说明 |
|------|-----------|-----------|------|
| Fill | shape outset(spread)，实心填充 | shape inset(-spread)，实心填充 | 任意 fill 适用 |
| Stroke (Center) | 描边有效宽度 += 2·spread，环变宽 | 描边有效宽度 -= 2·spread，环变窄 | 仅 Center 对齐参与 outset |
| Stroke (Inside / Outside) | 按对齐方式调整描边绘制位置后再 outset | 同左 | 由 `StrokeOutset` 折算到形状外扩距离 |
| FillStroke | fill 外扩 spread + 描边外伸量 | fill 内缩 | 描边外伸量由 `StrokeOutset(width, align)` 给出 |

#### StyledShape 生成规则

需要 spread 的图层返回一个 StyledShape——单一可识别形状（Rect / RRect / Oval / 单 path）的图层返回该形状 + 样式信息；复杂或不可解析的图层返回图层 tight-bounds Rect（type = Fill）作为兜底。spread 在所有场景下都有一致的视觉表现，差别只在"沿真实形状扩"还是"沿矩形扩"。

识别分两层：

1. **Layer 基类默认实现**：`Layer::onGetContentShape()` 调用静态 `MakeContentShape(getContent())`——取 content 的 tight-bounds，构造一个 fill 类型、shape 为该 bounds Rect 的 StyledShape；content 为空或 bounds 为空时返回 `std::nullopt`。同时基类提供私有的 `getContentShape()`：当图层带子节点（容器）时直接走 bounds 兜底（`MakeContentShape`），否则调用虚方法 `onGetContentShape()`。SolidLayer / ImageLayer / MeshLayer / Layer 容器均走默认实现，不需要 override。
2. **ShapeLayer / VectorLayer override `onGetContentShape()`**：从各自的内容里提取单一形状及其 fill / stroke 信息，无法精确还原时调用 `Layer::onGetContentShape()` 走基类 bounds 兜底。
3. **TextLayer**：没有 override，直接使用基类的 bounds-rect 兜底。spread 沿文字外接矩形扩，不沿字形轮廓扩。视觉退化是有意为之，与 Figma "文字不支持 spread"对齐（Figma 直接禁用 UI，TGFX 选择把视觉退化到矩形以保证规则统一）。

##### ShapeLayer override 规则

`ShapeLayer::onGetContentShape()` 直接基于 `_shape`、`_fillStyles`、`_strokeStyles` 构造：

| 条件 | onGetContentShape 返回 |
|------|--------------------|
| 无 fill 且无 stroke（或 `_shape` 为空） | `std::nullopt` |
| 有 fill 或 stroke | `StyledShape::Make(Shape::MakeFrom(_shape->getPath()), hasFill, hasStroke, stroke.width, strokeAlign)` |

由 `StyledShape::Make` 按 fill / stroke 的有无折算出 `StyledShapeType`，把当前 stroke 的宽度与对齐方式一并携带。Rect / RRect / Oval 等具体形状识别延后到光栅化阶段（见 `SpreadUtils`），ShapeLayer 这一层只负责交出 path + 样式语义。

##### VectorLayer override 规则

`VectorLayer::onGetContentShape()` 遍历 `_contents`，通过 `VectorContext` 收集 painters，判定能否简化为单一 StyledShape：

| 条件 | onGetContentShape 返回 |
|------|--------------------|
| 所有 painter 共享同一个 geometry，且至多一个 stroke 样式 | 取该共享 geometry 的 shape（经 `Shape::ApplyMatrix` 应用其变换），按 fill / stroke 语义构造 StyledShape |
| 任一 painter 含多个 geometry、geometry 不一致、或出现多个 stroke 样式 | `Layer::onGetContentShape()`（基类 bounds 兜底） |
| painters 为空或共享 geometry 取不到 shape | `std::nullopt` |

VectorLayer 直接遍历 `context.painters`，检查"单一共享 geometry + 统一样式"，不经过任何 collector 或 getContour 接口。

##### 各 Layer 类型最终行为

| Layer 类型 | 是否 override | 单一形状条件 | 复杂场景 |
|-----------|------|---------|---------|
| SolidLayer | 否 | 始终满足 | — |
| ImageLayer | 否 | 始终满足（图像矩形） | — |
| ShapeLayer | 是 | 有 fill / stroke 时取 `_shape` 的 path | 无 fill 且无 stroke 时 nullopt |
| VectorLayer | 是 | 单一共享 geometry + 至多一个 stroke | bounds Rect + fill |
| TextLayer | 否 | 不满足（走基类 bounds 兜底） | bounds Rect + fill |
| Layer（容器）| 否 | 带子节点时直接走 bounds 兜底 | bounds Rect + fill |
| MeshLayer | 否 | 不满足（走基类 bounds 兜底） | bounds Rect + fill |

只有当至少一个 LayerStyle 的 `needContourShape()` 返回 true 时才会生成 contentShape；否则 `LayerStyleSource::contentShape` 为 `std::nullopt`。复杂图层场景下 spread 沿 bounds Rect 作用，视觉与"沿 frame 矩形扩"一致。

#### Layer 调用流程变更

StyledShape 在 `getLayerStyleSource()` 中按需生成一次，作为 `LayerStyleSource` 的一部分传递给 LayerStyle：

```cpp
struct LayerStyleSource {
  float contentScale = 1.0f;
  std::unique_ptr<LayerStyleSourceGroup> groups[2] = {};
  // 简化后的图层内容形状，仅当有 LayerStyle 需要时才生成，否则为 std::nullopt。
  std::optional<StyledShape> contentShape = std::nullopt;
};
```

`getLayerStyleSource()` 先遍历所有 LayerStyle 聚合 `needContourShape()`，只有当至少一个 style 需要时才调用私有的 `getContentShape()` 填入 `source->contentShape`：

```cpp
bool needContourShape = false;
for (const auto& layerStyle : _layerStyles) {
  needContourShape |= layerStyle->needContourShape();
}
// ... 现有的 content image 生成逻辑 ...
if (needContourShape) {
  source->contentShape = getContentShape();
}
```

`drawLayerStyleDefault()` 在构造 `LayerStyleInput` 时，把 `contentShape` 打包进 `LayerStyleInputSourceContour`（仅当该 style 的 `needContourShape()` 为 true），通过 `extraSource` 交给 LayerStyle：

```cpp
auto contourShape = layerStyle->needContourShape() ? source->contentShape : std::nullopt;
drawSource.extraSource = std::make_shared<LayerStyleInputSourceContour>(
    std::move(contourImage), contourOffset, std::move(contourShape));
layerStyle->draw(canvas, drawSource, alpha);
```

同一图层挂多个 DropShadowStyle 时，所有阴影共享这一份 StyledShape，各自按自己的 spread 值独立做 outset 计算，无需重复构造。

#### LayerStyle 接口变更

StyledShape 通过 `LayerStyleInput` 体系交付，不引入额外的 setter / getter。LayerStyle 基类新增两个虚方法（默认 false）声明对 contour 资源的需求：

```cpp
class LayerStyle {
 public:
  // 是否需要 contour 的光栅化 image（仅在 extraSourceType() 为 Contour 时有意义）。
  virtual bool needContourImage() const { return false; }

  // 是否需要 contour 的矢量形状 StyledShape（仅在 extraSourceType() 为 Contour 时有意义）。
  virtual bool needContourShape() const { return false; }
};
```

DropShadowStyle 在 `needContourShape()` 返回 `_spread != 0.0f`，即只有 spread 非零时才需要 StyledShape。LayerStyle 在 `onDraw` 中通过 `input.extraSource` 取回形状：把 `extraSource` 转型为 `LayerStyleInputSourceContour`，再调用 `shape()`（返回 `const std::optional<StyledShape>&`）。

#### DropShadowStyle 绘制逻辑变更

`onDraw` 中根据 spread 选择源 image——spread 为零时直接用 content image，非零时把 StyledShape 经 spread 形变后光栅化为 alpha image，再喂给现有的 DropShadow 滤镜：

```cpp
void DropShadowStyle::onDraw(Canvas* canvas, const LayerStyleInput& input, float alpha,
                             BlendMode blendMode) {
  auto filter = getShadowFilter(input.contentScale);
  if (!filter) {
    return;
  }
  std::shared_ptr<Image> filterSource = input.content;
  Point filterSourceOffset = {};
  if (!FloatNearlyZero(_spread)) {
    // 几何路径：把 StyledShape 经 spread 形变后光栅化为 alpha image
    auto spreadImage = SpreadUtils::MakeSpreadShapeImage(input, _spread);
    if (spreadImage.collapsed) {
      return;  // 负 spread 把几何完全吞掉
    }
    if (spreadImage.image != nullptr) {
      filterSource = std::move(spreadImage.image);
      filterSourceOffset = spreadImage.offset;
    }
  }
  // 喂给现有滤镜：offset + blur + colorize 全部由 ImageFilter::DropShadowOnly 完成
  auto shadowImage = filterSource->makeWithFilter(filter, &offset);
  // ... 用 filterSourceOffset + offset 把 shadowImage 画到 canvas ...
}
```

`SpreadUtils::MakeSpreadShapeImage(input, spread)` 从 `input.extraSource` 取出 StyledShape，按 path 是 Oval / RRect / Rect 分派到 `DrawSpreadRRect`（其余复杂 path 退化为 bounding rect 的 fill）。形变规则：

- Fill / FillStroke：把 RRect 外扩 `spread`（FillStroke 额外加 `StrokeOutset(width, align)`），角半径同步增减
- Stroke：有效描边宽度 = `strokeWidth + 2·spread`，按 strokeAlign 决定绘制 RRect 的位置

光栅化后通过 `ToImageWithOffset` 得到 alpha image 及其相对 content 的 offset。**spread 不进入滤镜——滤镜接收的始终是 `(alpha image, offset, blur, color)`，与 spread = 0 时完全相同。**

**InnerShadow 的几何路径：**

InnerShadow 表达"光源在外、阴影投在图层内部"的视觉效果，因此 spread 的几何变换方向与 DropShadow **相反**：DropShadow 用 `+spread` 外扩，InnerShadow 用 `-spread` 内缩 mask。`InnerShadowStyle::drawWithSpread` 不走 `ImageFilter::InnerShadowOnly`，而是：

1. 用 `MakeSpreadShapeImage(input, 0)` 得到图层自身形状的 alpha image（shape image），作为绘制底图
2. 用 `MakeSpreadShapeImage(input, -_spread)` 得到 spread 形变后的形状作为 mask；mask 完全 collapse 时阴影填满整个内容区
3. 对 mask image 应用 `ImageFilter::Blur` 做模糊，再按 `_offset` 偏移
4. 用模糊后的 mask 作为反相 mask shader（`MaskFilter::MakeShader(..., true)`），把 shadow 颜色（`ColorFilter::Blend(_color, SrcIn)`）画到 shape image 上，产生内阴影

| 样式 | DropShadow + spread > 0 | InnerShadow + spread > 0 |
|------|-------------------------|--------------------------|
| fill | shape outset(spread)，向外扩大 | mask inset(spread)，阴影源孔洞缩小 |
| stroke | 描边变宽，环变宽 | 描边变窄，环变窄 |

#### DropShadowStyle / InnerShadowStyle 类变更

```cpp
class DropShadowStyle : public LayerStyle {
 public:
  static std::shared_ptr<DropShadowStyle> Make(float offsetX, float offsetY, float blurrinessX,
                                               float blurrinessY, const Color& color,
                                               bool showBehindLayer = true);
  float spread() const { return _spread; }
  void setSpread(float spread);

  bool needContourShape() const override { return _spread != 0.0f; }

 protected:
  void onDraw(Canvas* canvas, const LayerStyleInput& input, float alpha,
              BlendMode blendMode) override;

 private:
  float _spread = 0.0f;
};
```

`setSpread()` 在值变化时调用 `invalidateTransform()` 触发重绘。spread 不影响缓存的 ImageFilter（滤镜只与 offset / blur / color 相关），只改变喂给滤镜的源 image，因此无需失效 ImageFilter 缓存。

#### DropShadowFilter / InnerShadowFilter 变更

Filter 层不经过 Layer 体系，走的是 ImageFilter 管线，没有几何信息。Filter 层的 spread 参数仅做存储，实际 spread 效果不生效，与 CSS `drop-shadow()` 行为一致。

#### filterBounds 变更

`DropShadowStyle::filterBounds` 是**单一统一路径**：先把 srcRect 按 `spread * contentScale` 外扩（spread 非零时），再交给现有滤镜计算 blur / offset 引入的 bounds——spread 是否为零只影响是否预先 outset，不分叉两条不同实现：

```cpp
Rect DropShadowStyle::filterBounds(const Rect& srcRect, float contentScale) {
  auto filter = getShadowFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  auto bounds = srcRect;
  if (!FloatNearlyZero(_spread)) {
    bounds.outset(_spread * contentScale, _spread * contentScale);
  }
  return filter->filterBounds(bounds);
}
```

`InnerShadowStyle::filterBounds` 在 spread 非零时直接返回 srcRect（内阴影不扩展内容 bounds），spread = 0 时走现有滤镜的 `filterBounds`。两种阴影的 bounds 计算都复用现有滤镜，spread 只通过预先 outset srcRect 的方式参与，不重新实现一套 bounds 推导。

### 5.3 方案选型

**采用方案 B（基于几何的 Layer 层实现）。** 选型对比：

| 维度 | 方案 A.1（暴力卷积） | 方案 A.2（SDF） | 方案 B（几何 outset） |
|------|---------------------|----------------|---------------------|
| 椭圆 spread 后形状 | 圆角矩形 ❌ | 椭圆 ✅ | 椭圆 ✅ |
| 环形 spread 后形状 | 不正确 ❌ | 外扩内缩 ✅ | 外扩内缩 ✅ |
| 图片填充 spread | 跟随 alpha ❌ | 跟随 alpha ❌ | 退化为矩形 ✅ |
| 多形状容器 spread | 像素膨胀 ❌ | 像素膨胀 ❌ | 不生效 ✅ |
| 每像素采样数（R=10） | ~314 | ~90 | 1（直接 path 渲染） |
| 每像素采样数（R=100） | ~31400（不可行） | ~90 | 1 |
| pass 数 | 1 | log₂(N) + 1 ≈ 11 | 1（含 blur 是 2~3） |
| 中间纹理 | 0 | 至少 2 张同尺寸纹理 | 0 |
| 实现复杂度 | 中（shader + ImageFilter） | 高（JFA + 多 render target） | 低（复用 Path API） |
| 与现有架构契合度 | 需新增 ImageFilter 类型 | 同上 + Runtime/JFA 框架 | 仅在 Layer 层加虚方法 |

方案 A 的核心问题：

1. **实现成本高**：暴力卷积只能用矩形核，圆变方；要正确实现欧式膨胀必须走 SDF，需在 GPU 上实现 JFA 距离场算法、维护多 pass 渲染管线和中间纹理，工程量大且 shader 复杂度高
2. **性能压力大**：暴力圆形核每像素 πR² 次采样，R = 100 时 ~31400 次采样不可行；SDF 方案虽然半径无关，但需要 log(N) 个 pass + 至少 2 张同尺寸中间纹理
3. **无法编码图层语义**：见 5.1 末尾「方案 A 共同的根本问题」

方案 B 的优势：

1. **天然对齐 Figma 行为**：通过 StyledShape 表达图层语义（fill / stroke / 几何形状），各 Layer 类型按自身能力生成 StyledShape，无法精确还原原始形状时退化为 bounds Rect，spread 不再需要"开关型"禁用
2. **形状精度天然保证**：几何 outset 保证圆是圆、椭圆是椭圆、环形是外扩内缩
3. **实现成本低**：复用现有的 Path/Shape API 做 outset 与 stroke 计算，Layer 层加一个虚方法 + 几个子类实现，无需新增 GPU shader 和 ImageFilter
4. **性能开销小**：把形变后的形状光栅化为一张 alpha image，再复用现有 blur 滤镜，不引入额外 pass 和中间纹理；多个不同 spread 的阴影共享同一份 StyledShape

## 6 测试计划

### 6.1 截图测试

| 用例 | Layer 类型 | 形状 | 样式 | spread | 预期行为 |
|------|-----------|------|------|--------|---------|
| 1 | SolidLayer | RRect | fill | 0 | 回归验证，与现有 baseline 一致 |
| 2 | SolidLayer | RRect | fill | +10 | 阴影 RRect 四周扩大 10px，圆角相应增大 |
| 3 | SolidLayer | RRect | fill | -10 | 阴影 RRect 四周缩小 10px |
| 4 | ShapeLayer | 椭圆 | fill（单一） | +10 | 阴影椭圆扩大，保持完美几何形状 |
| 5 | ShapeLayer | 椭圆 | stroke（单一） | +10 | 环形阴影：外圈扩大，内圈缩小 |
| 6 | ShapeLayer | 椭圆 | fill + stroke（混合） | +10 | 验证 fill+stroke 烘焙后的 outset 椭圆 + spread 扩散 |
| 7 | ImageLayer | 矩形 | fill | +10 | 阴影沿图像外接矩形扩大（与 Figma 行为一致） |
| 8 | VectorLayer | 多个 Element | 混合 | +10 | 验证降级路径：spread 沿外接矩形 bounds 作用，视觉等价于矩形外扩 |
| 9 | VectorLayer | 单个 Element | 单一样式 | +10 | spread 生效，取该 Element 的几何做 outset |
| 10 | SolidLayer | RRect | fill | +10（InnerShadow） | 内阴影向图层内部扩展，阴影孔洞缩小 10px |
| 11 | SolidLayer | RRect | fill | -10（InnerShadow） | 内阴影向外收缩，阴影孔洞扩大 10px |
| 12 | SolidLayer | RRect | fill | +10 + blur=5 | Spread + Blur 组合：扩展形状 + 高斯模糊 |
| 13 | SolidLayer | RRect | fill | +10 + offset=(20, 20) | Spread + Offset 组合：扩展后再偏移 |
| 14 | SolidLayer | RRect | fill | spread = 0 / 10 / 30（叠加 3 个 DropShadowStyle） | 各阴影各自按 spread 值独立生效，共享 StyledShape |
| 15 | TextLayer | 文字 | — | +10 | 验证降级路径：spread 沿文字外接矩形扩，不沿字形轮廓扩 |

### 6.2 边界情况

- spread = 0：行为与修改前完全一致
- spread > 0 在复杂图层（多 element / fill+stroke 混合且 path 非 Rect/RRect/Oval / dash / Inside/Outside align）：spread 沿图层 bounds Rect 作用，视觉等价于矩形外扩
- blur = 0 + spread > 0：阴影是硬边的扩展形状
- 负 spread 导致形状完全收缩消失
- spread 与 contentScale 的交互
- 同一图层挂多个 DropShadowStyle，spread 值各不相同

## 7 实现步骤

| 阶段 | 文件 | 说明 |
|------|------|------|
| 1 | `include/tgfx/layers/layerstyles/StyledShape.h` | 新增 `StyledShape` 结构体（shape / type / strokeWidth / strokeAlign）与 `StyledShapeType` 枚举，提供 `Make(shape, hasFill, hasStroke, ...)` 静态方法 |
| 1 | `include/tgfx/layers/Layer.h` + `src/layers/Layer.cpp` | Layer 基类新增 protected 虚方法 `onGetContentShape()`（默认实现 `MakeContentShape`：返回 content tight-bounds 的 fill StyledShape）与私有 `getContentShape()`（带子节点时直接走 bounds 兜底，否则调 `onGetContentShape()`） |
| 1 | `src/layers/Layer.cpp` | `getLayerStyleSource()` 聚合 `needContourShape()`，需要时调用 `getContentShape()` 写入 `LayerStyleSource::contentShape`；`drawLayerStyleDefault()` 把 contentShape 打包进 `LayerStyleInputSourceContour` 经 `extraSource` 交付 |
| 1 | `src/layers/LayerStyleSource.h` | `LayerStyleSource` 新增 `std::optional<StyledShape> contentShape` 字段 |
| 1 | `include/tgfx/layers/layerstyles/LayerStyleInput.h` | `LayerStyleInputSourceContour` 携带 `std::optional<StyledShape>`，提供 `shape()` 访问器 |
| 1 | `include/tgfx/layers/layerstyles/LayerStyle.h` | 基类新增虚方法 `needContourImage()` / `needContourShape()`（均默认 false） |
| 2 | `include/tgfx/layers/ShapeLayer.h` + `src/layers/ShapeLayer.cpp` | override `onGetContentShape()`：有 fill / stroke 时用 `_shape` 的 path 构造 StyledShape，否则返回 `std::nullopt` |
| 2 | `include/tgfx/layers/VectorLayer.h` + `src/layers/VectorLayer.cpp` | override `onGetContentShape()`：遍历 `VectorContext::painters`，单一共享 geometry + 至多一个 stroke 时构造 StyledShape，否则委托基类返回 bounds 兜底 |
| 2 | TextLayer | 不新增 override，直接使用基类 bounds-rect 兜底 |
| 2 | `src/layers/SpreadUtils.h` + `.cpp` | 新增 `MakeSpreadShapeImage(input, spread)`：从 contour shape 取 StyledShape，按 Oval / RRect / Rect 分派形变并光栅化为 alpha image；`StrokeOutset` / `IsSpreadCollapsed` 辅助 |
| 3 | `include/tgfx/layers/layerstyles/DropShadowStyle.h` + `.cpp` | 新增 `_spread` 成员、getter/setter、`needContourShape()` 返回 `_spread != 0`；`onDraw` 在 spread 非零时用 `MakeSpreadShapeImage` 替换滤镜源，`filterBounds` 预先 outset srcRect |
| 3 | `include/tgfx/layers/layerstyles/InnerShadowStyle.h` + `.cpp` | 同上；spread 非零时走 `drawWithSpread`（mask-shader 路径），`filterBounds` 返回 srcRect |
| 4 | `test/src/` | 截图测试 |

## 8 参考资料

### 8.1 TGFX 项目内

| 路径 | 说明 |
|------|------|
| `src/core/filters/GaussianBlurImageFilter.h/.cpp` | Blur 多 pass 架构参考 |
| `src/gpu/processors/GaussianBlur1DFragmentProcessor.h` | FP 基类模式参考 |
| `src/gpu/glsl/processors/GLSLGaussianBlur1DFragmentProcessor.cpp` | GLSL emitCode 参考 |
| `src/core/filters/ComposeImageFilter.h/.cpp` | Filter 串联机制 |
| `src/layers/Layer.cpp` | LayerStyle 调用流程：`getLayerStyleSource`、`drawLayerStyleDefault` |
| `src/layers/layerstyles/DropShadowStyle.cpp` | 当前 DropShadowStyle 实现 |

### 8.2 Skia

| 路径 | 说明 |
|------|------|
| `skia/src/effects/imagefilters/SkDropShadowImageFilter.cpp` | DropShadow 滤镜组合链实现（无 spread 参数） |
| `skia/src/effects/imagefilters/SkMorphologyImageFilter.cpp` | Dilate/Erode 形态学滤镜（像素级膨胀/腐蚀） |
| `skia/src/sksl/sksl_rt_shader.sksl` | 形态学 shader（linear/sparse pass） |
| `skia/include/effects/SkImageFilters.h` | DropShadow/Dilate/Erode 公开 API 声明 |

### 8.3 Chrome（Chromium/Blink）

| 路径 | 说明 |
|------|------|
| `blink/renderer/core/paint/box_painter_base.cc` | box-shadow 几何绘制：Outset(spread) + DrawLooper(blur) |
| `blink/renderer/core/style/shadow_data.h` | ShadowData 结构体（offset, blur, spread, color） |
| `blink/renderer/platform/graphics/filters/fe_drop_shadow.cc` | drop-shadow() 实现：调用 SkImageFilters::DropShadow，忽略 spread |
| `cc/paint/paint_filter.cc` | DropShadowPaintFilter：封装 Skia DropShadow 调用 |
| `blink/renderer/core/css/properties/css_parsing_utils.cc` | CSS 解析：AllowInsetAndSpread 控制 drop-shadow 禁止 spread |

### 8.4 Figma

| 资源 | 说明 |
|------|------|
| https://developers.figma.com/docs/plugins/api/Effect/ | Figma Plugin API：Effect / DropShadowEffect / InnerShadowEffect 数据结构 |
| `https://static.figma.com/.../compiled_wasm.wasm.br` | Figma 渲染引擎 WASM（含阴影 shader 字符串、内部 assertion 等反向分析依据） |

### 8.5 实测验证产物

| 路径 | 说明 |
|------|------|
| `designs/shadow_spread_test.html` | CSS box-shadow / drop-shadow 行为对比，含矩形/椭圆/环形/图片填充场景 |
| `~/Desktop/test2.png` | Figma 实测：图片填充 frame + spread |
| `~/Desktop/test3.png` | Figma 实测：椭圆 fill / inner stroke + spread |
| `~/Desktop/test4.png` | Figma 实测：Frame/Group 容器整体阴影（spread 灰色） |
