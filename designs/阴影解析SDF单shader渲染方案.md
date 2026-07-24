# 可解析形状阴影效果单 Shader 性能优化方案

## 1 背景

TGFX 的内外阴影（DropShadow / InnerShadow）当前走"把形状光栅化成 alpha 纹理 → 多 pass 高斯模糊滤镜 → 上色"的通用滤镜路线：一次阴影绘制约 3~5 个 render pass、2~3 张中间纹理，且模糊半径受 `MAX_BLUR_SIGMA = 10` 约束，超出只能靠缩小 render target 近似、牺牲质量。对于图层轮廓本身就是 Rect / RRect 这类简单形状的常见场景，这条通用管线付出了不必要的多 pass 与纹理带宽代价。

本方案的目标：在**阴影数据源可解析为简单形状（Rect / RRect）**时，把内外阴影的渲染降为单 pass、零中间纹理，并解除模糊半径上限；不可解析的形状维持现有滤镜路径不变。

术语：

- **解析 SDF（Signed Distance Function）**：用闭式代数公式直接计算点到形状边界的有符号距离（外正内负），是逐点推导覆盖率（见下条）的输入；仅适用于 rect / rrect / circle 等简单几何，不依赖纹理采样或预处理。
- **覆盖率（coverage）**：fragment 落在形状内的比例 [0,1]。
- **spread（扩散）**：阴影在模糊之前的膨胀（正）/ 收缩（负），对已知形状是几何 outset / inset。
- **pass（render pass）**：绑定同一 render target 的一段连续渲染（期间可含多次 draw call）。多 pass 意味着中间纹理的写入与再采样开销。

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
- `LayerStyleInputSourceContour::shape()`：LayerStyle 在 `onDraw` 中通过 `input.extraSource` 取回 `std::optional<StyledShape>`。
- `SpreadUtils`（`src/layers/SpreadUtils.cpp`）：`path.isOval` / `path.isRRect` / `path.isRect` 分派，`MakeSpreadRRect` 做几何 outset / inset。

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

### 2.5 范围与扩展

本期只做内外阴影（DropShadow / InnerShadow）。依据：LayerStyle 是当前唯一能直接拿到图层轮廓 Shape 的位置（2.3 的 StyledShape / contentShape 机制）；且阴影本质是"形状 + 沿轮廓的柔和渐变"，最适合按形状解析处理。基于同一本质的其他效果（Glow、形状整体模糊等）也可同类优化，留待后续。

## 3 参考设计

对于能用代数式描述的简单形状（圆角矩形、椭圆），业界（Figma、部分浏览器）有另一条路线：在单个 fragment shader 内用**解析 SDF** 计算像素到形状边界的距离，再用高斯核的**闭式积分**直接算出模糊后的覆盖率，把形状求值、扩散（spread）、模糊、上色一次算完——单 pass、零中间纹理。

### 3.1 数学依据

阴影在语义上是**图层轮廓形状的纯色填充**——被模糊的信号是形状的二值 mask（内 1 外 0），而非图层的像素颜色，这正是该信号能被解析表达的前提（若模糊对象是任意彩色图像，信号无闭式表达，此路线不成立）。因此阴影模糊在数学上是**形状示性函数 `f`（形状内为 1、外为 0 的二值函数）与二维各向同性高斯核的卷积**：

```
C(p) = (f ∗ Gσ)(p) = ∬ f(q) · Gσ(p − q) dq
Gσ(x,y) = 1/(2πσ²) · exp(−(x²+y²)/(2σ²))
```

`Gσ` 即二维正态分布的概率密度，全空间积分为 1（权重归一，模糊前后图像总量不变，平坦区域不被压暗）。本方案的可行性建立在三条标准事实上：

