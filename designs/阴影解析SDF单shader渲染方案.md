# 可解析形状阴影效果单 Shader 性能优化方案

## 1 背景

TGFX 的内外阴影（DropShadow / InnerShadow）当前走"把形状光栅化成 alpha 纹理 → 多 pass 高斯模糊滤镜 → 上色"的通用滤镜路线：一次阴影绘制约 3~5 个 render pass、2~3 张中间纹理，且模糊半径受 `MAX_BLUR_SIGMA = 10` 约束，超出只能靠缩小 render target 近似、牺牲质量。对于图层轮廓本身就是 Rect / RRect 这类简单形状的常见场景，这条通用管线付出了不必要的多 pass 与纹理带宽代价。

优化目标：在**阴影数据源可解析为简单形状（Rect / RRect）**时，把内外阴影的渲染降为单 pass、零中间纹理，并解除模糊半径上限；不可解析的形状维持现有滤镜路径不变。

术语：

- **解析 SDF（Signed Distance Function）**：用闭式代数公式直接计算点到形状边界的有符号距离（外正内负），是逐点推导覆盖率（见下条）的输入；仅适用于 rect / rrect / circle 等简单几何，不依赖纹理采样或预处理。
- **覆盖率（coverage）**：fragment 落在形状内的比例 [0,1]。
- **spread（扩散）**：阴影在模糊之前的膨胀（正）/ 收缩（负），对已知形状是几何 outset / inset。
- **pass（render pass）**：绑定同一 render target 的一段连续渲染（期间可含多次 draw call）。多 pass 意味着中间纹理的写入与再采样开销。
- **σ（sigma）**：高斯核标准差，即对外 API 的 `blurrinessX` / `blurrinessY`。
- **截断高斯核**：高斯限制在 ±2σ 内、并按区间质量重新归一化后的核。现状滤镜使用的正是这种核。

## 2 现有实现分析

### 2.1 DropShadow 渲染管线

关键路径：`DropShadowStyle::onDraw`（`src/layers/layerstyles/DropShadowStyle.cpp`）→ `SpreadUtils::MakeSpreadShapeImage`（`src/layers/SpreadUtils.cpp`）→ `GaussianBlurImageFilter::lockTextureProxy`（`src/core/filters/GaussianBlurImageFilter.cpp`）。

```
spread != 0：
  SpreadUtils 光栅化白色 outset-RRect      →  alpha 纹理     (pass 1)
  → GaussianBlur1D 横向                     →  中间纹理       (pass 2)
  → GaussianBlur1D 纵向                     →  中间纹理       (pass 3)
  → XfermodeFragmentProcessor(SrcIn) 上色，随 drawImage 合入
```

blur 走可分离两 1D pass 架构（`GaussianBlurImageFilter.cpp` 中 `Blur1D` 调两次），sigma 上限 `MAX_BLUR_SIGMA = 10`，超出靠缩小 render target 近似。

**核的实际形态**：`GLSLGaussianBlur1DFragmentProcessor::emitCode` 中，`radius = int(ceil(2.0 * sigma))`，逐 tap 权重 `exp(-i*i / (2*sigma*sigma))`，最后 `sum / total` 按累加权重重新归一化（`GLSLGaussianBlur1DFragmentProcessor.cpp:58-75`）。即现状核**不是无限支撑高斯，而是 ±2σ 截断后重新归一化的高斯**；且 tap 取整数像素偏移，σ 较小时对核欠采样。与之匹配，`filterBounds` 按 `2σ` outset（`GaussianBlurImageFilter.cpp:184`），恰好等于截断半径。

**结论**：DropShadow + spread 约 3 个 render pass + 2~3 张中间纹理。

### 2.2 InnerShadow 渲染管线

关键路径：`InnerShadowStyle::drawWithSpread`（`src/layers/layerstyles/InnerShadowStyle.cpp:143`）。

```
spread != 0：
  MakeSpreadShapeImage(input, 0)       → shape image   (光栅化 pass)
  MakeSpreadShapeImage(input, -spread) → mask image    (光栅化 pass)
  → ImageFilter::Blur 对 mask 模糊      → 1~2 blur pass
  → MaskFilter::MakeShader 反相 mask + ColorFilter(SrcIn) 上色
```

**结论**：InnerShadow + spread 约 4~5 个 render pass + 多张中间纹理，是当前最重的形状效果路径，单 shader 优化收益最大。

### 2.3 StyledShape / contentShape 机制

spread 参数与几何来源已在前期落地（`0527-阴影支持扩散能力` 方案）：

- `StyledShape`（`include/tgfx/layers/layerstyles/StyledShape.h`）：封装 spread 的几何源（shape + fill/stroke 类型 + strokeWidth + strokeAlign）。
- `Layer::getContentShape()` / `onGetContentShape()`：各 Layer 类型交出可识别的单一形状，无法还原时退化为 bounds Rect。
- `ContourInputSource::shape()`（`include/tgfx/layers/layerstyles/LayerStyleInput.h:99`）：LayerStyle 在 `onDraw` 中把 `input.extraSource` 下转为 `ContourInputSource` 后取回 `const std::optional<StyledShape>&`。
- `SpreadUtils`（`src/layers/SpreadUtils.cpp`）：`path.isOval` / `path.isRRect` / `path.isRect` 分派，`MakeSpreadRRect` 做几何 outset / inset。

**contentShape 与 contour 图像的覆盖范围不一致**：`ContourInputSource` 同时携带两份数据，但二者的内容范围不同。contour 图像由 `getContentContourImage` 录制**整棵子树**（含子图层）；而 `contentShape` 来自各Layer 的 `onGetContentShape()`，各实现只描述**该图层自身**的形状——`ShapeLayer` 交出 `_shape->getPath()`（`ShapeLayer.cpp:253-274`）、`SolidLayer` 交出自身矩形（`SolidLayer.cpp:80-93`）、`VectorLayer` 交出自身 contents（`VectorLayer.cpp:85-156`），均不含`_children`。因此含可见子图层的图层，其 `contentShape` 小于 contour 图像的实际内容范围。

现状消费方中只有 `MakeSpreadShapeImage` 用 `contentShape`，且它是 spread ≠ 0 时的阴影源——也就是说**当前 spread ≠ 0 的阴影本来就只按图层自身形状生成、忽略子图层内容**，该行为是既有语义；spread = 0 时阴影源为 `input.content`（完整内容图像），是包含子图层的。

### 2.4 AA 处理

阴影内容源的抗锯齿处理按来源分为两类，且与直觉相反：**多数路径刻意不要 AA**。

- **`input.content`（OpaqueContext 生成，spread = 0 的阴影源）**：`OpaqueContext` 专为 LayerStyle 收集内容，把内容录制成**硬边剪影**——纯色绘制用 `Brush(Color::White(), BlendMode::SrcOver, /*antiAlias=*/false)`，非纯色 shader 加 `ColorFilter::AlphaThreshold` 把半透明量化为二值（`src/layers/OpaqueContext.cpp:329-340`）。刻意消除 AA 的原因：多笔绘制叠加时半透明边缘互相混合会产生接缝 artifact；且阴影后续要经高斯模糊（带宽远大于 1 px），源图有无 1 px AA 对模糊结果的影响不可感知。
- **`SpreadUtils` 光栅化（spread ≠ 0 的阴影源）**：`DrawSpreadRRect` 用 `paint.setAntiAlias(true)`（`src/layers/SpreadUtils.cpp:81`），形状边缘带 1 px 线性 AA。

TGFX 的形状 AA 统一为**线性过渡**：`clamp(0.5 - t * invlen, 0, 1)`（`src/gpu/glsl/processors/GLSLEllipseGeometryProcessor.cpp:85`，1 px 过渡带内覆盖率线性变化），无三次曲线过渡的实现。另外 `DropShadowStyle` 在 blur = 0 时把阴影图采样切换为 nearest（`src/layers/layerstyles/DropShadowStyle.cpp:143-146`），避免线性插值把边缘灰度糊化成可见描边。

各路径最终边缘状态：

| spread | blur | 阴影源边缘 | 最终阴影边缘 |
|--------|------|-----------|-------------|
| = 0 | > 0 | 硬边 | 高斯晕开（平滑） |
| = 0 | = 0 | 硬边 | 硬边（现状即无 AA） |
| ≠ 0 | > 0 | 1 px 线性 AA | 高斯晕开（平滑） |
| ≠ 0 | = 0 | 1 px 线性 AA | 光栅化 AA 保留 |

## 3 参考设计

对于能用代数式描述的简单形状（圆角矩形、椭圆），业界（Figma、部分浏览器）有另一条路线：在单个 fragment shader 内用**解析 SDF** 计算像素到形状边界的距离，再用高斯核的**闭式积分**直接算出模糊后的覆盖率，把形状求值、扩散（spread）、模糊、上色一次算完——单 pass、零中间纹理。

### 3.1 数学依据

阴影在语义上是**图层轮廓形状的纯色填充**——被模糊的信号是形状的二值 mask（内 1 外 0），而非图层的像素颜色，这正是该信号能被解析表达的前提（若模糊对象是任意彩色图像，信号无闭式表达，此路线不成立）。因此阴影模糊在数学上是**形状示性函数 `f`（形状内为 1、外为 0 的二值函数）与二维高斯核的卷积**。TGFX 的对外 API 提供两个独立标准差 `blurrinessX` / `blurrinessY`，故核为轴对齐的各向异性高斯：

```
C(p) = (f ∗ Gσ)(p) = ∬ f(q) · Gσ(p − q) dq
Gσ(x,y) = 1/(2πσxσy) · exp(−x²/(2σx²) − y²/(2σy²))
```

`Gσ` 即二维正态分布的概率密度，全空间积分为 1（权重归一，模糊前后图像总量不变，平坦区域不被压暗）。σx = σy 时退化为各向同性核。该路线的可行性建立在三条标准事实上：

1. **高斯可分离**：`Gσ(x,y) = gσx(x)·gσy(y)`（轴对齐，两轴各自独立），二维卷积可按 Fubini 定理拆成两个一维积分的嵌套。其中一维高斯概率密度为

   ```
   gσ(t) = 1/(σ√(2π)) · exp(−t²/(2σ²))
   ```

   **归约到各向同性**：把坐标按 `(x/σx, y/σy)` 缩放后，核成为 σ = 1 的各向同性单位高斯，形状则被非等比拉伸。该变换是精确的（不是近似）：各向异性高斯卷积在此缩放坐标系下严格等价于各向同性卷积，因为缩放同时作用于核与形状，且雅可比行列式常量可被归一化吸收。代价是形状的正圆角在缩放后变为**椭圆角**（x 半轴 `r/σx`、y 半轴 `r/σy`），故后文的半宽公式必须按椭圆弧给出、而非正圆弧。