1. **高斯可分离**：`Gσ(x,y) = gσ(x)·gσ(y)`（各向同性、单一 σ），二维卷积可按 Fubini 定理拆成两个一维积分的嵌套。
2. **一维高斯积分有闭式**：累积分布函数 `Φσ(u) = ∫₋∞ᵘ gσ(t)dt = ½[1 + erf(u/(σ√2))]`，区间质量 `∫ₐᵇ gσ = Φσ(b) − Φσ(a)`。被卷积的 `f` 有限支撑——形状外恒为 0、对卷积无贡献，积分区间天然有限，故闭式求值无需采样卷积那种"截断后重新归一化"的修正。
3. **卷积线性**：`f = f₁ − f₂ ⇒ C[f] = C[f₁] − C[f₂]`，环形（外圈减内圈）与 InnerShadow（乘反相覆盖率）由此成立。

**矩形（完全可分离 ⇒ 纯闭式）**：示性函数 `f = 1[x₀,x₁](qx) · 1[y₀,y₁](qy)` 变量分离，二重积分拆为两个一维积分之积，得纯闭式解，O(1)、零采样：

```
C(p) = [Φσ(px−x₀) − Φσ(px−x₁)] · [Φσ(py−y₀) − Φσ(py−y₁)]
```

**圆角矩形（逐行可分离 ⇒ 一维闭式 + 常数点数值积分）**：圆角使 `f` 不再完全分离，但固定一行 `y`，横截面仍是对称区间 `[−w(y), +w(y)]`，半宽解析：

```
直边区：w(y) = halfX
圆角区：w(y) = halfX − r + √(r² − (|y| − (halfY−r))²)
```

内层（沿 x）仍闭式（CDF 差，即 3.3 的 `rowSpan`）；外层（沿 y）因 `w(y)` 含根号无初等原函数，做数值积分。高斯核在 ±3σ 外指数衰减，有效积分区间宽度固定；所有几何量预除 blurRadius 归一化后 σ≡1，**固定 N=4 点中点求积即落入 8-bit 视觉容差，与模糊半径无关**：

```
C(p) ≈ [ Σᵢ wᵢ · rowSpan(px, sᵢ) ] · (Φ(end) − Φ(start)) / Σᵢ wᵢ
```

`/Σᵢwᵢ` 是数值求积的权重归一化（采样卷积截断修正在本方案中唯一保留的位置）；`×(Φ(end)−Φ(start))` 用连续 CDF 差给出该区间的真实高斯质量，把采样估计校准回真值。

**抗锯齿与模糊的统一视角**：AA 与阴影都是对同一 {0,1} 理想形状的卷积，仅核带宽不同——AA = 卷约 1 px 窄核（`aaCoverage` 的 ±0.5 px `smoothstep`），阴影 = 卷 blurRadius 宽核。二者在同一解析边界上并存，故支持 AA 不破坏闭式性。

**与 3.3 Figma 函数的对应**：`gaussianCDF` ↔ `Φ` 的分段二次近似；`triWeight` ↔ 求积权重 `wᵢ`；`rowSpan` ↔ 内层闭式积分；`roundedRectShadow` ↔ 外层 4 点求积；`axisAlignedCoverage` ↔ 矩形纯闭式解。

### 3.2 解析 SDF 基础设施

TGFX 内已存在一整套用解析 SDF 绘制圆角矩形 / 椭圆的 GeometryProcessor（2026 年新代码），本方案在其 SDF 骨架上扩展，无需从零实现 SDF。

| 组件 | 文件 | SDF 内容 |
|------|------|---------|
| NonAARRectGeometryProcessor | `src/gpu/glsl/processors/GLSLNonAARRectGeometryProcessor.cpp` | 圆角矩形 SDF；fill 用外 SDF，stroke 用外/内 SDF 差 |
| ComplexNonAARRectGeometryProcessor | `src/gpu/glsl/processors/GLSLComplexNonAARRectGeometryProcessor.cpp` | 每角独立半径的分区 SDF |
| ComplexEllipseGeometryProcessor | `src/gpu/processors/ComplexEllipseGeometryProcessor.h` | 角区椭圆 SDF + 边线线性距离 + 内部满覆盖 |

现有代码印证（`GLSLNonAARRectGeometryProcessor.cpp:87-118`）：fragment 阶段已用圆角矩形 SDF 算覆盖率，fill 取外 SDF，stroke 取外/内 SDF 差：

```glsl
// 现有代码：圆角矩形 SDF（归一化到单位圆角）
vec2 q = abs(localCoord - center) - halfSize + radii;
float d = min(max(q.x / radii.x, q.y / radii.y), 0.0) + length(max(q / radii, 0.0)) - 1.0;
float outerCoverage = step(d, 0.0);            // 硬边覆盖率
// stroke：innerCoverage 同理，coverage = outerCoverage * (1.0 - innerCoverage)
```

**核心观察**：现有 SDF shader 只把距离用于硬边 / AA 覆盖率（`step`）。把 `step(d, 0)` 换成 **SDF 与高斯核的闭式积分**，同一套 SDF 骨架即产出模糊覆盖率，且天然覆盖 fill / stroke（环形）两种语义。

### 3.3 Figma 阴影 shader 实现

Figma 的 `RoundedRectangleBlur` / `RoundedRectangleInnerShadow` fragment shader 源码已从其渲染引擎 WASM 中提取。核心思路与 Evan Wallace 的公开方法同源——**沿短轴用分段闭式积分、沿长轴用固定 4 采样积分**，常数时间、单 pass——但 Figma 在近似函数与采样策略上做了工程优化，直接采用其真实实现比用 erf 近似更贴合目标。以下为去 minify（变量还原语义名）后的源码。

**高斯 CDF 用分段二次多项式近似（非 erf）：**

Figma 不用 erf，而用一个三段二次多项式近似高斯的累积分布，成本更低、导数连续：

```glsl
// gaussianCDF：高斯累积分布的分段二次近似，定义域外裁剪到 [0,1]
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

给定一行（相对中心归一化坐标），算该行圆角矩形的半宽，再用 `gaussianCDF` 的差得到该行被模糊后的水平覆盖率。所有坐标已按 blurRadius 归一化（见下文 uniform）：

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

**圆角矩形阴影覆盖率（沿长轴积分）：**

Figma 按 `abs(coord.x) > abs(coord.y)` 选择沿**长轴**做 4 采样积分（Wallace 固定沿 Y），减少采样落在信号区外的浪费，最后用 `(gaussianCDF(end)-gaussianCDF(start)) / Σweight` 归一化：

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

**矩形特例的纯闭式覆盖（公共函数，未被调用）：**

提取源码中还有一个公共函数，给出轴对齐矩形的**完整闭式**模糊覆盖——两个方向各取一次 CDF 差相乘，无需任何采样：

```glsl
// axisAlignedCoverage：edges 为矩形四边在目标点坐标系下、按 blurRadius 归一化的坐标
float axisAlignedCoverage(vec4 edges) {
  return (gaussianCDF(edges.x) - gaussianCDF(edges.z))
       * (gaussianCDF(edges.y) - gaussianCDF(edges.w));
}
```

该函数在两个阴影 shader 中均**只有定义、没有调用点**——阴影统一走 `roundedRectShadow`，其 `corner` 退化为 0 时结果就是矩形闭式解。收录于此作为矩形特例完全可闭式的佐证。

**DropShadow 主体（`RoundedRectangleBlur`）：**

内部实心区直接取满覆盖（跳过积分），只在边缘带走 `roundedRectShadow`：

```glsl
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

**uniform 归一化约定**：Figma 把几何量预除 blurRadius 传入——`halfSizeOverBlurRadius = halfSize / blurRadius`、`cornerRadiusOverBlurRadius = corner / blurRadius`，坐标 `coord = (fragPos - center) / blurRadius`。这样 shader 内 sigma 恒为 1，无需单独传 sigma，也天然无 `MAX_BLUR_SIGMA` 上限。**spread 在 CPU 端 outset 进 `halfSize` / `corner` 后再归一化，shader 不感知 spread。**

采样数固定为 4，**与模糊半径无关**，无方案 A 那种大半径采样爆炸问题，也无需 JFA 距离场。该实现用单一 `corner`（各角同半径），TGFX 侧对每角独立半径的 RRect 需按现有 `ComplexNonAARRect` 的分区思路推广。

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