2. **一维高斯积分有闭式**：累积分布函数 `Φσ(u) = ∫₋∞ᵘ gσ(t)dt = ½[1 + erf(u/(σ√2))]`，区间质量 `∫ₐᵇ gσ = Φσ(b) − Φσ(a)`。被卷积的 `f` 有限支撑——形状外恒为 0、对卷积无贡献，积分区间天然有限。

   其中误差函数 `erf(z) = (2/√π) ∫₀ᶻ exp(−t²) dt`：`exp(−t²)` 无初等原函数，erf 即以该积分定义的标准特殊函数，为奇函数（`erf(−z) = −erf(z)`，取值 (−1, 1)）。

   **shader 侧无内置 erf**：GLSL / WGSL 均无（SPIR-V `GLSL.std.450` 亦无）；仅 Metal 的 MSL 提供 `metal::erf`，其实现闭源不可知，跨后端不可依赖，故 erf 必须手写。

   erf 的求值方法在 NIST DLMF [第 7 章](https://dlmf.nist.gov/7)有全集式收录，共六类：泰勒级数（[§7.6](https://dlmf.nist.gov/7.6)）、连分式（[§7.9](https://dlmf.nist.gov/7.9)）、渐近展开（[§7.12](https://dlmf.nist.gov/7.12)）、初等函数逼近（[§7.24(i)](https://dlmf.nist.gov/7.24#i)）、Chebyshev 级数（[§7.24(ii)](https://dlmf.nist.gov/7.24#ii)）、Padé 逼近（[§7.24(iii)](https://dlmf.nist.gov/7.24#iii)）。其中只有**初等函数逼近**适合 fragment shader——它由初等函数（多项式、有理式、`exp`、根号等）组成闭式求值式、成本固定；其余五类各有硬伤：泰勒级数在 \|z\| 稍大时因交错项相消而浮点失效（z=5 即耗尽 float32 的 7 位有效数字），连分式与渐近展开需迭代或按 \|z\| 变成本，Chebyshev 与 Padé 的系数表过大。

   DLMF §7.24(i) 收录 Hastings (1955) 与 Cody (1969) 两支，面向不同精度需求：

   | 方案 | 结构 | 系数 | 单次成本 | 数据分支 | 最大 erf 误差 | CDF 差误差（灰度级） |
   |------|------|------|---------|------|--------------|-----------|
   | A&S 7.1.26 | 单式：`t = 1/(1+px)`，`1 − P(t)·exp(−x²)` | 6 | 1 exp + 1 除 + 5 乘加 | 无 | 1.4e-7（公布界 1.5e-7） | 0.000 |
   | A&S 7.1.27 | 单式：`1 − 1/(1+a₁x+a₂x²+a₃x³+a₄x⁴)⁴`，纯有理式 | 4 | 1 除 + 4 乘加 | 无 | 4.7e-4（公布界 5e-4） | 0.124 |
   | A&S 7.1.27 去 a₃ 项 | 同上，省去 a₃ = 0.000972（Wallace shader 所用） | 3 | 1 除 + 3 乘加 | 无 | 8.2e-4 | 0.219 |
   | Cody (1969) | 分段：按 \|x\| 分 4 段，各段独立有理式，大 x 段另含 exp | 52 | 9~15 乘加 + 1~2 除 | 2~3 次区间比较 | ~1e-17 | ≈ 0（机器精度级） |

   两式均出自 Hastings (1955)，收录于 A&S 式 7.1.25~7.1.28（p. 299，页脚注 2 标注出处），适用域均为 `0 ≤ x < ∞`。Cody 的实测对象为 fdlibm [`s_erf.c`](https://netlib.org/fdlibm/s_erf.c)（文件头自述精度 2⁻⁵⁷·⁹ ~ 2⁻⁵⁹·¹）；开源的 Cephes（[`ndtr.h`](https://github.com/scipy/xsf/blob/main/include/xsf/cephes/ndtr.h)）与 fdlibm 的 erf 实现均属 Cody 风格的分段有理逼近。

   两列误差的含义：**最大 erf 误差**为 `max|逼近(x) − erf(x)|`，即公式本身的逐点最差精度；**CDF 差误差**为误差传导到一次截断 CDF 差求值（shader 的实际消费形式）后的可观察量，以 8-bit 灰度级计（数值 = 绝对误差 × 255）。实测后者约为"最大 erf 误差 × 255"的 1.05 倍，放大来自截断归一化除以 `MASS ≈ 0.9545`；两次 erf 求值的误差在最差参数组合处近似相等而抵消，未出现 2 倍叠加。

   关键差异：7.1.27 一族**不需超越函数求值**，是纯有理式；精度虽比 7.1.26 低约三个数量级，但传导到可观察量后仅 0.12~0.22 个灰度级。Cody 则以分段分支、52 个系数与 exp 开销换取机器精度，且其数据分支在 SIMT 执行下会因跨边界像素产生分支发散。

3. **卷积线性**：`f = f₁ − f₂ ⇒ C[f] = C[f₁] − C[f₂]`，环形（外圈减内圈）的模糊覆盖率由此成立——形状的差对应卷积的差，两次闭式卷积相减即可。

**核截断对闭式的影响**：上文的高斯密度为理想无限支撑核——密度在任何有限距离外都不为零，因此工程计算无法直接使用它：逐 tap 采样的实现必须把核截断到有限区间，通常取 ±kσ（k 常取 2~3），并按截断区间的质量重新归一化以保持权重总和为 1。现状滤镜即取 k = 2（见 2.1）；紧支撑核（如三角核）天然有限支撑，无此问题。截断后 `Φ` 相应替换为截断核的 CDF：

```
Φ̃σ(u) = [Φσ(u) − Φσ(−kσ)] / [Φσ(kσ) − Φσ(−kσ)]，并在 u ≤ −kσ 取 0、u ≥ kσ 取 1
```

即先按无限支撑高斯的 CDF 求值，再减去左端质量、除以区间总质量——闭式结构不因截断而改变，且截断使积分窗口变窄，有利于后续的数值求积。

以下两式沿用上文的归约：坐标与几何量均已按 `(1/σx, 1/σy)` 缩放，故两轴的 σ 恒为 1、截断支撑恒为 ±k，`Φ̃` 不再带 σ 下标。

**矩形（完全可分离 ⇒ 纯闭式）**：示性函数 `f = 1[x₀,x₁](qx) · 1[y₀,y₁](qy)` 变量分离，二重积分拆为两个一维积分之积，得纯闭式解，O(1)、零采样。两轴各自独立求值，因此**矩形对各向异性零额外代价**：

```
C(p) = [Φ̃(px−x₀) − Φ̃(px−x₁)] · [Φ̃(py−y₀) − Φ̃(py−y₁)]
```

**圆角矩形（逐行可分离 ⇒ 一维闭式 + 常数点数值积分）**（限定原形状四角统一正圆角 `r`）：圆角使 `f` 不再完全分离，但固定一行 `y`，横截面仍是对称区间 `[−w(y), +w(y)]`，半宽解析。缩放后角半径成为椭圆的两个半轴 `rx = r/σx`、`ry = r/σy`（各向同性时二者相等，公式自动退化为正圆弧）：

```
直边区（|y| ≤ halfY − ry）：w(y) = halfX
圆角区（|y| > halfY − ry）：w(y) = halfX − rx + (rx/ry)·√(ry² − (|y| − (halfY−ry))²)
```

按 Fubini 定理把二重积分展开为嵌套积分——固定一行 `qy`，该行横截面为区间 `[−w(qy), +w(qy)]`（`f` 在其上恒为 1），先沿 `qx` 积（内层），再对 `qy` 积（外层）：

```
C(p) = ∫ [ ∫_{−w(qy)}^{+w(qy)} g(px−qx) dqx ] · g(py−qy) dqy
            └──── 内层（沿 x）────┘            └ 外层（沿 y）┘
```

内层是区间 `[−w(qy), +w(qy)]` 上的高斯积分，闭式（CDF 差）；外层因 `w(qy)` 含根号、与 `g` 复合后无初等原函数，做数值积分。把外层积分变量换为相对目标点的核空间偏移 `s = py − qy`，则：

```
C(p) ≈ [ Σᵢ wᵢ · rowSpan(px, py − sᵢ) ] · (Φ̃(end) − Φ̃(start)) / Σᵢ wᵢ
```

其中 `rowSpan(x, y)` 为第 y 行的水平模糊覆盖率（内层闭式 CDF 差 `Φ̃(x + w(y)) − Φ̃(x − w(y))`）；`sᵢ` 为第 i 个采样点在核空间中的偏移——中点求积：`step = (end − start)/N`，`sᵢ = start + (i + 0.5)·step`（i = 0..N−1），积分区间 `[start, end]` 为核支撑窗口与形状纵向范围的交集；`wᵢ = exp(−sᵢ²/2)` 为该点的核权重（归约后 σ = 1）。`/Σᵢwᵢ` 与 `×(Φ̃(end)−Φ̃(start))` 把积分拆为"rowSpan 的核加权平均值 × 该区间真实核质量"。注意 `wᵢ` 与 `rowSpan` 的第二参数对 `sᵢ` 的用法不同：前者按核空间偏移取权重，后者需要绝对行坐标 `py − sᵢ`。两个轴的误差类型不同：**x 轴（内层）**为闭式 CDF 差，无采样误差，仅有所用求值方法带来的计算误差；**y 轴（外层）**为 N 点采样加权和，存在采样误差，其大小由 rowSpan 沿 y 的变化幅度决定。

**求积点数 N 与精度的关系**：N 为常数即可，与 σ 及形状尺寸无关——这是该路线"常数时间"的来源。但 N 所需的大小取决于截断范围 `k`：中点求积的误差界随区间长度成立方增长（`(区间长度)³/(24N²)·max|f″|`），窗口越宽，同样的 N 摊得越薄、误差界越高。因此 N 与 k 必须一起选定，单独引用其中一个的取值没有意义。

**推广复杂度**：上文的半宽公式已按椭圆弧给出（各向异性归约的必然结果），因此**原形状本身带椭圆角（`rx ≠ ry`）不需要额外推广**——归约后与"正圆角 + 各向异性 σ"落在同一公式上。仍需额外工作的是四角半径各异的情形：

- **四角半径各异（per-corner）**：横截面仍是区间，但左右边界各自由本行角半径决定，`w(y)` 从单一对称半宽变为按 y 区间分段的双边界；左右角不等时形状不再关于中心对称，无法用 `abs()` 折叠象限，shader 分支与每行计算量均增加。

**描边形状的等距曲线限制**：椭圆弧的等距曲线（offset）不再是椭圆——描边椭圆的内外边界无法用"两组半径各自偏移描边厚度"的椭圆表示（仅正圆的等距曲线仍为正圆）。因此"外圈减内圈"的双圈闭式对描边椭圆只是同心近似，而非精确表达；矩形描边无此问题（inset / outset 精确）。

**抗锯齿与模糊的统一视角**：AA 与阴影都是对同一 {0,1} 理想形状的卷积，仅核带宽不同——AA = 卷约 1 px 窄核，阴影 = 卷 σ 量级的宽核。二者在同一解析边界上并存，故支持 AA 不破坏闭式性。

### 3.2 Figma 阴影 shader 实现

Figma 的 `RoundedRectangleBlur` / `RoundedRectangleInnerShadow` fragment shader 源码已从其渲染引擎 WASM 中提取。核心思路与 Evan Wallace 的公开方法同源——**一维分段闭式积分、另一维固定 4 采样积分（积分轴按目标点位置自适应）**，常数时间、单 pass；本文以 WASM 提取源码为准，Evan Wallace 公开文章仅作同源背景参考。以下为去 minify（变量还原语义名）后的源码。

**Figma 内外阴影的实现方式与派发条件：**

| 效果 | 方式 | shader | 适用条件 | 核心 |
|------|------|--------|---------|------|
| Drop shadow | 闭式单 pass | `RectangleBlur` | 轴对齐矩形（fill） | `axisAlignedCoverage`：两方向 CDF 差相乘 |
| Drop shadow | 闭式单 pass | `RoundedRectangleBlur` | 统一正圆角 RRect（fill） | `roundedRectShadow`：`rowSpan` + 固定 4 采样 |
| Drop shadow | 通用路径 | `MaskKnockoutShadowTile` + 渐进模糊链 | 其余形状（per-corner 半径、corner smoothing、任意 path） | 形状画成 mask tile → 模糊 |
| Inner shadow | 闭式单 pass | `RectangleInnerShadow` | 轴对齐矩形（fill） | `(1 − axisAlignedCoverage)` × 形状 mask（`useAAAMask` 开 = SDF + AA，关 = quad 几何硬边） |
| Inner shadow | 闭式单 pass | `RoundedRectangleInnerShadow` | 统一正圆角 RRect（fill） | `(1 − roundedRectShadow)` × 形状 mask（`useAAAMask` 开 = SDF + AA，关 = SDF 硬边） |
| Inner shadow | 通用路径 | `MaskInnerShadowTile` + 渐进模糊链 | 其余形状 | 形状画成 mask tile → 模糊 |

四个要点：

1. **命名辨析**：`RectangleBlur` / `RoundedRectangleBlur` 虽以 Blur 命名，实为 **Drop shadow 对可解析形状的闭式实现**——阴影效果的核心动作即模糊（offset 由坐标 transform 承担、`color` 即阴影色），故 Figma 未再设 `*DropShadow*` 命名的 shader。同一 `roundedRectShadow` 也内置于 `RoundedRectangleInnerShadow`，是阴影闭式 shader 的共用组件。
2. 派发条件的引擎侧佐证：strings 中存在 fill 分类标志 `isAxisAlignedRectFill`（轴对齐矩形）、`isSimpleRoundedRectFill` / `isAxisAlignedSimpleRoundedRectFill`（简单圆角矩形）、`isSimpleCircleFill`（正圆）、`preRasterizeCustomEffectFill`（预光栅化自定义效果，即通用路径入口）。正圆由圆角闭式 shader 以 `corner = halfSize` 覆盖。
3. **AA 需求的不对称**：Drop shadow 的 shader 无 `useAAAMask`——其唯一可见边缘是模糊衰减边（三角核支撑 ±1 归一化单位，blur > 0 时远超 1 px），天然平滑、无需 1 px AA；Inner shadow 多出一条不经模糊的形状裁剪边（mask 硬切），故设 `useAAAMask` 控制其 AA / 硬边。blur ≈ 0 时相反：Drop shadow 的衰减边退化为硬边且无 AA 可救，Inner shadow 的 mask 边仍可选 AA。
4. **stroke（描边）形状的阴影**：Figma 如何实现无法从 strings 确认——无 stroke 专用闭式变体，fill 分类标志亦不含 stroke；但理论上可用"外圈减内圈"**两次调用同一闭式 shader** 实现，成本 2×4 采样、单 pass 量级，仍远低于通用路径（mask 光栅化 + 多级模糊），即性能优化对 stroke 同样成立。

下文依次给出：共享函数链、四种闭式 shader 的源码、通用路径的机制轮廓。

**与 3.1 数学依据的对应**：`gaussianCDF` ↔ 核的 CDF（此处为三角核的精确 CDF）；`triWeight` ↔ 外层求积权重 `wᵢ`；`rowSpan` ↔ 内层闭式积分；`roundedRectShadow` ↔ 外层 4 点求积；`axisAlignedCoverage` ↔ 矩形纯闭式解。

**Figma 的高斯 CDF 实为三角核的精确 CDF（非 erf）：**

`gaussianCDF` 名义上近似高斯累积分布，实则为**三角核的精确 CDF**——对其求导即得 `triWeight`。即 Figma 阴影本质是三角核（tent kernel）模糊而非高斯：三角核紧支撑 [−1, +1]、天然归一（面积 = 1），CDF 为分段二次多项式——全 shader 无 exp / erf，成本最低、导数连续：

```glsl
// gaussianCDF：三角核（triWeight）的精确 CDF，分段二次多项式，定义域外裁剪到 [0,1]
float gaussianCDF(float x) {
  if (x < -1.0) return 0.0;
  else if (x < 0.0) return 0.5 * (x + 1.0) * (x + 1.0);
  else if (x < 1.0) return 0.5 - 0.5 * x * (x - 2.0);
  else return 1.0;
}

// triWeight：采样权重用三角窗，而非 exp 高斯核
float triWeight(float x) {
  return max(0.0, 1.0 - abs(x));
}
```

**单行的圆角矩形水平覆盖：**

给定一行（相对中心归一化坐标），算该行圆角矩形的半宽，再用 `gaussianCDF` 的差得到该行被模糊后的水平覆盖率。所有坐标已按 blurRadius 归一化：

```glsl
// rowSpan：point 在某一行、圆角矩形半宽由 halfSize/corner 决定时的水平模糊覆盖
//   x       该点横坐标
//   y       该点纵坐标（决定处于圆角区还是直边区）
//   corner  归一化角半径
//   halfX/halfY  归一化半尺寸的两个分量
float rowSpan(float x, float y, float corner, float halfX, float halfY) {
  float delta = min(halfY - corner - abs(y), 0.0);
  float curved = halfX - corner + sqrt(max(0.0, corner * corner - delta * delta));
  return gaussianCDF(x + curved) - gaussianCDF(x - curved);
}
```

**圆角矩形阴影覆盖率（自适应选轴积分）：**

Figma 按 `abs(coord.x) > abs(coord.y)` 选择**目标点分量较小的轴**做 4 采样积分（Wallace 固定沿 Y）：分量较小的轴形状范围相对居中，外层积分区间更可能满窗，避免 Wallace 在点沿 Y 偏远时区间窄化 / 夹空（`start == end`）造成的采样浪费。选轴只影响采样效率、不影响结果——两轴交换仅为 Fubini 换序，且某轴 `rowSpan` 恒定时外层退化为对常数积分、中点法精确；长宽悬殊时按绝对分量未必选中满窗轴，正确性不变。最后用 `(gaussianCDF(end)-gaussianCDF(start)) / Σweight` 归一化：

```glsl
// roundedRectShadow：coord 为相对中心、按 blurRadius 归一化的坐标
//   halfSize / corner 同样已按 blurRadius 归一化
float roundedRectShadow(vec2 coord, vec2 halfSize, float corner) {
  const int N = 4;                       // 固定 4 采样，与 sigma / 半径无关
  float weightSum = 0.0;
  float accum = 0.0;
  float lo, hi, start, end, step, s;
  if (abs(coord.x) > abs(coord.y)) {     // 沿 Y 积分
    lo = coord.y - halfSize.y;
    hi = coord.y + halfSize.y;
    start = clamp(-1.0, lo, hi);
    end   = clamp( 1.0, lo, hi);
    if (start == end) return 0.0;
    step = (end - start) / float(N);
    s = start + step * 0.5;
    for (int i = 0; i < N; i++) {
      float wgt = triWeight(s);
      accum    += rowSpan(coord.x, coord.y - s, corner, halfSize.x, halfSize.y) * wgt;
      weightSum += wgt;
      s += step;
    }
  } else {                               // 沿 X 积分（对称写法，交换 xy）
    lo = coord.x - halfSize.x;
    hi = coord.x + halfSize.x;
    start = clamp(-1.0, lo, hi);
    end   = clamp( 1.0, lo, hi);
    if (start == end) return 0.0;
    step = (end - start) / float(N);
    s = start + step * 0.5;
    for (int i = 0; i < N; i++) {
      float wgt = triWeight(s);
      accum    += rowSpan(coord.y, coord.x - s, corner, halfSize.y, halfSize.x) * wgt;
      weightSum += wgt;
      s += step;
    }
  }
  return accum * (gaussianCDF(end) - gaussianCDF(start)) / weightSum;
}
```

**绘制区域的范围扩展**：模糊会把影响范围推出形状边界——三角核紧支撑 [−1, +1]（归一化单位），物理上即形状 bounds 向外扩展 **1 × blurRadius**：宽高 100、blurRadius = 10 的矩形，阴影绘制区域（quad）为 120 × 120（再叠加 offset 平移）。归一化式 `coord = (fragPos − center) / blurRadius` 的除数即支撑半径，故核的外缘恰好落在 quad 边界上。（quad 由 CPU 侧计算，WASM 中不见其算式；扩展量由核支撑范围推得。）

**uniform 归一化约定**：Figma 两组 shader 的归一化几何量对应为 `halfSizeOverBlurRadius = halfSize / blurRadius`（`RoundedRectangleInnerShadow` 中即 `shadowHalfSize * invBlurRadius`）、`cornerRadiusOverBlurRadius = corner / blurRadius`（即 `shadowCornerRadius * invBlurRadius`），坐标同样归一化（`coord = (fragPos - center) / blurRadius`）。归一化后 shader 内核宽恒为 1，无需单独传核尺度参数，也天然无模糊半径上限。**spread 在 CPU 端 outset 进 `halfSize` / `corner` 后再归一化，shader 不感知 spread。**

**内部实心区跳过**：归一化单位下三角核的有效支撑为 [−1, +1]（见上文 `gaussianCDF`），点离任意边界超过 1 个单位时核完全不跨边界、覆盖率恒为 1，故方式 2 主体以 `abs(coord) < halfSizeOverBlurRadius − (cornerRadiusOverBlurRadius + 1.0)` 判定"深入内部"、跳过积分；`+ corner` 则因圆角弧把最坏情况的行半宽内推至 `halfX − corner`，须按最窄行留余量（充分偏保守，圆角区附近少量满覆盖点仍走积分路径，结果不变）。该判据中的常数 `1.0` 即核支撑半径，随核而定。

采样数固定为 4，**与模糊半径无关**——大半径下采样成本恒定，也无需 JFA 距离场。该实现用单一 `corner`（各角同半径），每角独立半径的 RRect 不走此 shader。

**方式 1：RectangleBlur（矩形 DropShadow 闭式）：**

轴对齐矩形的阴影闭式 shader，核心为 `axisAlignedCoverage`——两方向各取一次 CDF 差相乘，零采样（对应 3.1 矩形纯闭式解）：

```glsl
// axisAlignedCoverage：edges 为矩形四边在目标点坐标系下、按 blurRadius 归一化的坐标（顶点插值传入）
float axisAlignedCoverage(vec4 edges) {
  return (gaussianCDF(edges.x) - gaussianCDF(edges.z))
       * (gaussianCDF(edges.y) - gaussianCDF(edges.w));
}

// RectangleBlur main：uniform 仅 vertexTransform / quad / color / blurRadius（无角半径）
void main() {
  float coverage = axisAlignedCoverage(normEdges);
  fragColor = dither(color * coverage);
}
```

**方式 2：RoundedRectangleBlur（圆角矩形 DropShadow 闭式）——主体：**

内部实心区直接取满覆盖（跳过积分），只在边缘带走 `roundedRectShadow`：

```glsl
// RoundedRectangleBlur main：uniform 为 vertexTransform / quad / color / halfSizeOverBlurRadius / blurRadius / cornerRadiusOverBlurRadius
void main() {
  float coverage;
  if (all(lessThan(abs(coord), halfSizeOverBlurRadius - vec2(cornerRadiusOverBlurRadius + 1.0)))) {
    coverage = 1.0;                      // 深入内部，必然全覆盖，省去积分
  } else {
    coverage = roundedRectShadow(coord, halfSizeOverBlurRadius, cornerRadiusOverBlurRadius);
  }
  fragColor = dither(color * coverage);  // dither：加微小噪声抗 banding
}
```

**InnerShadow 辅助函数（真实定义）：**

InnerShadow 主体引用的图层形状 SDF 与抗锯齿覆盖，在提取源码中的定义如下。`roundRectSDF` 由轴对齐矩形 SDF 减去角半径做圆角化，角半径 clamp 到不超过半尺寸；`aaCoverage` 把 SDF 按 `fwidth` 归一化到像素单位，在 ±0.5 px 过渡带内做 `smoothstep`：

```glsl
// boxSDF：轴对齐矩形 SDF，p 相对中心（外正内负）
float boxSDF(vec2 p, vec2 halfSize) {
  vec2 d = abs(p) - halfSize;
  return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
}

// roundRectSDF：圆角矩形 SDF = boxSDF(halfSize - r) - r，r clamp 到半尺寸内
float roundRectSDF(vec2 p, vec2 halfSize, float corner) {
  float r = min(corner, min(halfSize.x, halfSize.y));
  return boxSDF(p, halfSize - r) - r;
}

// aaCoverage：SDF 按 fwidth 归一化到像素单位后，在 ±0.5 px 过渡带内平滑
float aaCoverage(float sdf) {
  float w = max(fwidth(sdf), 0.00001);
  return 1.0 - smoothstep(-0.5, 0.5, sdf / w);
}
```

**方式 3：RectangleInnerShadow（矩形 InnerShadow 闭式）：**

复用方式 1 的 `axisAlignedCoverage`，反相后乘图层矩形的 AA mask：

```glsl
// RectangleInnerShadow main：uniform 为 vertexTransform / shadowBounds / maskBounds / color / blurRadius / useAAAMask
void main() {
  float coverage = 1.0 - axisAlignedCoverage(normShadowEdges);
  if (bool(useAAAMask)) {
    // 图层矩形（maskBounds）覆盖率：per-corner SDF 传零半径（即矩形），经 fwidth AA
    vec2 maskHalfSize = maskBounds.zw - maskBounds.xy;
    float maskSDF = perCornerRectSDF(canvasPos - maskBounds.xy, vec4(0.0, 0.0, maskHalfSize), vec4(0.0));
    coverage *= aaCoverage(maskSDF);
  }
  fragColor = dither(color * coverage);
}
```

`useAAAMask` 关闭时不乘 mask——矩形 quad 的绘制几何本身即裁剪范围。

**方式 4：RoundedRectangleInnerShadow（圆角矩形 InnerShadow 闭式）——主体：**

InnerShadow 复用同一 `roundedRectShadow`，取反后乘图层原形状覆盖率，把阴影裁进形状内部：

```glsl
// RoundedRectangleInnerShadow main：uniform 为 vertexTransform / shadowBounds / maskBounds / color
//   / maskHalfSize / shadowHalfSize / blurRadius / maskCornerRadius / shadowCornerRadius / invBlurRadius / useAAAMask
// maskBounds/maskCornerRadius 描述图层原形状（裁剪范围），
// shadowBounds/shadowCornerRadius 描述 spread 形变后的穿透形状。
void main() {
  // 阴影穿透区取反：形状内、且不在 spread 穿透区
  float shadowCoverage = 1.0 - roundedRectShadow(
      centeredNormShadowCoords, shadowHalfSize * invBlurRadius, shadowCornerRadius * invBlurRadius);
  // 图层原形状覆盖率：用圆角矩形 SDF，useAAAMask 时走 fwidth 抗锯齿，否则硬边 step
  float maskSDF = roundRectSDF(centeredMaskCoords, maskHalfSize, maskCornerRadius);
  float maskCoverage = bool(useAAAMask) ? aaCoverage(maskSDF) : 1.0 - step(0.0, maskSDF);
  fragColor = dither(color * shadowCoverage * maskCoverage);
}
```

Figma 用独立的圆角矩形 SDF（`roundRectSDF`）算图层形状覆盖率，`useAAAMask` 控制是否做 `fwidth` 抗锯齿。

**Figma 的 AA 策略归纳**：

- **Drop shadow（方式 1/2）**：唯一可见边缘是闭式积分的三角核衰减边（支撑 ±1 归一化单位），天然平滑，shader 无任何 AA 过渡函数——blur ≈ 0 时衰减边退化为硬边，且无 AA 可救。
- **InnerShadow（方式 3/4）**：多出一条不经模糊的形状裁剪边（mask），由 `useAAAMask` 控制其 AA，且矩形与圆角矩形的"关"分支不同：矩形关 = 无 mask（quad 几何与形状同边界、免费裁剪）；圆角矩形关 = SDF 硬边（圆角轮廓在 quad 内，mask 不可省略）。两者"开"分支同为 `aaCoverage`（`fwidth` 归一 + `smoothstep` 三次过渡）。

**方式 5 / 6：通用路径（mask tile + 渐进模糊链）：**

不满足闭式条件的形状（per-corner 半径、corner smoothing、任意 path）走通用路径。本方案不改动 TGFX 的回落路径，故此处只记录机制轮廓，不展开源码。

大步骤：形状光栅化为 alpha 纹理 → 对 mask 做**多级降采样链**，每级 1D 三角核卷积（横 + 纵两 pass，`GradientBlurTile`）→ 相邻两级经 `UpscaleAndBlendGradientBlurTile` 级联混合 → 按需镂空（Drop 用 `MaskKnockoutShadowTile`，Inner 用 `MaskInnerShadowTile` 反相 + 乘形状 mask）→ 与图层内容合成。Drop 与 Inner 共用同一条模糊链，差异仅在末端 mask：

| 环节 | Drop shadow（方式 5） | Inner shadow（方式 6） |
|------|------|------|
| 光栅化 + 渐进模糊 | 同 | 同 |
| 末端 mask | `MaskKnockoutShadowTile`：阴影需镂空时挖空形状内部 | `MaskInnerShadowTile`：反相模糊 + 乘形状 mask，把阴影裁进形状内 |
| 对应闭式公式 | `shadowColor × coverage` | `shadowColor × (1 − coverage) × maskCoverage` |

**两路径的对称性**：Drop shadow 的镂空不进入模糊计算——闭式（合成时内容遮盖或叠加 knockout）与通用路径在末端同构；Inner shadow 的 mask 是效果核心——闭式内联进单 pass（`maskCoverage` 因子 + `useAAAMask`），通用路径外置为独立步骤。

**与 TGFX 现状模糊实现的差异**：TGFX 为整图两 1D pass（横 + 纵）、exp 高斯核、采样窗口 ⌈2σ⌉，σ 超 `MAX_BLUR_SIGMA = 10` 靠缩 render target 一级近似（大半径降质）；Figma 通用路径 pass 数更多（约 10 vs 3~4），但降采样链上像素指数衰减、每级采样数与半径解耦，总采样量可控，换来无 σ 硬上限与级间模糊度连续（并支撑 progressive blur），而非 TGFX 式的截断降质。其核与闭式路径同为三角核，故 Figma 全局核形状一致。

**证据边界**：级数、降采样倍率、混合次数由 CPU 侧编排（WASM 不可见），上述分级策略为结合 shader 行为的推断；shader 层面已确认的事实为两点——`GradientBlurTile` 是单方向 1D 三角核卷积（故每级需横 + 纵两次调用）、`UpscaleAndBlendGradientBlurTile` 一次混两个纹理且权重逐像素由 gradient / count 给出。此外 `MaskKnockoutShadowTile` / `MaskInnerShadowTile` **仅有注册表中的名字、未提取到源码**——WASM 中 shader 名与源码分离存放，无可读的"名字 → 源码"链接，调试 name section 已剥离；现有"源码 ↔ shader 名"对应均靠源码内部特征（uniform 语义 + 代码结构）辨认，这两个 shader 特征未知、无法从 127 个 uniform 块中指认。

### 3.3 Figma WASM shader 源码提取

Figma 渲染引擎 WASM（`compiled_wasm.wasm.br`）**内嵌了完整的 shader 源码文本**（GLSL-ES3 / WebGL1 / WGSL 三套变体，变量名经 minify 压缩为单字母，但结构完整可读），并非编译后的字节码。该文件实为未压缩的 WASM 二进制（文件头即 `\0asm` 魔数，`.br` 后缀名不副实），直接 `strings` + 结构化切分即可拿到内外阴影 shader 的完整源码。提取产物：

| 文件 | 内容 |
|------|------|
| `~/Desktop/Work/资料/FigmaSource/figma_shaders/RectangleBlur.frag.glsl` | `RectangleBlur`（矩形 Drop）GLSL-ES3 fragment |
| `~/Desktop/Work/资料/FigmaSource/figma_shaders/RoundedRectangleBlur.frag.glsl` | `RoundedRectangleBlur`（圆角 Drop）GLSL-ES3 fragment 全文 |
| `~/Desktop/Work/资料/FigmaSource/figma_shaders/RectangleInnerShadow.frag.glsl` | `RectangleInnerShadow`（矩形 Inner）GLSL-ES3 fragment |
| `~/Desktop/Work/资料/FigmaSource/figma_shaders/RoundedRectangleInnerShadow.frag.glsl` | `RoundedRectangleInnerShadow`（圆角 Inner）GLSL-ES3 fragment 全文 |
| `~/Desktop/Work/资料/FigmaSource/figma_shaders/ShadowShaders_all.txt` | 4 个闭式 shader 的多语言变体合集（Rectangle 系列为 GLSL-ES3 + WebGL1，其 WGSL 变体混于 shadow_shaders.txt 暂未分离；Rounded 系列为 WGSL / WebGL1 / GLSL-ES3 三变体） |
| `~/Desktop/Work/资料/FigmaSource/shadow_shaders.txt` | 阴影相关的 64 个 shader 块全量：另含 Glass、`GradientBlurTile`、`UpscaleAndBlendGradientBlurTile` 等通用路径 shader 的源码（本方案不改动回落路径，3.2 方式 5/6 只取其机制轮廓，未转录源码） |
| `~/Desktop/Work/资料/FigmaSource/wasm_strings.txt` | raw.wasm 的 strings 全量输出（约 3 MB）：shader 名注册表、uniform 名、fill 分类标志等文本索引 |

除闭式阴影 shader 外，strings 中还发现 Figma 通用阴影/模糊 tile 路径的管线 shader 名：`MaskTile` / `MaskKnockoutShadowTile` / `MaskInnerShadowTile` / `MaskBackgroundBlurTile` / `UpscaleTile`，以及多级渐进模糊链 `GradientBlurTile`（`Clamped` / `Unclamped` / `progressiveBlurUniformControlFlow`）与 `UpscaleAndBlendGradientBlurTile`（机制见 3.2 方式 5/6）。

## 4 对比分析

前两章三家实现的对比：

| 维度 | TGFX 现状（几何 outset + 滤镜） | Evan Wallace 公开方法 | Figma 生产实现 |
|------|------|------|------|
| 模糊核 | 高斯，±2σ 截断后重新归一化 | 高斯，±3σ 截断（仅限外层求积窗口） | 三角核（紧支撑 ±1） |
| 核求值方式 | 逐 tap `exp`，整数像素偏移点采样 | `erf` 用 A&S 7.1.27 去 a₃ 项（纯有理式，无 exp）；外层权重用 `exp` | `triWeight` + `gaussianCDF`（分段二次，无 exp/erf） |
| 每像素采样成本 | 光栅 1 次 + blur 卷积 O(σ) | 固定 4（一维闭式 + 一维 4 采样），积分轴固定沿 Y | 固定 4，积分轴按目标点分量自适应 |
| 大 σ 行为 | `MAX_BLUR_SIGMA = 10`，超出缩 RT 降质 | 无上限 | 归一化无上限，内部实心区跳过积分 |
| 各向异性（σx ≠ σy） | 支持（横纵两 pass 各传自己的 sigma） | 未涉及 | ❌ 不支持：模糊尺度 uniform 为单个 `float blurRadius`，通用 tile 路径亦为标量 `count` |
| Drop / Inner pass | 3 / 4~5 | 1 / 1 | 1 / 1 |
| 中间纹理 | 2~3 张 | 0 | 0 |
| 形状覆盖 | 通用（任意 path / 容器） | 矩形 + 统一正圆角 RRect | 矩形 + 统一正圆角 RRect 闭式；其余走通用 tile 路径 |
| spread | 几何 outset（椭圆 ring 不保真） | 未涉及（几何量直接由 CPU 传入） | CPU 端 outset 进 `halfSize` / `corner`，shader 不感知 |
| stroke | 外扩内缩 ring | 未涉及 | 未涉及 |
| 许可 | — | CC0 | 私有（反向分析所得） |

两条路线的分野在于：闭式路线把 pass 数与采样成本都压成与 σ 无关的常数，代价是只覆盖可解析形状；通用滤镜路线覆盖任意形状，代价是多 pass、中间纹理与随 σ 增长的采样量。两家闭式实现的差别集中在核与求值方式上——Wallace 保留高斯、用纯有理式逼近 erf；Figma 换成三角核，使 CDF 退化为分段二次多项式、连 erf 都不再需要。

**闭式与离散卷积的精度关系**：两者即使取同一个核也不完全等价——闭式求的是卷积积分的解析值，而现状滤镜按整数像素偏移点采样源图，σ 小时对核欠采样。以 1D 阶跃边界为基准，两者在 ±2σ 截断高斯核下的差异为（数值实验，脚本清单见 9.1）：

| σ | 1 | 2 | 3 | 5 | 10 | 20 |
|---|---|---|---|---|----|----|
| 源图为 1 px AA 光栅（灰度级） | 8.8 | 3.7 | 2.4 | 1.4 | 0.7 | 0.4 |
| 源图为硬边剪影（灰度级） | 51.3 | 26.0 | 17.5 | 10.6 | 5.3 | 2.7 |

偏差方是离散卷积（欠采样一侧），且随 σ 增大迅速消失；源图带 AA 时差异显著更小。

## 5 实现方案

### 5.1 总体思路与适用范围

本方案沿用 3.2 闭式路线的结构（内层闭式 + 外层固定点数求积、自适应选轴、内部实心区跳过、spread 在 CPU 端折算），**但核函数取 ±2σ 截断高斯而非 Figma 的三角核**：在阴影绘制末端，当 StyledShape 可解析为 **fill 类型的 Rect / RRect / Oval（四角统一圆角，可为椭圆角；无 corner smoothing）**时，构造对应 FragmentProcessor 单 pass 绘制阴影；stroke 类型及其余形状回落现状滤镜路径。spread 在 CPU 端 outset 进 SDF 参数（复用 `SpreadUtils::MakeSpreadRRect` 的几何折算），shader 不感知 spread。

**范围限定为内外阴影（DropShadow / InnerShadow）**。依据：LayerStyle 是当前唯一能直接拿到图层轮廓 Shape 的位置（2.3 的 StyledShape / contentShape 机制）；且阴影本质是"形状 + 沿轮廓的柔和渐变"，最适合按形状解析处理。基于同一本质的其他效果（Glow、形状整体模糊等）也可同类优化，留待后续。

**核选型：与现状滤镜同为 ±2σ 截断高斯**（即 3.1 中 k = 2 的 `Φ̃`）。不跟随 Figma 三角核的理由：

- **跨路径虚化尺度一致**：快路径（可解析形状）与回落路径（stroke / 任意 path）共用同一个 σ 和同一个核形状，因此阴影的扩散范围与衰减曲线形态一致，同一份设计稿里不同形状的阴影虚化程度可比。若两条路径核形状不同（例如快路径用紧支撑三角核），调用方无法预知某个图层走了哪条路径，却能看到虚化范围与衰减快慢的差异——这是可感知的正确性问题。两路径仍有逐像素差异（源于回落路径的离散化欠采样，见 4），但那是同一个核的求值精度差异，不是核本身不同。Figma 无此约束，其通用 tile 路径同样是三角核（3.2 方式 5/6），全局核一致。
- **σ 语义单一**：对外 API 的 `blurrinessX` / `blurrinessY` 始终是高斯标准差，不引入第二个模糊尺度参数，也不需要跨核换算常数。
- **bounds 零改动**：截断半径 2σ 与现状 `onFilterBounds` 的 `2σ` outset 恰好吻合（2.1），绘制区域外核严格为 0，不会出现被裁切的硬边，因此可直接沿用现有 `filterBounds` 结果。

代价是核求值需 `exp` 与多项式（erf 逼近）而非纯分段二次多项式。该成本落在每次 `Φ̃` 调用上（矩形 4 次、圆角 `2N + 2 = 18` 次，N 见 5.2），仍是与 σ 无关的常数，不改变"单 pass、零中间纹理、常数采样"的量级结论。

**各向异性 σ（σx ≠ σy）走同一套闭式，不回落**。TGFX 的 `blurrinessX` / `blurrinessY` 是两个独立标准差，现状滤镜真实支持（横纵两个 1D pass 各传自己的 sigma，`GaussianBlurImageFilter.cpp:137/151`），故快路径必须同样支持，否则各向异性阴影完全拿不到优化。做法即 3.1 的归约：坐标与几何量按 `(1/σx, 1/σy)` **各向异性缩放**到等向空间，核在两轴上都成为 σ = 1 的单位高斯。这是精确变换而非近似。两类形状的代价不同：

- **矩形**：闭式解本就是两轴独立的 CDF 差相乘，各用自己的 σ 归一化即可，**零额外成本**。
- **圆角矩形**：缩放后正圆角变为椭圆角（半轴 `r/σx`、`r/σy`），`rowSpan` 用 3.1 的椭圆弧半宽公式。σx = σy 时两半轴相等、公式自动退化为正圆弧，因此**只有一条代码路径**，不需要分支判断各向同性与否。成本有两项：`rowSpan` 每次多一次乘法（`rx/ry` 比例因子），以及**求积点数需从 4 提到 8**——归一化会放大形状的等效长宽比，使求积误差上升（实测与取值依据见 5.2）。后者是主要成本。

这一点无法从 Figma 借鉴：其四个闭式 shader 的模糊尺度 uniform 均为单个 `float blurRadius`（`halfSizeOverBlurRadius` 虽是 `vec2`，但除数是标量），全量 strings 中不存在 `blurRadiusX` / `sigmaX` 一类字段，通用 tile 路径的 `GradientBlurTile` 也是横纵共用同一个标量 `count`——Figma 的模糊 API 本身只有单一半径，从未面对该问题。

**快路径限定 spread ≠ 0**。这不是取舍而是正确性要求：闭式求值的几何来自 `contentShape`，它只描述图层自身形状、不含子图层内容（2.3）。spread ≠ 0 时回落路径的阴影源同样只由 `contentShape` 光栅化（`MakeSpreadShapeImage`），两条路径口径一致，快路径不改变阴影的形状语义；而 spread = 0 时回落路径的阴影源是 `input.content`——含子图层的完整内容图像，若改走解析形状，含子图层的图层其阴影会缩小到只剩自身轮廓，是行为回退。

代价是优化覆盖不到 spread = 0 的场景，而这是阴影的多数用法。解除该限制的前提是让 `contentShape` 与contour 图像的覆盖范围一致（见 8.3），届时只需去掉这一条判定。

`_showBehindLayer = false` 时的轮廓遮罩不受此限：遮罩用的是 contour 图像（含子图层），挖掉的正是"图层本体会盖住的部分"，与阴影几何来自何处无关。

由于快路径不再需要 spread = 0 时的 StyledShape，两个 style的 `extraSourceType()` **保持现状不变**（`DropShadowStyle.cpp:110-113` 的 `!_showBehindLayer || spread != 0`；`InnerShadowStyle.cpp:108-111` 的 `spread != 0`）：spread ≠ 0 时本就返回 `Contour`、`Layer::drawLayerStyleDefault` 会构造 `ContourInputSource` 并塞入 `contentShape`（`Layer.cpp:1958-1964`），快路径所需的数据已经就位。若改为一律返回 `Contour`，则 spread = 0 的场景会白白多做一次轮廓录制而无人消费。

### 5.2 数值参数选型

两个自由参数需定值：外层求积点数 N、erf 逼近式。核截断范围 k 已在 5.1 定为 2。

**求积点数取 N = 8**。测定方法：以收敛的高分辨率中点求积（3000~4000 点）作为参考值——加密一倍后变化约 1e-9，远小于被测误差，可作真值看待；误差取 N 点结果与参考值之差的绝对值，在若干代表性形状（正圆、尖角矩形、极端长宽比等）的全部核影响范围内布探针网格取最大值。等向（σx = σy）下的实测最大误差（8-bit 灰度级）为

| k（σ） | N=4 | N=6 | N=8 | N=12 |
|--------|-----|-----|-----|------|
| 2.0（本方案所取，亦为现状滤镜的截断半径） | 2.05 | 1.05 | 0.66 | 0.34 |
| 2.5 | 2.01 | 1.01 | 0.66 | 0.36 |
| 3.0（Wallace shader 所用） | 2.26 | 1.30 | 0.81 | 0.47 |
| 4.0 | 4.68 | 2.64 | 1.74 | 0.96 |

上表为 15×15 探针网格的结果。加密到 21×21（固定 k=2）后 k=2 一列为：N=4 时 2.06、N=6 时 1.07、N=16 时 0.23——探针加密只让最差点被更精确地捕捉到，量级不变。

读表结论：k 从 2 增到 3 时同一个 N 的误差仅小幅上升（N=4：2.05 → 2.26），k=4 起才明显退化（4.68）；同一 k 下 N 加倍约使误差减半；最差点集中在极端长宽比形状上——`halfX/halfY = 10/4` 与 `4/10` 取到 2.06，正圆仅 0.48。

**但上表只覆盖等向情形，各向异性下误差显著更高**。归一化按 `(1/σx, 1/σy)` 逐轴进行，形状的等效长宽比会被 σ 比例放大（例如 40×25 的形状在 σx=16 / σy=4 下变为 2.5 × 6.25，长宽比由 1.6 放大到 2.5），而极端长宽比正是求积误差的最坏场景。以原空间的暴力二维卷积为真值实测：

| 场景（half 40×25，r=10） | σx / σy | N=4 | N=8 | N=32 |
|---|---|---|---|---|
| 等向基线 | 6 / 6 | 0.69 | 0.27 | 0.27 |
| 2:1 | 12 / 6 | 2.44 | 0.79 | 0.27 |
| 4:1 | 16 / 4 | 3.07 | 1.49 | 0.28 |
| 1:3 | 4 / 12 | **3.55** | 0.94 | 0.28 |
| 全圆角 oval（half 30×30，r=30） | 10 / 5 | 2.53 | 0.51 | 0.03 |

N=32 列同时验证了**归约本身是精确的**：各场景均 ≤ 0.51（该值已接近暴力参考解自身的网格精度下限），说明"各向异性缩放到等向 + 椭圆角"没有引入系统偏差，N=4 处的残差是纯求积误差而非归约误差。

因此 **N 取 8**：各向异性下最差 1.49 灰度级，与等向 N=4 的 2.06 同量级；若仍取 4，各向异性最差达 3.55，明显超出可接受范围。代价是圆角路径每像素的 `Φ̃` 调用从 `2N + 2 = 10` 次增到 18 次。这是各向异性支持的真实成本——它不只是换个半宽公式，还抬高了求积点数需求。

**若仅需等向场景可降回 N=4**：如后续实测确认各向异性阴影在实际内容中占比极低，可考虑按 `σx ≈ σy` 分派两套 N（等向 4 / 各向异性 8），代价是 shader 多一个变体或一个分支。本期不做，统一取 8 以保证一条代码路径。

本方案不采用"1 个灰度级"作为硬性容差线——那是**单个孤立像素**上可察觉差异的经验下限，而此处的偏差是空间上缓变的低频量，可察觉阈值显著更高。真正的判据是最终截图上无可见 banding 与轮廓偏移（6.1）。

**erf 逼近取 A&S 7.1.27 完整四系数版**（3.1 候选表第二行）。理由：该式为纯有理式、**不需 `exp`**，而圆角路径每像素调 `Φ̃` 达 `2N + 2 = 18` 次，`exp` 在 GPU 上是多周期特殊函数指令；其 4.7e-4 的精度传导到可观察量后仅 0.124 个灰度级，落在 8-bit 量化噪声以下，故不选精度高三个数量级但需 `exp` 的 7.1.26。保留 a₃ = 0.000972（Wallace 省去了它）：该项使误差从 8.2e-4 降到 4.7e-4、回到公布界 5e-4 内，代价仅一次乘加。公布界已在本仓库复验（实测 4.659e-4，见 6.3）。另一个优点是它对全体 x ≥ 0 有效，不与核截断范围 `k` 耦合。

**若 erf 精度成为瓶颈**（实测出现无法靠加点消除的 banding），升级序列为：先换 7.1.26（同接口同出处，只需替换 `erfApprox` 一个函数，代价是每次调用一个 `exp`）；若 `exp` 开销不可接受，再考虑有界区间拟合多项式（8.8，精度更高且无 exp，但系数需自行拟合、与 `k` 隐式耦合）。

**本方案不做 dither**。Figma 的四个闭式 shader 末尾均有 `dither(...)`（3.2），即叠加约 `1/255 × ±0.5` 的哈希噪声打散色阶。不跟随的理由：TGFX 的 `src/` 与 `include/` 中当前不存在任何 dither 机制，渐变（`GLSLClampedGradientEffect`）等同样输出长距离平缓过渡的路径都未做，若只给阴影加，会出现"阴影有噪声抖动、相邻渐变没有"的不一致；且引入后 baseline 截图将带随机噪声，需要额外处理比较容差。

需要区分两类 banding 以免误判：**量化 banding** 源于 8-bit 输出本身（长距离平缓渐变每个色阶跨越多个像素），与 erf 精度、求积点数都无关，加点或换 erf 都消不掉，只能靠 dither 或更高位深；**近似误差导致的条纹**才与 N / erf 有关。因此实测若见 banding，先判断它是否随 N 增大而减弱——不减弱即为量化 banding，届时再评估是否为整个 TGFX 引入统一的 dither 机制（属独立改进，见 8.9）。

综合两项误差源，快路径的总偏差为求积的 1.49（各向异性最差，等向 0.66）与 erf 逼近的 0.124 个灰度级，量级由前者主导。与回落路径的差异则主要来自后者的离散化欠采样（4），其在小 σ 下可达 8.8~51 个灰度级，比快路径自身的近似误差大一个数量级。

### 5.3 坐标空间与 FragmentProcessor 的接入路径

#### 5.3.1 坐标空间约定

闭式求值需要明确 σ 与几何量所处的坐标系。本方案取 **content 像素空间**——即 `LayerStyleInput.content` 图像的像素坐标系，原点在 `contentOffset`、单位为设备像素。依据：

- 现状阴影的 σ 就在该空间：`getShadowFilter(scale)` 传给滤镜的是 `_blurrinessX * scale`（`DropShadowStyle.cpp:168`，`scale` 即 `input.contentScale`），滤镜作用于 content 图像的像素。快路径取同一口径，才能保证与回落路径的 σ 语义一致。
- `SpreadUtils` 的几何折算也在该空间：录制时先 `recordCanvas->scale(contentScale, contentScale)` 再 concat 形状矩阵（`SpreadUtils.cpp:193-194`）。
- AA 过渡带的"1 px"在该空间才成立（2.4 的现状 AA 是设备像素级）。

因此三个量的定义为：

```
σx = _blurrinessX × input.contentScale
σy = _blurrinessY × input.contentScale
几何量（rect / 角半径）= StyledShape 的 local 几何，经 UnwrapMatrixShape 剥出的矩阵变换后，再 × contentScale
```

**shader 坐标基准的机制**：paint shader 的采样坐标是 **canvas local 坐标**（受 canvas 矩阵影响）。链路为：`RectDrawOp` 构造时取 `uvMatrix = firstMatrix().invert()`（`RectDrawOp.cpp:53-57`，`firstMatrix` 即 viewMatrix），GP 在顶点阶段用它把 device 顶点位置映射回 local 坐标，再 concat 各 FP 的 `CoordTransform::getTotalMatrix()` 得到最终 uv（`GeometryProcessor.cpp:58-70` 与 `:88-102`）。该行为已实测确认：横向梯度配 canvas 2x 缩放，屏幕右端采到的是梯度中点值而非终点值。

**canvas 矩阵的处理**：`Layer::drawLayerStyleDefault` 在调用 `onDraw` 前已给 canvas concat 了 `MakeScale(1/contentScale)` 并平移 `contentEntry.offset`（`Layer.cpp:1936-1938`），使 `onDraw` 里的 canvas local 空间为**图层 local 空间**而非 content 像素空间。所以 Shader 需要把采样坐标再乘回 `contentScale`。

由于 `Shader::makeWithMatrix(M)` 的语义是**取 M 的逆作为坐标变换**（`MatrixShader.cpp:62-71`：`matrix.invert(&totalMatrix)` 后传给下游 FP），要让采样点从图层 local 变换到 content 像素空间（即乘 `contentScale`），应传入的是缩小矩阵：

```
shader = ShapeBlurShader::Make(content 像素空间的几何与 σ)
             ->makeWithMatrix(Matrix::MakeScale(1.0f / contentScale));
paint.setShader(shader);
canvas->drawRect(drawRect, paint);      // drawRect 在图层 local 空间
```

等价做法是让 `ShapeBlurShader::asFragmentProcessor` 自行把 `MakeScale(contentScale)` 并入交给 `CoordTransform` 的 uvMatrix（`CoordTransform` 的 matrix 即正向的 local → uv 变换，不再取逆），省去一层 `MatrixShader` 包装。两种写法择一，需在实现时统一，避免重复缩放。

**阴影绘制区域**（drawRect，图层 local 空间）：

```
DropShadow：spreadBounds.makeOutset(2σx / contentScale, 2σy / contentScale)
            再 offset(_offsetX, _offsetY)
InnerShadow：图层原形状 bounds（阴影不外扩，与 filterBounds 返回 srcRect 一致）
```

其中 `spreadBounds` 为 spread 折算后的形状 bounds（`MakeSpreadRRect` 的结果）。`2σ` 即截断核支撑半径，故 drawRect 边界处覆盖率严格为 0、不产生被裁切的硬边；除以 `contentScale` 是因为 drawRect 在图层 local 空间而 σ 在 content 像素空间。该范围与 `filterBounds` 的 `2σ` outset 口径一致（5.5）。

#### 5.3.2 接入路径

`LayerStyle::onDraw` 只能拿到 `Canvas`（`LayerStyle.h`），没有 DrawContext / OpsCompositor 直通口，无法直接提交 FragmentProcessor。现状阴影都是 `canvas->drawImage(模糊后的图, ..., &paint)`。因此本方案需要把新 FP 包装成 `Shader`：

```
DropShadowStyle::onDraw
  → Shader（新增内部子类，持有 content 像素空间的几何 + σx / σy + color）
      asFragmentProcessor(FPArgs, uvMatrix) → RectBlur / RRectBlurFragmentProcessor
  → paint.setShader(shader) + canvas->drawRect(阴影绘制区域, paint)
```

参照 `PerlinNoiseShader`（`src/core/shaders/PerlinNoiseShader.h/.cpp`）——同样是"程序化生成、无输入图像"的 Shader，其 `asFragmentProcessor` 直接构造对应 FP 并把 `uvMatrix` 交给 FP 的 `CoordTransform`。需要的改动：

- 新增 `src/core/shaders/ShapeBlurShader.h/.cpp`，继承 `Shader`；
- `Shader::Type` 枚举（`include/tgfx/core/Shader.h:199`，protected）新增一个值。该 enum 是封闭的，`Types::Get(shader)` 依赖它做类型分派（`src/core/utils/Types.h:43`）；
- 工厂**不进公开头文件**：`Shader.h` 的 `MakeXxx` 都是对外 API，而本 Shader 是阴影内部实现细节，改为在 `ShapeBlurShader.h` 上提供静态 `Make`，由 layerstyles 直接 include 内部头文件；
- **必须实现 `isEqual`**（`Shader.h:209` 为纯虚），且需按全部几何与 σ 字段逐一比较（参照 `PerlinNoiseShader::isEqual`）。这一点直接影响合批：`OpsCompositor::CompareBrush` 先比 shader 指针，不等时回退到 `isEqual`（`OpsCompositor.cpp:412-416`）。每个阴影都会 new 一个 ShapeBlurShader，指针必然不同，因此**能否与相邻阴影合批完全取决于 `isEqual`**——若只作类型判断就返回 true，参数不同的阴影会被错误合批（`pendingBrush` 只保留第一个 brush，后续阴影会被按第一个的参数绘制）；若逐字段比较，则同参数的多个阴影（如列表中样式一致的卡片）可合批，参数不同的各自成 op。

**合批能力的变化需纳入性能评估**：现状路径的 `canvas->drawImage` 在 brush 相同时可合批，而阴影图像各不相同（每个图层的模糊结果是独立纹理），实际也难合批；新路径下同参数阴影反而**可以**合批（几何走 RectDrawOp 的顶点数据、参数走 uniform）。但**参数不同的阴影一律打断合批**，这是相对"pass 数减少"的一项反向成本，量级需实测确认（6.4）。

**坐标获取方式**：取 `CoordTransform` 的 local uv 路径（同 `PerlinNoiseShader` / 渐变系），不用 `RectEffect` / `RRectEffect` 的 `gl_FragCoord` device-space 路径。原因是后者需要 render target 的 origin transform，而 `FPArgs` 不提供该信息——`OpsCompositor::tryApplyAnalyticFP` 能用 device-space 是因为它在 compositor 内部、能直接取 `renderTarget->getOriginTransform()`（`OpsCompositor.cpp:945-985`），走 paint shader 提交则拿不到。

### 5.4 阴影 FragmentProcessor（4 类）

按"效果 × 形状"拆为 4 个 FragmentProcessor，与 Figma 的 4 个闭式 shader 一一对应：

| 处理器 | 对齐 Figma | 核心 |
|--------|-----------|------|
| `RectBlurFragmentProcessor` | `RectangleBlur` | `axisAlignedCoverage` 零采样 |
| `RRectBlurFragmentProcessor` | `RoundedRectangleBlur` | `roundedRectShadow` 8 采样 |
| `RectInnerShadowFragmentProcessor` | `RectangleInnerShadow` | `(1 − axisAlignedCoverage)` × mask |
| `RRectInnerShadowFragmentProcessor` | `RoundedRectangleInnerShadow` | `(1 − roundedRectShadow)` × mask |

四者本期都是**阴影专用**：均以 `shadowColor` 为 uniform、输出 `shadowColor × coverage`，不是可直接复用的通用模糊原语。前两个（Blur 系）的覆盖率计算部分（`axisAlignedCoverage` / `roundedRectShadow` 及其函数链）与阴影语义无关，是后续扩展的复用点——但复用方式取决于场景：Glow 只需换 `shadowColor` 的取值（可直接复用 FP），而"形状整体模糊"需要输出裸覆盖率而非着色结果，得把上色从 FP 中剥离（改为输出 coverage、由外层 shader 或 colorFilter 上色）。该剥离本期不做，留待第一个真实复用场景落地时按需重构（见 8.1 / 8.2）。

各类文件参照 `GaussianBlur1DFragmentProcessor` 的 `DEFINE_PROCESSOR_CLASS_ID` / `onComputeProcessorKey` / `emitCode` / `onSetData` 结构，新增于 `src/gpu/processors/` 与 `src/gpu/glsl/processors/`。ClassID 无需集中注册（`DEFINE_PROCESSOR_CLASS_ID` 宏用 `UniqueID::Next()` 运行时分配）；基类的静态 `Make` 定义写在 GLSL 子类的 .cpp 里、由它 `allocator->make<GLSLXxx>()`，与 `RectEffect::Make` 定义在 `GLSLRectEffect.cpp` 的做法一致。

**共享约定**（4 类一致）：

- **几何量预除 σ 归一化**（对齐 3.2 的归一化思路，除数由标量 blurRadius 换为 `vec2(σx, σy)`）：传 `halfSize / σ`、`corner / σ`（均为 `vec2`，逐分量相除），坐标 `coord = (fragPos − center) / σ`，shader 内两轴 σ 恒为 1、核支撑恒为 ±2，无 `MAX_BLUR_SIGMA` 上限；
- spread 在 CPU 端 outset 进几何参数（复用 `SpreadUtils::MakeSpreadRRect` 折算），shader 不感知 spread；
- 共享 GLSL 函数链 `erfApprox` / `truncatedCDF` / `axisAlignedCoverage` / `rowSpan` / `roundedRectShadow` / `rectSDF` / `roundRectSDF` / `shapeCoverage` 用 `FragmentShaderBuilder::addFunction` 声明（`ShaderBuilder.h:70`），函数名用 `getMangledFunctionName`（`ShaderBuilder.h:72`）避免同一 program 内多个同类 FP 重定义；
- AA 沿用现状（2.4）：形状 mask 的覆盖率用 `shapeCoverage`，与 `EllipseGeometryProcessor` / `RRectEffect` 一致的线性过渡 `clamp(0.5 − sdf, 0.0, 1.0)`，不引入 Figma 的 `smoothstep` 三次过渡（故不复用 3.2 的 `aaCoverage` 命名，避免与其 `fwidth` + `smoothstep` 实现混淆）；
- 输出为 premultiplied alpha：`shadowColor` 在 `onSetData` 时按 `Color::premultiply()`（`include/tgfx/core/Color.h:193`）预乘后再上传，shader 内直接 `outColor = shadowColor * coverage`；
- **图层 alpha 沿用 `paint.setAlpha`，不需并入 `shadowColor`**：`Paint::setAlpha` 写的是 `brush.color.alpha`（`Paint.h:85-87`），该 color 会随顶点记录进入 GP（`OpsCompositor.cpp` 的 `RectRecord(rect, matrix, brush.color)`），由 GP 写入 FP 链的输入色（`GLSLQuadPerEdgeAAGeometryProcessor.cpp:55-63` 的 `args.outputColor`），shader FP 与之相乘——故 alpha 对 shader 输出同样生效。已实测确认：白色 color shader 配 `setAlpha(0.5)`，输出为 128。因此现状 `paint.setAlpha(alpha)`（`DropShadowStyle.cpp:158`）的传递方式可原样保留。

**坐标传递**：Blur 系只需一套 σ 归一化坐标，由单个 `CoordTransform` 承担（其 uvMatrix 合并了 5.3.1 的 `contentScale` 缩放、中心平移、`1/σ` 归一化，以及 DropShadow 的 offset）。Inner 系需要**两套坐标**，原因有二：

1. 阴影穿透区在 σ 归一化空间求值，而形状 mask 的 AA 过渡带是 1 个 content 像素、与 σ 无关，必须在未归一化空间求值；
2. DropShadow 的 offset 是整体平移（可并入唯一的坐标变换），但 InnerShadow 的 offset **只平移 shadow 形状、mask 不动**，两套坐标之间存在相对偏移。

Inner 系的做法是只用一个 `CoordTransform` 产出 **content 像素空间**的 `maskCoord`（相对图层原形状中心），再在 shader 内由它算出 `shadowCoord`：

```glsl
// maskCoord：CoordTransform 产出，content 像素空间，相对 mask 形状中心
// shadowCenterOffset：shadow 形状中心相对 mask 形状中心的偏移（含 InnerShadow 的 offset），
//   由 CPU 在 content 像素空间算好；invSigma = vec2(1/σx, 1/σy)
vec2 shadowCoord = (maskCoord - shadowCenterOffset) * invSigma;
```

即 Inner 系需额外传 `shadowCenterOffset` 与 `invSigma` 两个 uniform，换取只用一套 `CoordTransform`。`maskHalfSize` / `maskCornerRadius` 保持 content 像素空间（不除 σ），`shadowHalfOverSigma` / `shadowCornerOverSigma` 为归一化后的值。

**核函数链**：σ 归一化后两轴 σ 恒为 1、支撑恒为 ±2，`Φ̃` 的两个端点质量退化为编译期常量，故截断 CDF 只需一次 `erfApprox`：

```glsl
// erfApprox：erf 的逼近式，源自 Hastings (1955)，即 Abramowitz & Stegun
// "Handbook of Mathematical Functions" 式 7.1.27（p. 299）:
//   https://personal.math.ubc.ca/~cbm/aands/page_299.htm
// 适用域 0 <= x < inf，公布误差界 |e(x)| <= 5e-4。纯有理式，无需 exp。
// 先对 |x| 求正支，再按 erf 的奇函数性赋号。
float erfApprox(float x) {
  float ax = abs(x);
  float d = 1.0 + ax * (0.278393 + ax * (0.230389 + ax * (0.000972 +
            ax * 0.078108)));
  float d2 = d * d;
  float positiveBranch = 1.0 - 1.0 / (d2 * d2);
  return x >= 0.0 ? positiveBranch : -positiveBranch;
}

// truncatedCDF：sigma 归一化后（sigma = 1）的截断核 CDF。
// LOW / MASS 分别为 Phi(-2) 与 Phi(2) - Phi(-2)；0.70710678 为 1/sqrt(2)。
float truncatedCDF(float u) {
  const float LOW = 0.02275013;
  const float MASS = 0.95449974;
  if (u <= -2.0) return 0.0;
  if (u >= 2.0) return 1.0;
  return (0.5 * (1.0 + erfApprox(u * 0.70710678)) - LOW) / MASS;
}

// axisAlignedCoverage：矩形的纯闭式覆盖率，两方向各一次 CDF 差后相乘，零采样。
// coord / halfSize 均已按 vec2(σx, σy) 归一化。
float axisAlignedCoverage(vec2 coord, vec2 halfSize) {
  vec2 lo = coord + halfSize;
  vec2 hi = coord - halfSize;
  return (truncatedCDF(lo.x) - truncatedCDF(hi.x))
       * (truncatedCDF(lo.y) - truncatedCDF(hi.y));
}
```

`rowSpan` / `roundedRectShadow` 的结构照 3.2 移植，三处改动：`gaussianCDF` 换成 `truncatedCDF`、`triWeight(s)` 换成 `exp(-0.5 * s * s)`、核支撑常数 `1.0` 换成 `2.0`（含 `clamp(±1.0, lo, hi)` → `clamp(±2.0, lo, hi)`）。此外 `rowSpan` 的角半径由单一 `corner` 改为椭圆双半轴，以支持各向异性 σ（5.1）：

```glsl
// rowSpan：点在某一行的水平模糊覆盖率（内层闭式积分）。
//   x / y        该点坐标（已按 σ 归一化）
//   cornerAlong  沿积分轴方向的角半轴（决定直边区/圆角区分界）
//   cornerCross  垂直于积分轴方向的角半轴（决定半宽的收缩量）
//   halfCross / halfAlong  归一化半尺寸
// 各向同性时 cornerAlong == cornerCross，退化为 3.2 的正圆弧公式。
float rowSpan(float x, float y, float cornerAlong, float cornerCross,
              float halfCross, float halfAlong) {
  float delta = min(halfAlong - cornerAlong - abs(y), 0.0);
  float ratio = cornerCross / max(cornerAlong, 1e-6);
  float curved = halfCross - cornerCross +
                 ratio * sqrt(max(0.0, cornerAlong * cornerAlong - delta * delta));
  return truncatedCDF(x + curved) - truncatedCDF(x - curved);
}
```

`rowSpan` 假定 `cornerAlong ≤ halfAlong` 且 `cornerCross ≤ halfCross`（否则 `halfCross − cornerCross` 为负、半宽算错）。该前提由 `RRect::setRectRadii` 的 `ScaleRadii` 保证：相邻两角半径之和超过所在边长时按比例整体缩小（`RRect.cpp:45-79`），故单角半径不超过对应半边长。而 `MakeSpreadRRect` 折算后同样调用 `setRectRadii`（`SpreadUtils.cpp:50-52`），因此 **spread 折算的结果也满足该不变式**——包括"角半径 + spread 后逼近半尺寸"的情形（如 100×60、r=50/30 叠加 spread=10 后正好 r=60/40 = halfSize，即退化为 oval，仍然合法）。

另需注意 `ScaleRadii` 会把任一分量为 0 的角整体归零（`RRect.cpp:74-78`），故"x 分量非零、y 分量为零"的畸形角不会到达 shader。

`roundedRectShadow` 相应把 `corner` 参数改为 `vec2 corner`（两个半轴），在两条选轴分支里按积分轴分配 `cornerAlong` / `cornerCross`：沿 Y 积分时 `cornerAlong = corner.y`、`cornerCross = corner.x`，沿 X 积分时交换。内部实心区跳过的判据同样按 `vec2` 写（支撑常数取 2.0）：

```glsl
// 深入内部：点离任意边界超过核支撑半径（2.0）且已越过圆角弧的内推量，覆盖率恒为 1
if (all(lessThan(abs(coord), halfSizeOverSigma - (cornerOverSigma + vec2(2.0))))) {
  coverage = 1.0;
} else {
  coverage = roundedRectShadow(coord, halfSizeOverSigma, cornerOverSigma);
}
```

该判据在椭圆角下已数值验证：在判据边界上取最坏点，真实覆盖率与 1.0 的偏差为 9.4e-9（纯数值积分残差，0.0000 灰度级），即判据充分保守。形状小于核时（如 `halfSize − corner < 2`）右侧为负、条件恒假，自动退回积分路径，无需额外保护。

**形状 mask 的 SDF 与覆盖率**（仅 Inner 系用，在 content 像素空间求值）：

```glsl
// rectSDF：轴对齐矩形 SDF，p 相对中心（外正内负）
float rectSDF(vec2 p, vec2 halfSize) {
  vec2 d = abs(p) - halfSize;
  return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
}

// roundRectSDF：圆角矩形 SDF，角半径 clamp 到半尺寸内
float roundRectSDF(vec2 p, vec2 halfSize, float corner) {
  float r = min(corner, min(halfSize.x, halfSize.y));
  return rectSDF(p, halfSize - r) - r;
}

// shapeCoverage：SDF 转覆盖率，1 px 线性过渡（对齐 TGFX 现状 AA，非 smoothstep）
float shapeCoverage(float sdf) {
  return clamp(0.5 - sdf, 0.0, 1.0);
}
```

`shapeCoverage` 的 1 px 过渡带假设 SDF 已在 content 像素空间、且该空间到设备像素为等比且尺度接近 1；5.6 已把含透视或非等比缩放的形状排除到回落路径。


`rowSpan` / `roundedRectShadow` 的结构照 3.2 移植，仅把 `gaussianCDF` 换成 `truncatedCDF`、`triWeight` 换成 `exp(-0.5 * s * s)`、支撑常数 `1.0` 换成 `2.0`。

四类的 uniform 与主体如下。`coord` / `maskCoord` 由 `CoordTransform` 产出（5.4 坐标传递），`shadowColor` 已预乘。

**RectBlurFragmentProcessor**（矩形通用模糊）：uniform 为 `halfSizeOverSigma`（vec2）/ `shadowColor`。零采样（对应 3.1 矩形纯闭式解）：

```glsl
float coverage = axisAlignedCoverage(coord, halfSizeOverSigma);
outColor = shadowColor * coverage;
```

**RRectBlurFragmentProcessor**（圆角通用模糊）：uniform 为 `halfSizeOverSigma`（vec2）/ `cornerOverSigma`（vec2，椭圆双半轴）/ `shadowColor`。`roundedRectShadow`（`rowSpan` + 固定 8 采样 + 自适应选轴），外加内部实心区跳过：

```glsl
float coverage;
if (all(lessThan(abs(coord), halfSizeOverSigma - (cornerOverSigma + vec2(2.0))))) {
  coverage = 1.0;
} else {
  coverage = roundedRectShadow(coord, halfSizeOverSigma, cornerOverSigma);
}
outColor = shadowColor * coverage;
```

**RectInnerShadowFragmentProcessor**（矩形内阴影）：uniform 为 `shadowHalfOverSigma`（vec2）/ `shadowCenterOffset` / `invSigma` / `maskHalfSize` / `shadowColor`。mask 固定走 1 px 线性 AA 软边：

```glsl
vec2 shadowCoord = (maskCoord - shadowCenterOffset) * invSigma;
float coverage = 1.0 - axisAlignedCoverage(shadowCoord, shadowHalfOverSigma);
coverage *= shapeCoverage(rectSDF(maskCoord, maskHalfSize));
outColor = shadowColor * coverage;
```

**RRectInnerShadowFragmentProcessor**（圆角内阴影）：uniform 为 `shadowHalfOverSigma`（vec2）/ `shadowCornerOverSigma`（vec2）/ `shadowCenterOffset` / `invSigma` / `maskHalfSize` / `maskCornerRadius` / `shadowColor`。mask 固定走 1 px 线性 AA 软边：

```glsl
vec2 shadowCoord = (maskCoord - shadowCenterOffset) * invSigma;
float shadowCoverage = 1.0 - roundedRectShadow(shadowCoord, shadowHalfOverSigma,
                                              shadowCornerOverSigma);
float maskCoverage = shapeCoverage(roundRectSDF(maskCoord, maskHalfSize, maskCornerRadius));
outColor = shadowColor * shadowCoverage * maskCoverage;
```

Inner 系不做内部实心区跳过：`1 − coverage` 在形状深处恒为 0，那里反而是被 mask 裁掉或阴影为空的区域，跳过分支无收益。

`maskHalfSize` / `maskCornerRadius` / `shadowCenterOffset` / `maskCoord` 均在 content 像素空间（不除 σ），`shadowHalfOverSigma` / `shadowCornerOverSigma` 已归一化——两组量所处空间不同，由 shader 内的 `* invSigma` 衔接（5.4）。

Inner 语义（双形状：`mask*` = 图层原形状裁剪范围，`shadow*` = spread 内缩后的穿透形状）与现有 `InnerShadowStyle::drawWithSpread` 一致（`InnerShadowStyle.cpp:143`，现状用 `MakeSpreadShapeImage(input, 0)` 取原形状、`MakeSpreadShapeImage(input, -_spread)` 取穿透形状），亦与 3.2 提取的 Figma `RoundedRectangleInnerShadow` 真实 `main()` 对应。

### 5.5 DropShadowStyle / InnerShadowStyle 集成

`DropShadowStyle::onDraw`：StyledShape 可解析且 spread ≠ 0 时按形状分派——`isRect` → `RectBlurFragmentProcessor`，`isRRect` / 正圆 → `RRectBlurFragmentProcessor`——经 5.3 的 Shader 单 pass 绘制，旁路 `SpreadUtils::MakeSpreadShapeImage` + blur 滤镜；否则回落现有路径。spread → (bounds, corner) 复用 `MakeSpreadRRect` 的折算，避免两条路径几何不一致。offset 并入drawRect 与坐标变换（5.3.1）。

`_showBehindLayer = false` 时仍用轮廓图做反向遮罩（`paint.setMaskFilter`），但**遮罩矩阵必须显式抵消 offset**，这与新shader 并非正交：

| 路径 | offset 的实现 | 遮罩为何仍对齐 |
|---|---|---|
| 回落| `drawImage(shadowImage, left, top, ...)` 几何平移 | `Canvas::drawImage` 用 `dstMatrix` 的逆构造 `brush.makeWithMatrix`（`Canvas.cpp:586-593`），而 `Brush::makeWithMatrix` 同时作用于 shader 与 maskFilter（`Brush.cpp:66-72`），遮罩被逆矩阵钉回原位 |
| 快路径 | `canvas->translate(offset)` | canvas 矩阵对几何、shader、maskFilter 一视同仁（`ShaderMaskFilter::asFragmentProcessor` 与 shader 的 FP 共用同一套 `FPArgs` + uvMatrix），**无任何补偿** |

因此快路径必须在遮罩的 shader 矩阵里减去 offset，否则轮廓遮罩被一并平移、正好压在阴影形状本体上，反向遮罩会把阴影内部整块挖空：

```
maskShader = contourShader->makeWithMatrix(
    Matrix::MakeTrans(contourOffset.x - offsetX, contourOffset.y - offsetY));
```

符号依据 `MatrixShader::asFragmentProcessor` 取 `matrix.invert()` 作为坐标变换（`MatrixShader.cpp:62-71`）：canvas 平移 +offset 后局部坐标为"图层坐标 − offset"，故传入项减去 offset，其逆恰好把采样点抬回图层坐标。该结果与回落路径复合出的 `T(contourOffset − offset)` 一致，已实测确认。

`InnerShadowStyle::onDraw`：同理按形状分派——`isRect` → `RectInnerShadowFragmentProcessor`，`isRRect` / 正圆 → `RRectInnerShadowFragmentProcessor`——传图层原形状（mask）+ spread 内缩形状（shadow）两组参数，两者的中心差（含 offset）作为 `shadowCenterOffset`。

**CPU 端需算好的量**（均在 content 像素空间，见 5.3.1）：

| 量 | 算法 |
|---|---|
| `σx` / `σy` | `_blurrinessX/Y × contentScale`；任一为 0 时不构造 blur FP（6.2） |
| shadow 形状 | `MakeSpreadRRect(rRect, ±spread)` 折算后 × contentScale |
| mask 形状（仅 Inner） | 原 rRect × contentScale |
| `halfSizeOverSigma` | `shadow.halfSize / vec2(σx, σy)` |
| `cornerOverSigma` | `vec2(r / σx, r / σy)`（同一个原角半径 r，两轴各自归一化 ⇒ 椭圆双半轴） |
| `shadowCenterOffset`（仅 Inner） | shadow 中心 − mask 中心 + `(_offsetX, _offsetY) × contentScale` |
| `drawRect` | 见 5.3.1 |

`filterBounds`：两者均**保持现状不变**。DropShadow 按 spread + `2σ` outset srcRect（`DropShadowStyle.cpp:98-108` 叠加 `onFilterBounds` 的 `2σ`），该 `2σ` 恰为截断核支撑半径（2.1、3.1），单 shader 的 drawRect 与其一致、边界处覆盖率严格为 0；InnerShadow 返回 srcRect。`filterBounds` 拿不到 StyledShape、无法区分快 / 回落路径，保持不变也就保证了回落路径的图层 bounds 零改动。

### 5.6 形状识别与回落

复用 `SpreadUtils` 已有的 `path.isOval` / `path.isRRect` / `path.isRect` 分派（`SpreadUtils.cpp:201-217`），单 shader 覆盖 **fill 类型、四角统一圆角（可为椭圆角）、无 corner smoothing** 的形状：

- `isRect`（fill）→ Rect 系（`RectBlur` / `RectInnerShadowFragmentProcessor`）。
- `isRRect`（fill）且 `rRect.type()` 为 `Simple`（四角同半径，`RRect.h:66-75`）→ RRect 系。角半径的 x/y 分量可以不等（椭圆角）——归一化后本就按椭圆双半轴求值（5.1），故无需要求正圆。
- `isOval`（fill）→ 角半径 = 半尺寸的 RRect，走 RRect 系。**正圆与真椭圆均可**：`RRect::MakeOval` 产出 `Type::Oval`（各角半径等于对应边长之半），落在同一椭圆角公式上。
- **stroke 类型**（描边，含 Rect / RRect stroke）、`rRect.type() == Complex`（per-corner 半径）、corner smoothing、非 Rect/RRect/Oval 的 path、多形状容器 → 回落现状滤镜路径。

**必须排除"bounds 兜底"产出的 Rect**（正确性要求，不只是优化取舍）。`Layer::onGetContentShape` 的默认实现在无法还原矢量形状时，会把 `content->getTightBounds()` 包成 Fill 型 Rect 返回（`Layer.cpp:1032-1048`），而非返回 `nullopt`。未 override 该方法的 Layer 类型（ImageLayer、TextLayer 等）走的都是这条兜底路径。若快路径不加区分地接受它，TextLayer 这类"轮廓远小于 bounds"的内容，阴影会从贴合字形变成整块矩形——**明显的行为回退**。

现状 `SpreadUtils` 同样有 bounds 近似（`SpreadUtils.cpp` 的 `MakeSpreadShapeImage` 把复杂 path 按 bounds 当 fill 画），生效范围与快路径一致（均限 spread ≠ 0），因此快路径不会把该近似扩大到原本精确的 spread = 0 场景。但两者的近似程度不同：现状把 bounds 画成实心矩形后仍经真实高斯模糊，而快路径直接按矩形闭式求值——对 TextLayer 这类内容，两者都错，快路径没有更差，然而这不构成放行的理由，正确做法仍是回落。

因此需要区分"真实矢量形状"与"bounds 兜底"。可选做法：① 给 `StyledShape` 或 `onGetContentShape` 增加一个标记位，兜底路径置位，快路径见到即回落；② 快路径只接受**已知会 override `onGetContentShape` 的类型**（`ShapeLayer` / `SolidLayer` / `VectorLayer`，见 `src/layers/` 中的三处实现），其余一律回落。②改动最小但耦合类型判断，①更干净但要动公开结构。**取 ①**：`StyledShape` 增加 `exact` 字段，兜底路径由 `StyledShape::MakeApproximate` 置为 false，`MakeAnalyticShape` 见到即返回 `nullopt`。

分派顺序取 oval → rRect → rect，与 `SpreadUtils.cpp:201-217` 的既有顺序一致。该顺序下三个分支互斥：`Path::isRRect` 内部显式排除了可表示为 rect 或 oval 的情形（`Path.cpp:173-178`：`skRRect.isEmpty() || skRRect.isRect() || skRRect.isOval()` 任一成立即返回 false），故先判 oval 不会被 rRect 抢走、后判 rect 也不会漏。

形状还需经 `SpreadUtils::UnwrapMatrixShape` 剥离嵌套矩阵。剥出的矩阵仅在**含平移与轴对齐缩放**时可并入几何参数（缩放吸收进 halfSize / 角半径的两个分量）；含旋转、skew 或透视时形状不再轴对齐、闭式不成立，回落现状滤镜路径。

## 6 测试计划

### 6.1 截图测试

**复用现有 LayerTest 用例为主**。`TGFX_TEST(LayerTest, DropShadow)`（`LayerTest.cpp:3872`）/ `InnerShadow`（`:3887`）共用辅助函数 `BuildShadowTestLayers`（`:3542`）构造 17 个 Case，每个 Case 各画 spread = 0 与 spread ≠ 0 两份。按 5.6 的判定规则，**只有 spread ≠ 0 的那一份**才可能走快路径（5.1），spread = 0 的那一份一律回落。各 Case 中 spread ≠ 0 那份的路径归属为：

| Case | 内容 | 路径 |
|---|---|---|
| 1 | SolidLayer roundness 10 | 快路径 RRect 系 |
| 3 | ShapeLayer Oval fill | 快路径 RRect 系（`isOval`，椭圆角） |
| 6 | ShapeLayer Rect + ImageShader fill | 快路径 Rect 系 |
| 10 | VectorLayer RoundRect + ImagePattern fill，spread = −8 | 快路径 RRect 系（负 spread） |
| 2 | ImageLayer | 回落（`exact = false` 的 bounds 兜底） |
| 9 | TextLayer with perspective | 回落（`exact = false` + 透视） |
| 12 | VectorLayer star fill | 回落（任意 path） |
| 4, 5 | Oval stroke / star stroke | 回落（stroke） |
| 7, 8, 15 | Rect fill + dashed stroke / dashed 烘焙成 fill path | 回落（多形状容器 / 任意 path） |
| 11, 13, 14, 16, 17 | Ellipse / Rect+Ellipse + 各类 stroke，含复合变换 | 回落（stroke / 多形状 / 旋转） |

Case 2（ImageLayer）与 Case 9（TextLayer）都因 `exact = false` 回落：两者均未 override `onGetContentShape`，落到 `Layer::onGetContentShape` 的兜底实现（`Layer.cpp:1032-1048`），该实现走 `StyledShape::MakeApproximate`、置 `exact = false`（5.6）。ImageLayer 的矩形内容虽与 bounds 一致、走快路径本可得到正确结果，但那是"兜底恰好正确"而非"判定为真实矩形"，不作为放行依据；待 ImageLayer 自行 override 并交出真实矩形后自然进入快路径。

**基准变化的预期**（必须区分两类，不可一概按"回归"处理）：

| 用例类别 | 基准是否变化 | 原因 |
|---------|------------|------|
| 回落路径（spread = 0 的全部；spread ≠ 0 中的 stroke / 任意 path / 多形状 / 变换 / 非exact） | **不变** | 代码路径与 `filterBounds` 均未改动（5.5） |
| 快路径（Case 1/3/6/10 的 spread ≠ 0 那份） | **变化** | 由现状"离散点采样卷积"改为同核的精确闭式，差异量级见 4 精度结论 |

因此快路径用例的 baseline 差异是**预期结果而非回归**。审阅方式是逐例确认：① 阴影轮廓与虚化范围不变（同核同 σ、bounds 不变）；② 差异集中在边缘过渡带的灰度，且 σ 越大差异越小；③ 无硬边、无裁切、无偏移。若出现轮廓位移或范围变化，则是实现 bug，需回到几何折算与坐标变换排查。

**新增用例**（末尾追加，覆盖现有 Case 照不到的分支）。三者均需 spread ≠ 0 才能进入快路径，故spread 取小的非零值，使几何折算量接近原形状、便于与回落路径对照：

| 用例 | 形状 | 阴影参数 | 验证点 |
|------|------|---------|--------|
| 正圆 | `isOval` 宽高相等，fill | Drop / Inner，spread = 1，σ = 10 | ① `isOval` 正圆 → RRect 系，`corner = halfSize` 角半径极值；② σ = 10 为现状 `MAX_BLUR_SIGMA` 上限、回落路径未降质，是两条路径唯一可公平对照的 σ |
| 各向异性 σ | 统一圆角 RRect，fill | Drop，spread = 1，σx = 12 / σy = 4 | 椭圆角 `rowSpan` 路径（5.1）：角半径两个半轴不等，验证归一化正确、无方向性畸变 |
| 超大 σ | 统一圆角 RRect，fill | Drop，spread = 1，σ = 30 | 解除 `MAX_BLUR_SIGMA` 后的质量提升（现状会缩 RT 降质，此处应明显更平滑） |

对照方式：正圆用例同时给出快路径输出与**强制回落**（临时改判定条件）的输出，二者对比确认虚化范围与衰减形态一致、差异仅在过渡带灰度。该对照仅在开发期做，不进最终测试用例。

**截图基准变更需由用户主动执行 `/accept-baseline`，本方案不自行接受。**

### 6.2 边界情况

单测（`test/src/`，可访问 private 成员）覆盖前四项，截图覆盖后四项：

- **σ = 0（任一轴）**：核退化，`truncatedCDF` 的归一化除数与 `invSigma` 均失效，须在 CPU 端拦截——任一轴 σ 为 0 时不构造 blur FP，回落现状滤镜路径（现状对 blur = 0 已有 nearest 采样的专门处理，`DropShadowStyle.cpp:146-148`；且 `ImageFilter::Blur` 在两轴皆 0 时返回 nullptr）。**不自行按"硬边形状"绘制**：现状 spread ≠ 0 且 blur = 0 时阴影边缘保留 1 px 光栅化 AA（2.4 表末行），改走硬边会使这批场景外观回退。
- **负 spread 导致形状收缩消失**：CPU 端由 `MakeSpreadRRect` 折算出空 rect，不绘制（与 `IsSpreadCollapsed` 现有语义一致，`SpreadUtils.cpp:135`）。
- **角半径 = 半尺寸（正圆 / 椭圆极值）与角半径 = 0（尖角）**：`rowSpan` 的圆角区 / 直边区分界退化。尖角时 `cornerAlong = 0`，`ratio = cornerCross / max(cornerAlong, 1e-6)` 的兜底分母生效，且 `sqrt` 项因 `cornerAlong² − delta² ≤ 0` 被 `max(0, ...)` 归零，`curved` 正确退化为 `halfCross`。断言两种极值下覆盖率无 NaN。
- **σ 远大于形状尺寸（形状被完全"吞没"）**：外层积分窗口被形状范围截断而非核截断，验证 `start == end` 的退化分支返回 0 且不产生 NaN。
- spread = 0：回落现状滤镜路径（5.1），验证快路径未被触发、基准与改造前一致。
- 大 σ（> 10）：验证不再有 `MAX_BLUR_SIGMA` 降质（现状会缩 RT，此处应看到明显质量提升）。
- 各向异性 σ（σx ≠ σy，含相差数倍）：验证椭圆角路径无方向性畸变、与两轴交换后镜像一致。
- contentScale ≠ 1（图层带缩放）：验证 5.3.1 的坐标空间换算正确——阴影的虚化范围应随缩放等比变化，且与回落路径一致。这是最容易因 `makeWithMatrix` 取逆的语义而写成反向缩放的地方。
- 图层 alpha < 1：验证 `paint.setAlpha` 对 shader 输出仍生效（5.4），阴影整体按图层 alpha 变淡。
- `_showBehindLayer = false`：验证 maskFilter（coverage FP）与新 shader（color FP）共存正确——二者在 `addDrawOp` 中分属 `addColorFP` / `addCoverageFP` 两条链，互不冲突（`OpsCompositor.cpp:1124-1152`）。

### 6.3 数值实验脚本

`designs/shadow-sdf-scripts/` 下的 8 个脚本是全文数值结论的唯一来源，可随参数改动重跑复核。它们是方案论证的实验记录，不是工程代码：随设计文档归档，不参与构建，也无需按项目的代码规范维护。

各脚本承载的结论：

| 脚本 | 结论 |
|------|------|
| `quadrature_study.py` | 等向情形下外层求积点数 N 的选型（核固定 ±2σ，21×21 探针）：N=4 最大偏差 2.06、N=8 为 0.66 个灰度级，最差点在长短边比 2.5:1 的形状上 |
| `window_vs_samples_study.py` | 截断范围 k 与 N 的联合影响（15×15 探针）：k 由 2 增到 3 时误差小幅上升，k=4 起明显退化 |
| `erf_and_kernel_study.py` | erf 逼近候选的初步对比；闭式与现状离散滤镜在理想硬边下的差异 |
| `wallace_erf_comparison.py` | 辨明 Wallace 所用 erf 即 A&S 7.1.27 去 a₃ 项；复验 7.1.27 公布界（实测 4.659e-4 ≤ 5e-4）并对比三式传导误差 |
| `truncation_necessity_study.py` | 核截断的必要性：闭式覆盖率公式的数值卷积复核，及无限支撑核在 2σ 绘制边界处的残留覆盖率 |
| `path_difference_study.py` | 快 / 回落路径差异随 σ 与源图边缘形态（硬边 vs 1 px AA）的变化 |
| `erf_verification.py` | 复验 A&S 7.1.26 的公布误差界（实测 1.394e-7 ≤ 1.5e-7）与奇函数性；并评估有界区间多项式替代方案 |
| `anisotropic_reduction_study.py` | 各向异性归约的正确性验证：以原空间暴力二维卷积为真值，确认"按 (1/σx, 1/σy) 缩放 + 椭圆角"无系统偏差（N=32 时 ≤ 0.51 灰度级）；并测出各向异性下 N=4 最差 3.55、N=8 最差 1.49，为 N 的取值依据 |

### 6.4 性能验证

本方案以性能为目标，但 1 与 4 中的 pass 数、中间纹理数均为**静态代码推算**，非实测。落地后需补一轮实测确认收益成立，否则整个方案的立论无依据。

被测指标与口径：

| 指标 | 统计方式 |
|------|---------|
| render pass 数 | 每帧 `OpsRenderTask` 数量（`DrawingManager` 的 renderTasks），快 / 回落路径分别记 |
| draw op 数 | 每帧 `drawOps.size()` 合计，用于观察合批是否被打断（5.3.2） |
| 中间纹理 | 每帧新建的 `RenderTargetProxy` / `TextureProxy` 数量与字节数峰值 |
| GPU 帧耗时 | 平台 profiler 的 GPU 时间；CPU 侧另记 `flushAndSubmit` 耗时 |

测试序列至少覆盖三类：① 单个大 σ 阴影（最能体现解除 `MAX_BLUR_SIGMA` 与省去多 pass 的收益）；② 同参数阴影 × N（验证合批成立）；③ 不同参数阴影 × N（验证合批打断的代价）。每类各跑快路径与强制回落两组对照。

预期与风险：pass 数与中间纹理应显著下降；但每像素成本上升（圆角路径 10 次 `Φ̃`、每次含一次除法与四次乘加），在**小 σ + 大面积**的场景下有可能反而变慢——因为回落路径的模糊在缩小的 render target 上做，而闭式路径逐像素全分辨率求值。若实测确认该场景存在回退，需补一条"σ 小于某阈值时仍走回落路径"的判定；该阈值只能由实测给出，不预设。

记录格式与分析纪律按性能测试文档规范执行（分序列归档、指标口径固定、数字可复算）。

## 7 实现步骤

| 阶段 | 文件 | 说明 |
|------|------|------|
| 1 | `src/gpu/processors/` + `src/gpu/glsl/processors/`：`RectBlur` / `RRectBlurFragmentProcessor`（各 `.h` + `.cpp` 及 GLSL 对应） | 新增 Blur 系 2 个 FP：`RectBlur`（`axisAlignedCoverage`，零采样）、`RRectBlur`（`roundedRectShadow`，内层闭式 + 外层 8 点求积 + 内部实心区跳过）；共享 `erfApprox`（A&S 7.1.27，纯有理式）/ `truncatedCDF`（±2σ 截断归一化）/ `axisAlignedCoverage` / `rowSpan`（椭圆双半轴）函数链，用 `addFunction` + `getMangledFunctionName` 声明 |
| 2 | `src/core/shaders/ShapeBlurShader.h` + `.cpp`；`include/tgfx/core/Shader.h`（`Type` 枚举）；`src/core/utils/Types.h` | 新增内部 Shader 子类把 Blur 系 FP 接入 Canvas 绘制路径（5.3.2），承载 5.3.1 的坐标空间换算；静态 `Make` 留在内部头文件、不进公开 API |
| 3 | `src/layers/SpreadUtils.h` + `.cpp`；`include/tgfx/layers/layerstyles/StyledShape.h`；`src/layers/Layer.cpp` 等 `onGetContentShape` 实现 | 抽出"StyledShape → 可解析形状（rect / rRect + `RRect::Type` 判定）+ spread 折算 + 矩阵剥离"的纯计算供快路径复用，与 `MakeSpreadRRect` 共享同一折算逻辑；并增加"是否为 bounds 兜底"的标记以便快路径排除（5.6，正确性要求） |
| 4 | `src/layers/layerstyles/DropShadowStyle.cpp` | `onDraw` 中 spread ≠ 0 且形状可解析时按 `isOval` / `isRRect` / `isRect` 分派 `RectBlur` / `RRectBlurFragmentProcessor` 单 pass，否则回落现状滤镜路径；`extraSourceType()` 保持现状不变（5.1）；`_showBehindLayer = false` 时遮罩矩阵需减去 offset（5.5）；CPU 端算好 5.5 表中各量；`paint.setAlpha` / `setBlendMode` 与 `filterBounds`均不动 |
| 5 | `test/src/` | Drop shadow 的截图测试（6.1，含新增 3 个用例的 Drop 部分）与边界单测（6.2） |
| 6 | `src/gpu/processors/` + `src/gpu/glsl/processors/`：`RectInnerShadow` / `RRectInnerShadowFragmentProcessor`；`src/layers/layerstyles/InnerShadowStyle.cpp` | Inner 系 2 个 FP 与集成：`(1 − coverage) × maskCoverage`；新增 `rectSDF` / `roundRectSDF` / `shapeCoverage` 函数（mask 在 content 像素空间求值）；两套坐标由单个 `CoordTransform` + `shadowCenterOffset` / `invSigma` 衔接（5.4）；同样限定 spread ≠ 0、`extraSourceType()` 保持现状不变 |
| 7 | `test/src/` | Inner shadow 的截图测试与边界补充；确认 Case 2（ImageLayer）的路径归属（6.1） |
| 8 | — | 性能实测（6.4）：pass 数 / draw op 数 / 中间纹理 / GPU 耗时，快路径与强制回落对照。若发现小 σ 场景性能回退，据实测阈值补回落判定 |

Inner 系的 Shader 接入方式：复用阶段 2 的 `ShapeBlurShader` 与同一个 `Shader::Type` 枚举值，由构造参数区分 Blur / Inner 与 Rect / RRect 四种组合（`asFragmentProcessor` 内分派到对应 FP），不新增第二个 Shader 子类与枚举值。

阶段 1~5 先落地 Drop shadow 并出图验证通路与外观，阶段 6~7 再补 Inner shadow——Inner 复用同一函数链与 Shader 接入方式，通路一经验证即可低风险扩展。

## 8 后续规划

本方案的核心产物是一套可复用的 **`RRect SDF + 闭式高斯覆盖率` fragment 模块**。以下扩展按适用面从小到大：

1. **Glow / 外发光**：offset = 0 的彩色 DropShadow，可直接复用 `RectBlur` / `RRectBlurFragmentProcessor`，仅 `shadowColor` 与 offset 取值不同，无需改 FP。
2. **形状高斯模糊**：对纯几何图形整体做 blur，需要的是裸覆盖率而非"覆盖率 × 阴影色"。因此不能直接复用现有 FP，需先把上色从 Blur 系中剥离——或将 `shadowColor` 改为可选（不设时输出 `vec4(coverage)`），或抽出只输出覆盖率的基类由阴影 FP 组合。改动集中在 `emitCode` 的末行与 uniform 声明，覆盖率函数链不动。
3. **让 contentShape 覆盖子图层内容，以解除 spread = 0 的限制**：各 `onGetContentShape()` 实现只交出图层自身形状，而 contour 图像录制整棵子树，二者范围不一致（2.3）。这正是快路径必须限定 spread ≠ 0 的原因（5.1）——含子图层的图层若按 `contentShape` 生成 spread = 0 的阴影，阴影会缩小到只剩自身轮廓。可行方向是让 `getContentShape()` 在存在可见子图层内容时聚合子树形状（能合成单一可解析形状则给出，否则返回 `nullopt` 让消费方回落）。这是本方案覆盖面最大的一块缺口：spread = 0 是阴影的多数用法，补齐后只需去掉 `drawAnalytic` 里的 spread 判定即可解锁，无需改动 FP 与 shader。同时它也会修正现状 spread ≠ 0 阴影"忽略子图层内容"的既有行为。
4. **从 Contour 的 PictureImage 解析 Shape**：当前只有 LayerStyle 能拿到矢量 Shape。后续支持从 Contour 的 PictureImage 反解出 Rect / RRect / Oval，则任何持有该 Contour 的路径（不限于 LayerStyle）都能走单 shader 快路径，把适用面从"图层阴影"扩大到"任意带轮廓来源的阴影 / 形状效果"。届时 4 个 FragmentProcessor 与形状识别逻辑可原样复用，只需新增 Contour → StyledShape 的解析入口。
5. **复杂圆角矩形（每角独立半径）**：数学上可行——per-corner 下每行横截面仍为区间，左右边界各自由本行角半径解析给出，`rowSpan` 由"单一对称半宽"推广为"按 y 区间分段选角 + 双边界"，闭式结构（内层 CDF 差 + 外层固定点数求积）不变；本方案的 `rowSpan` 已按椭圆双半轴实现（5.4），推广时每个角各带自己的一对半轴即可，无需再改半宽公式的形式。代价是 shader 分支增多、每行约两倍边界计算、失去象限对称折叠。Figma 的阴影同样只为统一圆角提供闭式单 shader，per-corner 形状回落其通用 tile 路径（3.3）。推广可按 `ComplexNonAARRectGeometryProcessor` 的分区思路进行。
6. **Stroke（描边）阴影的闭式优化**：理论上可用"外圈、内圈各调一次 `roundedRectShadow` 取差"实现（卷积线性），但本期不走，原因有三：① **RRect stroke 的同心近似误差**——闭式双圈把内圈视为 inset RRect（同心椭圆弧），而真实描边的内外边界是等距曲线（非椭圆），粗描边、扁椭圆（长短轴比 > 2）、半描边宽度超角半径时，同心近似在法线厚度上与真实描边误差显著（`Canvas.cpp` 的 `UseDrawPath` 正是检测这些失效场景并回落通用 stroker 的现有逻辑）；Rect stroke 无此问题（inset/outset 精确），但使用情况尚不确定。② Figma 未提供 stroke 专用闭式 shader（3.2 要点 4），本期跟随不走。③ 成本收益未明——闭式实现对 stroke 的覆盖受同心近似限制、且描边阴影使用频率低，成本能否被收益覆盖尚未可知。若未来评估值得做，仅需在 `RRectBlurFragmentProcessor` 加 Stroke 模式（外圈、内圈各调一次 `roundedRectShadow` 取差）。

7. **提高回落路径的核采样精度（独立改进）**：4 的精度结论显示，快 / 回落两路径的差异主要来自现状滤镜按整数像素偏移点采样源图，σ 小时对核欠采样（σ=1、硬边源时达 51 个灰度级），比快路径自身的近似误差（求积 1.49 + erf 0.12，均为灰度级）大一个数量级。若后续希望两路径在小 σ 下也逐像素接近，应改进的是**回落路径**——例如按 σ 自适应提高 tap 密度或改用双线性半像素偏移 tap。与本方案独立，收益覆盖所有走通用滤镜的效果（不限阴影）。

8. **erf 换为有界区间拟合多项式**：σ 归一化 + 核截断使 erf 实参恒在 `|z| ≤ k/√2` 内，该区间上 9 阶多项式即可达 3.5e-8，比已采用的 7.1.27（4.7e-4）高四个数量级，且同样无 `exp`、还省去一次除法；利用 erf 的奇函数性写成 `z·P(z²)` 可再省项数。本期不采用，因其系数需自行拟合、无文献出处，且与截断范围 `k` 隐式耦合。它是 5.2 升级序列的第二级——仅在 7.1.26 的 `exp` 开销也不可接受时才考虑；替换只需改 `erfApprox` 一个函数，但必须同时锁定 `k` 并在注释中标注该多项式的有效区间。

9. **统一的 dither 机制（独立改进）**：8-bit 输出下的长距离平缓渐变存在量化 banding，与本方案的近似精度无关（5.2）。若要消除，应在 TGFX 层面引入统一的 dither（Figma 的做法是给每个输出叠加约 `1/255 × ±0.5` 的哈希噪声），覆盖阴影、渐变、模糊等所有产生平缓过渡的路径，而非只给阴影加。需一并解决 baseline 截图带随机噪声后的比较容差问题。

## 9 参考资料

### 9.1 TGFX 项目内

| 路径 | 说明 |
|------|------|
| `src/layers/layerstyles/DropShadowStyle.cpp` | DropShadow 现有管线 |
| `src/layers/layerstyles/InnerShadowStyle.cpp` | InnerShadow 现有管线（drawWithSpread） |
| `src/layers/SpreadUtils.cpp` | spread 几何 outset / 光栅化折算，形状识别分派；光栅化阴影源带 1 px 线性 AA（2.4） |
| `src/layers/OpaqueContext.cpp` | LayerStyle 内容收集，硬边剪影（AA 刻意关闭 / AlphaThreshold 量化，2.4） |
| `src/gpu/glsl/processors/GLSLEllipseGeometryProcessor.cpp` | 形状 AA 线性过渡 `clamp(0.5 - t * invlen, 0, 1)`（2.4） |
| `src/core/filters/GaussianBlurImageFilter.cpp` | 可分离两 1D pass 高斯模糊，MAX_BLUR_SIGMA=10；`filterBounds` 按 2σ outset |
| `src/gpu/glsl/processors/GLSLGaussianBlur1DFragmentProcessor.cpp` | 现状核的实际形态：±2σ 截断 + 整数偏移 tap + 重新归一化（2.1）；亦为 GLSL emitCode / onSetData / uniform 绑定参考 |
| `src/gpu/glsl/processors/GLSLComplexNonAARRectGeometryProcessor.cpp` | 复杂圆角矩形分区 SDF |
| `src/gpu/processors/GaussianBlur1DFragmentProcessor.h` | FP 基类模式参考 |
| `src/gpu/processors/RRectEffect.h`、`src/gpu/glsl/processors/GLSLRRectEffect.cpp` | 已有的 RRect 解析覆盖率 FP（clip 用）：AA 线性过渡与 device-space 坐标重建范式 |
| `src/core/shaders/PerlinNoiseShader.h` / `.cpp` | 程序化 Shader → FP 的接入范式（5.3.2） |
| `src/gpu/ShaderBuilder.h` | `addFunction` / `getMangledFunctionName`：GLSL 辅助函数声明机制 |
| `designs/shadow-sdf-scripts/` | 本方案数值结论的实验脚本（6.3） |

### 9.2 外部参考

| 资源 | 说明 |
|------|------|
| Figma 渲染引擎 WASM（`compiled_wasm.wasm.br`） | **本方案 3.2 shader 的真实来源**；内嵌完整 GLSL/WGSL/WebGL shader 源码文本，经 `strings` + 结构化切分提取得到内外阴影 fragment 全文。提取产物清单与各文件内容见 3.3 |
| [Evan Wallace, "Fast Rounded Rectangle Shadows"](https://madebyevan.com/shaders/fast-rounded-rectangle-shadows/) | 闭式路线的公开源头（CC0），保留真高斯核；其 `erf()` 实现为 A&S 7.1.27 去 a₃ 项，是本方案 erf 选型的参考 |
| [Inigo Quilez, "2D distance functions"](https://iquilezles.org/articles/distfunctions2d/) | 2D 解析 SDF 公式（sdRoundBox 等） |
| Hastings, C. Jr. (1955), *Approximations for Digital Computers*, Princeton University Press | shader 侧 erf 逼近的原始出处（无官方在线全文；经 A&S 与 DLMF 转引，见下两行） |
| Abramowitz & Stegun, *Handbook of Mathematical Functions*（NBS Applied Mathematics Series 55, 1964），式 7.1.25~7.1.28，p. 299：[扫描原页](https://personal.math.ubc.ca/~cbm/aands/page_299.htm) / [全书扫描](https://personal.math.ubc.ca/~cbm/aands/frameindex.htm) / [NIST 说明页](https://www.nist.gov/mathematics-statistics/handbook-mathematical-functions-abramowitz-and-stegun) / [Internet Archive](https://archive.org/details/handbookofmathem1964abra) | 手册收录版本，适用域均为 `0 ≤ x < ∞`：**7.1.27**（本方案采用）误差界 `\|ε(x)\| ≤ 5×10⁻⁴`、纯有理式；7.1.26（备选升级）误差界 `\|ε(x)\| ≤ 1.5×10⁻⁷`、需 exp。页脚注 2 标注 7.1.25~7.1.28 均来自 Hastings (1955)；NBS（今 NIST）官方出版物、公共领域 |
| NIST DLMF [§7.24(i) Approximations in Terms of Elementary Functions](https://dlmf.nist.gov/7.24#i) ｜ [第 7 章](https://dlmf.nist.gov/7) ｜ [Hastings 文献条目](https://dlmf.nist.gov/bib/H#bib1056) | A&S 第 7 章的现行官方后继，仍将 Hastings (1955) 列为 erf 的推荐初等函数逼近 |
| `0527-阴影支持扩散能力`（`~/Desktop/Work/项目/2026/`） | spread 参数与 StyledShape 机制的前期方案；含 Figma WASM 反向分析（3.4.1），本方案的现状与几何来源承接自此 |