**InnerShadow 主体（`RoundedRectangleInnerShadow`）：**

InnerShadow 复用同一 `roundedRectShadow`，取反后乘图层原形状覆盖率，把阴影裁进形状内部：

```glsl
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

`maskCoverage` 印证了本方案 5.2 的 Inner 公式 `shadowColor * shapeCoverage * (1 - shadowCoverage)`——Figma 用独立的圆角矩形 SDF（`roundRectSDF`）算图层形状覆盖率，`useAAAMask` 控制是否做 `fwidth` 抗锯齿。

**Figma 的 AA 策略归纳**：DropShadow 主体无 AA 过渡函数——阴影边缘由 ±3σ 高斯衰减天然平滑，无需 1 px AA；InnerShadow 的图层形状裁剪边由 `useAAAMask` 二选一——`aaCoverage`（`smoothstep` 三次过渡）或 `1.0 - step` 硬边。与 2.4 对照：TGFX 的形状 AA 统一为线性过渡，Figma 用三次 `smoothstep`，两者在过渡带内最大相差 ±0.096（8-bit 约 24 级）。

### 3.4 Figma WASM shader 源码提取

Figma 渲染引擎 WASM（`compiled_wasm.wasm.br`）**内嵌了完整的 shader 源码文本**（GLSL-ES3 / WebGL1 / WGSL 三套变体，变量名经 minify 压缩为单字母，但结构完整可读），并非编译后的字节码。该文件实为未压缩的 WASM 二进制（文件头即 `\0asm` 魔数，`.br` 后缀名不副实），直接 `strings` + 结构化切分即可拿到内外阴影 shader 的完整源码。提取产物：

| 文件 | 内容 |
|------|------|
| `~/Desktop/Work/figma_shaders/RoundedRectangleBlur.frag.glsl` | DropShadow fragment 全文 |
| `~/Desktop/Work/figma_shaders/RoundedRectangleInnerShadow.frag.glsl` | InnerShadow fragment 全文 |
| `~/Desktop/Work/figma_shaders/RoundedRectangleBlur_all.txt` | 同一 shader 的 WGSL / WebGL1 / GLSL-ES3 三变体 |

提取结果与前期调研（`0527` 方案 3.4.1）推断的 uniform 结构（`halfSizeOverBlurRadius` / `cornerRadiusOverBlurRadius` / `shadowBounds` / `maskBounds` / `shadowHalfSize` / `maskHalfSize` / `shadowCornerRadius` / `maskCornerRadius` / `blurRadius` / `invBlurRadius` / `useAAAMask`）完全吻合，并进一步确认了两点此前无法确定的实现细节：

1. **高斯 CDF 用分段二次多项式而非 erf**（`gaussianCDF`），采样权重用三角窗 `triWeight` 而非 `exp`。
2. **按长轴自适应选择积分方向**，并对内部实心区跳过积分直接取满覆盖。

3.3 的 shader 以该真实源码为准。

### 3.5 证据来源与结论修正

关于 "Figma 的 RoundedRectangleBlur 是否就是 Evan Wallace 公开算法原样实现" 的结论已做证据分级与修正：

1. **早期结论（推测）**：仅基于公开文章与 shader 命名、uniform 形态相似性，判断两者算法同源，但无法证明实现细节完全一致。
2. **当前结论（已验证）**：基于 `compiled_wasm.wasm.br` 的真实 shader 提取结果（见 3.4），确认 Figma 与 Evan Wallace 方法**同源但非原样拷贝**，至少存在以下可验证差异：
   - `gaussianCDF` 采用分段二次近似，而非 erf；
   - 采样权重用三角窗 `triWeight`，而非 `exp`；
   - 积分方向按长轴自适应选择，并对内部实心区走快速满覆盖路径。
3. **文档取证优先级**：后续若出现描述冲突，按 "Figma WASM 提取源码 > 公开文章推断" 处理，以可复现源码证据为准。

## 4 对比分析

| 方案 | spread 后形状 | stroke spread | 每像素采样成本 | DropShadow pass | InnerShadow pass | 中间纹理 | sigma 上限 | 可解析形状外兜底 | 实现复杂度 |
|------|--------------|---------------|---------------|-----------------|------------------|---------|-----------|-----------------|-----------|
| A：暴力卷积 | 圆变方 ❌ | 不正确 ❌ | O(R²)（R=100 时约 3 万） | 1 | 1 | 0 | 无 | 无 | 中 |
| B：几何 outset + 滤镜（现状） | 精确 ✅ | 外扩内缩 ✅ | 光栅 1 次 + blur 卷积 O(R) | 3 | 4~5 | 2~3 张 | MAX_BLUR_SIGMA=10，超出降质 | 通用（任意 path / 容器） | 低（已落地） |
| C：解析 SDF 单 shader（本方案） | 精确 ✅ | 外扩内缩（双 SDF 差）✅ | 固定 4（与 R 无关） | 1 | 1 | 0 | 无 | 回落方案 B | 中（新增 1 FP + GLSL，复用现有 SDF） |

**结论**：方案 C 在可解析形状（Rect / RRect）上以更少的 pass、零中间纹理、无 sigma 上限降质的方式渲染阴影；其余形状回落已落地的方案 B。二者是"简单形状快路径 + 通用兜底"的分层关系。

## 5 实现方案

### 5.1 总体思路

在阴影绘制末端，当 StyledShape 可解析为 Rect / RRect 时，构造一个新的 FragmentProcessor 单 pass 绘制阴影；否则回落方案 B。spread 仍在 CPU 端 outset 进 SDF 参数（复用 `SpreadUtils::MakeSpreadRRect` 的几何折算），shader 不感知 spread。

### 5.2 ShadowRRectFragmentProcessor

新增 `src/gpu/processors/ShadowRRectFragmentProcessor.h/.cpp` 与对应 `src/gpu/glsl/processors/GLSLShadowRRectFragmentProcessor.h/.cpp`，参照 `GaussianBlur1DFragmentProcessor` 的 `DEFINE_PROCESSOR_CLASS_ID` / `onComputeProcessorKey` / `emitCode` / `onSetData` 结构。

**输出模式（枚举，非 shader 感知 spread）：**

```cpp
enum class ShadowRRectMode {
  Drop,   // 输出 shadowColor * roundedRectShadow(shadow*)
  Inner   // 输出 shadowColor * maskCoverage * (1 - roundedRectShadow(shadow*))，裁进图层内部
};
```

**uniform（均由 CPU 计算，已含 spread，几何量预除 blurRadius 归一化，对齐 Figma）：**

| uniform | 类型 | 含义 |
|---------|------|------|
| `shadowHalfOverBlur` | Float2 | spread 形变后半尺寸 / blurRadius（对应 Figma `halfSizeOverBlurRadius`） |
| `shadowCornerOverBlur` | Float | spread 形变后角半径 / blurRadius（对应 `cornerRadiusOverBlurRadius`） |
| `blurRadius` | Float | 模糊半径（`blurriness * contentScale`；归一化后 shader 内 sigma 恒为 1，无 10 上限） |
| `shadowColor` | Float4 | 预乘阴影色 |
| `maskHalfSize` | Float2 | 图层原形状半尺寸（仅 Inner，非归一化） |
| `maskCornerRadius` | Float | 图层原形状角半径（仅 Inner） |
| `invBlurRadius` | Float | `1 / blurRadius`（仅 Inner，用于 shadow 坐标归一化） |
| `useAAAMask` | Int | 图层形状覆盖率是否走 `fwidth` 抗锯齿（仅 Inner） |

坐标 `coord = (fragPos - center) / blurRadius` 通过坐标 transform 处理（含 DropShadow 的 offset），与现有 `GLSLGaussianBlur1DFragmentProcessor::onSetData` 用 `coordTransform` 映射一致，不单列 offset uniform。

**fragment 核心（3.3 提取的 Figma 真实 shader）：**

```glsl
// 坐标与几何均已按 blurRadius 归一化（见 3.3 uniform 约定）
// Drop：
coverage = roundedRectShadow(coord, shadowHalfOverBlur, shadowCornerOverBlur);
outColor = shadowColor * coverage;

// Inner：maskCoverage 为图层原形状覆盖率（mask*），shadowCoverage 为 spread 穿透形状的模糊覆盖率
shadowCoverage = roundedRectShadow(shadowCoord, shadowHalfOverBlur, shadowCornerOverBlur);
maskSDF        = roundRectSDF(maskCoord, maskHalfSize, maskCornerRadius);
maskCoverage   = useAAAMask ? aaCoverage(maskSDF) : 1.0 - step(0.0, maskSDF);
outColor = shadowColor * maskCoverage * (1.0 - shadowCoverage);   // 形状内、且在模糊穿透区之外
```

Inner 用"形状覆盖率 × 反相的模糊穿透覆盖率"，与现有 `InnerShadowStyle`（`shape_alpha * (1 - blurredMask) * color`，`InnerShadowStyle.cpp:193-198`）语义一致，也与 3.3 提取的 Figma `RoundedRectangleInnerShadow` 真实 `main()` 完全一致。双圆角矩形（`mask*` = 图层原形状裁剪范围，`shadow*` = spread 穿透形状）与 Figma 结构对应。 `roundRectSDF` / `aaCoverage` 的定义见 3.3。

**AA 处理沿用现状，不引入新逻辑**：本方案不修改 TGFX 现有 AA 行为（2.4）。InnerShadow 的 `maskCoverage` 抗锯齿用与 `EllipseGeometryProcessor` 一致的线性过渡 `clamp(0.5 - sdf * invPixelWidth, 0.0, 1.0)`，不采用 Figma 参考实现（3.3）的 `smoothstep` 三次过渡，TGFX 全生态保持单一线性 AA 风格。两种过渡的差异仅在过渡带约 1/4 处取极值 ±0.096（8-bit 约 24 级），只作用于 InnerShadow 的 `maskCoverage` 1 px 截止带；DropShadow 公式无 AA 过渡函数参与，不受影响。

**Stroke（环形）**：`shadow*` 传外圈、内圈两组，覆盖率取外圈 `roundedRectShadow` 减内圈，与现有 stroke 双 SDF 差（3.2）同源。spread 分别 outset 外圈、inset 内圈。

### 5.3 DropShadowStyle / InnerShadowStyle 集成

`DropShadowStyle::onDraw`：spread 非零（或需精确形状阴影）且 StyledShape 可解析为 Rect / RRect 时，构造 `ShadowRRectFragmentProcessor(Drop)` 单 pass 绘制，旁路 `SpreadUtils::MakeSpreadShapeImage` + blur 滤镜；否则回落现有路径。spread → (bounds, corner) 复用 `MakeSpreadRRect` 的折算，避免两条路径几何不一致。

`InnerShadowStyle::onDraw`：同理走 `ShadowRRectFragmentProcessor(Inner)`，传图层原形状（mask）+ spread 内缩形状（shadow）两组参数。

`filterBounds`：DropShadow 仍按 spread + blur outset srcRect（现有逻辑不变，单 shader 绘制范围与其一致）；InnerShadow 返回 srcRect。

### 5.4 形状识别与回落

复用 `SpreadUtils` 已有的 `path.isOval` / `path.isRRect` / `path.isRect` 分派：

- `isRect` / `isRRect` → 直接取 bounds + radii 走单 shader。
- `isOval` 且为**正圆**（宽高相等）→ 角半径 = 半径的 RRect，走单 shader。
- 非圆真椭圆、非 Rect/RRect/圆的 path、多形状容器 → 回落方案 B。

## 6 测试计划

### 6.1 截图测试

复用并对齐现有阴影 baseline，验证单 shader 路径与方案 B 视觉一致：

| 用例 | 形状 | 样式 | 阴影 | spread | blur | 预期 |
|------|------|------|------|--------|------|------|
| 1 | RRect | fill | Drop | 0 | 有 | 与现有 baseline 一致 |
| 2 | RRect | fill | Drop | +10 | 有 | 四周扩大，圆角相应增大 |
| 3 | RRect | fill | Drop | -10 | 有 | 四周缩小 |
| 4 | 正圆 | fill | Drop | +10 | 有 | 圆扩大，仍是完美圆 |
| 5 | 正圆 | stroke | Drop | +10 | 有 | 环形：外扩内缩 |
| 6 | RRect | fill | Inner | +10 | 有 | 内阴影向内扩展，孔洞缩小 |
| 7 | RRect | fill | Inner | -10 | 有 | 内阴影向外收缩 |
| 8 | Rect | fill | Drop | +10 | 0 | 硬边扩展矩形（无 blur） |
| 9 | RRect | fill | Drop | +10 | 大 sigma | 大模糊不受 MAX_BLUR_SIGMA=10 降质 |
| 10 | 非圆椭圆 | fill | Drop | +10 | 有 | 回落方案 B，形状仍为椭圆 |
| 11 | 复杂 path | fill | Drop | +10 | 有 | 回落方案 B（退化 bounds） |

**截图基准变更需由用户主动执行 `/accept-baseline`，本方案不自行接受。**

### 6.2 边界情况

- spread = 0：与现有行为一致（可选择仍走单 shader 或保持原路径）。
- blur = 0 + spread：硬边扩展形状。
- 负 spread 导致形状收缩消失：coverage 全 0，不绘制。
- 大 sigma（> 10）：单 shader 直接闭式计算，验证不降质。
- 单 shader 与方案 B 在边界像素的一致性（AA 边缘）。

## 7 实现步骤

| 阶段 | 文件 | 说明 |
|------|------|------|
| 1 | `src/gpu/processors/ShadowRRectFragmentProcessor.h` + `.cpp` | 新增 FP：`ShadowRRectMode`（Drop/Inner）、uniform（shadowHalfOverBlur/shadowCornerOverBlur/blurRadius/shadowColor/maskHalfSize/maskCornerRadius/invBlurRadius/useAAAMask）、`DEFINE_PROCESSOR_CLASS_ID` / `onComputeProcessorKey`，参照 `GaussianBlur1DFragmentProcessor` |
| 1 | `src/gpu/glsl/processors/GLSLShadowRRectFragmentProcessor.h` + `.cpp` | `emitCode` 移植 3.3 的 Figma 真实实现：`gaussianCDF`（分段二次）/ `triWeight`（三角窗）/ `rowSpan` / `roundedRectShadow`（长轴 4 采样）；Drop 输出 `shadowColor * coverage`、Inner 输出 `shadowColor * maskCoverage * (1 - shadowCoverage)`、Stroke 输出外/内差；`onSetData` 绑定 uniform 与坐标 transform |
| 2 | `src/layers/SpreadUtils.h` + `.cpp` | 抽出 spread → (bounds, corner) 的纯计算供单 shader 复用，与 `MakeSpreadRRect` 共享同一折算逻辑；新增识别"正圆"的判定 |
| 3 | `src/layers/layerstyles/DropShadowStyle.cpp` | `onDraw` 中可解析形状走 `ShadowRRectFragmentProcessor(Drop)` 单 pass，否则回落方案 B |
| 3 | `src/layers/layerstyles/InnerShadowStyle.cpp` | `onDraw` 中可解析形状走 `ShadowRRectFragmentProcessor(Inner)`，传 mask + shadow 两组参数，否则回落 |
| 4 | `test/src/` | 截图测试（6.1）与边界（6.2） |

## 8 后续规划

本方案的核心产物是一套可复用的 **`RRect SDF + 闭式高斯覆盖率` fragment 模块**。以下扩展按适用面从小到大：

1. **Glow / 外发光**：offset = 0 的彩色 DropShadow，复用 `ShadowRRectFragmentProcessor(Drop)`，仅参数不同。
2. **形状高斯模糊**：对纯几何图形整体做 blur，等价于阴影去上色、直接输出 `roundedRectShadow` 覆盖率。
3. **从 Contour 的 PictureImage 解析 Shape**：当前只有 LayerStyle 能拿到矢量 Shape。后续支持从 Contour 的 PictureImage 反解出 Rect / RRect / Oval，则任何持有该 Contour 的路径（不限于 LayerStyle）都能走单 shader 快路径，把适用面从"图层阴影"扩大到"任意带轮廓来源的阴影 / 形状效果"。届时 `ShadowRRectFragmentProcessor` 与形状识别逻辑可原样复用，只需新增 Contour → StyledShape 的解析入口。
4. **复杂圆角矩形（每角独立半径）**：3.3 的参考实现用单一角半径，可按 `ComplexNonAARRectGeometryProcessor` 的分区思路推广到每角独立半径。

## 9 参考资料

### 9.1 TGFX 项目内

| 路径 | 说明 |
|------|------|
| `src/layers/layerstyles/DropShadowStyle.cpp` | DropShadow 现有管线 |
| `src/layers/layerstyles/InnerShadowStyle.cpp` | InnerShadow 现有管线（drawWithSpread） |
| `src/layers/SpreadUtils.cpp` | spread 几何 outset / 光栅化折算，形状识别分派；光栅化阴影源带 1 px 线性 AA（2.4） |
| `src/layers/OpaqueContext.cpp` | LayerStyle 内容收集，硬边剪影（AA 刻意关闭 / AlphaThreshold 量化，2.4） |
| `src/gpu/glsl/processors/GLSLEllipseGeometryProcessor.cpp` | 形状 AA 线性过渡 `clamp(0.5 - t * invlen, 0, 1)`（2.4） |
| `src/core/filters/GaussianBlurImageFilter.cpp` | 可分离两 1D pass 高斯模糊，MAX_BLUR_SIGMA=10 |
| `src/gpu/glsl/processors/GLSLNonAARRectGeometryProcessor.cpp` | 圆角矩形解析 SDF 覆盖率（可复用骨架） |
| `src/gpu/glsl/processors/GLSLComplexNonAARRectGeometryProcessor.cpp` | 复杂圆角矩形分区 SDF |
| `src/gpu/processors/ComplexEllipseGeometryProcessor.h` | 椭圆 / 复杂圆角矩形 AA SDF |
| `src/gpu/processors/GaussianBlur1DFragmentProcessor.h` | FP 基类模式参考 |
| `src/gpu/glsl/processors/GLSLGaussianBlur1DFragmentProcessor.cpp` | GLSL emitCode / onSetData / uniform 绑定参考 |

### 9.2 外部参考

| 资源 | 说明 |
|------|------|
| Figma 渲染引擎 WASM（`compiled_wasm.wasm.br`） | **本方案 3.3 shader 的真实来源**；内嵌完整 GLSL/WGSL/WebGL shader 源码文本，经 `strings` + 结构化切分提取得到内外阴影 fragment 全文（见 3.4） |
| `~/Desktop/Work/figma_shaders/` | 从上述 WASM 提取的 shader 源码文件：`RoundedRectangleBlur.frag.glsl`、`RoundedRectangleInnerShadow.frag.glsl`、`RoundedRectangleBlur_all.txt`（三语言变体） |
| Evan Wallace, "Fast Rounded Rectangle Shadows"（madebyevan.com/shaders/fast-rounded-rectangle-shadows/） | 同源公开方法（CC0），算法结构参考；Figma 真实实现在近似函数上有优化（分段二次替代 erf、三角窗替代 exp、长轴自适应） |
| Inigo Quilez, "2D distance functions"（iquilezles.org/articles/distfunctions2d/） | 2D 解析 SDF 公式（sdRoundBox 等） |
| `0527-阴影支持扩散能力`（`~/Desktop/Work/项目/2026/`） | spread 参数与 StyledShape 机制的前期方案；含 Figma WASM 反向分析（3.4.1），本方案的现状与几何来源承接自此 |
