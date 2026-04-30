# Analytic 裁剪 FP 覆盖率优化

## 1. 背景

### 问题描述

`OpsCompositor::applyClip(const ClipStack& clipStack)` 将每个裁剪元素落到三条通道之一：

1. **Scissor**：像素对齐且开启 AA 的轴对齐矩形，或者未开启 AA 的轴对齐矩形，交由 scissor 硬裁剪
2. **Analytic FP**：开启 AA 的轴对齐矩形走 `AARectEffect`，在 fragment shader 里算平滑覆盖率
3. **Mask 纹理**：其余元素（非轴对齐矩形、任意圆角矩形、一般路径）统一走 `makeClipTexture` 光栅化到离屏纹理，再用 `DeviceSpaceTextureEffect` 采样

> **关于"轴对齐矩形"的判定**：由 `path.isRect()` 决定，实测接受**恒等 / 平移 / 正缩放 / 镜像 / 90° 倍数旋转**（只要变换后顶点仍轴对齐就返回 true）；**任意角度旋转、错切、透视**下返回 false。详见 `PathTest.IsRectUnderTransforms`。因此现状代码对"旋转 90°"、"镜像"这类特殊矩阵下的矩形已经命中 Scissor / Analytic FP 快路径，真正被迫走 mask 纹理的是任意角度旋转、错切、透视的矩形，以及所有非轴对齐的 RRect。

第三条通道开销显著：需要额外申请 RenderTarget、提交离屏 `OpsRenderTask`、对每个元素做一次 shape 光栅化，最终再采一次纹理。对于"几何解析可直接求覆盖率"的场景（旋转矩形、任意 RRect），生成纹理是浪费。

Skia 存在两个 GPU 后端 **Ganesh** 和 **Graphite**，它们处理裁剪区的策略不一样；当前 TGFX 的架构和 **Ganesh 更接近**，本方案以 Ganesh 为参考。Ganesh 对同类场景直接走 analytic FP：

- 轴对齐 RRect → `GrRRectEffect`（按 RRect 子类型分派到圆形或椭圆 FP，详见第 3.2 节）
- 带仿射变换的矩形 → `GrConvexPolyEffect`（用不超过 8 条边线性方程求覆盖率）
- 恒等变换矩形 → `GrFragmentProcessor::Rect`（与当前 `AARectEffect` 等价）

只要命中这几类快路径，就不需要离屏纹理。

> **补充说明：为何不参考 Graphite**
> Graphite 走的是**深度缓冲路线**——analytic clip 只保留 1 个且仅支持 identity 变换；不能解析的元素生成 depth-only draw 写入深度缓冲，后续 draw 用硬件深度测试自动被裁掉；另外还做几何求交（`Rect∩Rect`、`RRect∩RRect`）直接简化 draw 形状。这套方案依赖 `BoundsManager` 的全局 painters order 分配。
> TGFX 当前没有 painters order / 深度缓冲统一分配机制，引入 Graphite 路线需要改 DrawList 排序和 Z 值分配，工程量远超当前目标；且 Graphite 砍掉了椭圆角 RRect / 凸多边形的 analytic 能力，直接复刻到 TGFX 反而丢失优化收益。**Graphite 的几何求交和深度缓冲方案留作后续架构演进方向。**


### 需求

在 `applyClip` 中扩展 analytic FP 判定：带任意仿射矩阵的矩形、任意 RRect（轴对齐，任意四角半径）改走 FP。不能用 FP 处理的（一般路径、带透视的 RRect、带透视的非矩形）仍然走 mask 纹理。

> 本次重构**不处理透视矩阵**：带透视的矩形/RRect 统一降级到 mask 纹理。原因：Skia 的 `GrConvexPolyEffect` 用线性边方程 `ax + by + c` 做距离场，仅在仿射空间成立；透视下边界不再是线性函数。引入透视 FP 需要单独设计 shader，超出当前优化范围。

### 设计决策

1. **保留 mask 纹理兜底**：FP 判定失败时仍走 `getClipMaskFP`，行为等价于现状
2. **ClipElement 结构扩展**：保存源几何分类（Rect / RRect / Path）和源几何数据，使 `applyClip` 能拿到未烘焙矩阵前的 RRect 信息；同时记录一个"从源几何空间到设备空间"的矩阵供 FP 使用
3. **新增两个 FP**：
   - `AARRectEffect`：轴对齐 RRect 的 analytic 覆盖率（四角半径可各不相同）
   - `AAConvexPolyEffect`：用线性边方程组表示的凸多边形覆盖率，用于带仿射矩阵的矩形（4 条边）和 RRect 切角/变换后的等价凸包（暂只用于矩形）
4. **最大 FP 数量限制**：参考 Skia 的 4 个 analytic FP 上限；超出则后续元素降级到 mask 纹理。原因：FP 链越长着色器越复杂，4 个覆盖了绝大多数实际场景

### 术语定义

| 术语 | 含义 |
|------|------|
| Analytic FP | 在 fragment shader 内直接由几何参数计算覆盖率的 FragmentProcessor |
| Axis-aligned | 轴对齐（矩阵只含平移和非负缩放，不含旋转/错切/翻转引起的轴变化） |
| Convex Poly | 凸多边形，每条边用 `a*x + b*y + c >= 0` 表达 |
| RRect | 四角可独立半径的轴对齐圆角矩形（对应 `tgfx::RRect`） |

---

## 2. 现有实现分析

### 2.1 ClipStack 数据流

`Canvas::clipPath` 在入栈前就把 `_matrix` 烘焙进 path：

```cpp
// src/core/Canvas.cpp:192
void Canvas::clipPath(const Path& path, bool antiAlias) {
  auto clipPath = path;
  clipPath.transform(_matrix);        // 矩阵直接作用在 path 上
  clipStack->clip(clipPath, antiAlias);
}
```

`ClipElement` 只保存 `Path _path`、`Rect _bounds`、`bool _isRect`（来自 `path.isRect(nullptr)`）：

```cpp
// src/core/ClipStack.h:110-116
Path _path = {};
bool _antiAlias = false;
Rect _bounds = {};
bool _isRect = false;
int _invalidatedByIndex = -1;
```

**关键结论**：矩阵已烘焙进 path，源几何与矩阵不再可分开访问。对于"任意角度旋转/错切/透视的矩形"和"所有非轴对齐矩阵下的 RRect"，`path.isRect` / `path.isRRect` 均返回 false，RRect 的圆角信息也从 path 结构里丢失。这两类元素目前被迫全部走 mask 纹理——正是本次要补齐的场景。

### 2.2 applyClip 现状

`src/gpu/OpsCompositor.cpp:729-786`：

```
Stage 3 元素分派：
  isRect && (pixelAligned || !AA)  → 跳过（scissor 覆盖）
  isRect && AA                     → makeAnalyticFP → AARectEffect
  其他                             → 收集到 elementsForMask
Stage 4:
  elementsForMask 非空 → getClipMaskFP（离屏光栅化）
```

`makeAnalyticFP` 内部注释已标注 "Future extension: RRectEffect, etc."，预留了扩展点。

### 2.3 现有 AARectEffect 着色器

`src/gpu/glsl/processors/GLSLAARectEffect.cpp:29-49`：

```glsl
vec4 dists4 = clamp(vec4(1,1,-1,-1) * (gl_FragCoord.xyxy - Rect), 0, 1);
vec2 dists2 = dists4.xy + dists4.zw - 1.0;
float coverage = dists2.x * dists2.y;
```

CPU 侧把矩形外扩 0.5 像素。基于 `gl_FragCoord`，是**设备空间**的 AA 距离场。

### 2.4 mask 纹理开销分析

`makeClipTexture` (`src/gpu/OpsCompositor.cpp:821-862`)：

- 申请 `width × height` 大小的 `RenderTargetProxy`（可能几百万像素）
- 为每个元素生成 `ShapeDrawOp`，走完整 tessellator 或 mask 光栅化管线
- 通过 `addOpsRenderTask` 异步提交到 drawing manager
- 消费端再通过 `DeviceSpaceTextureEffect` 采样

一次典型的"AA RRect 裁剪"目前走 mask 纹理，成本 ≥ 1 次离屏 pass + 1 次 shape 光栅化 + 1 次纹理采样。优化后降为 1 个 FP 计算。

---

## 3. 参考设计（Skia Ganesh）

### 3.1 全局分派

`src/gpu/ganesh/ClipStack.cpp` 的 `apply()` 主循环按四级优先级处理每个元素：

| 优先级 | 通道 | 条件 |
|--------|------|------|
| 1 | Op 内部裁剪 | Op 支持自裁剪（`op->clipToShape()`） |
| 2 | 硬件 | scissor / window rectangles |
| 3 | Analytic FP | 满足形状条件，且当前 FP 数 < 4 |
| 4 | 软 mask / stencil mask | 其他所有情况 |

核心决策函数 `analytic_clip_fp`（`ClipStack.cpp:256-277`）：

```
if (rect 且恒等变换)  → GrFragmentProcessor::Rect()
if (rrect 且恒等变换) → GrRRectEffect::Make()
if (shape 是凸多边形) → GrConvexPolyEffect::Make()
else                   → 空（降级）
```

每个元素独立判定；上限 4 个 FP，超出的元素落到 mask。

### 3.2 关键 FP

#### GrFragmentProcessor::Rect

恒等变换矩形，shader 与 TGFX `AARectEffect` 一致（距离场 saturate）。

#### GrRRectEffect

轴对齐 RRect 的入口。核心是按 SkRRect 子类型把 RRect 分派到两个具体 FP，决策树详见下文。

##### SkRRect 子类型分派（代码入口 `GrRRectEffect::Make`）

| SkRRect 子类型 | 特征 | 走向 |
|---------------|------|------|
| `kRect` | 无圆角 | 退化到 `GrFragmentProcessor::Rect` |
| `kOval` | 整体椭圆（半径 = 半宽/半高） | `GrOvalEffect` |
| `kSimple` | 四角半径全部相同 | 若 `rx == ry` → `CircularRRectEffect(kAll)`；否则 → `EllipticalRRectEffect` |
| `kNinePatch` | 左右/上下对称（两对半径） | 若所有非零角半径相等且为圆形 → `CircularRRectEffect`；否则若四角半径 ≥ 0.5 → `EllipticalRRectEffect` |
| `kComplex` | 四角可各异 | 仅处理"tab 形"——**部分角是相同半径的圆形角、其余是直角 0**，走 `CircularRRectEffect` 对应的 `cornerFlags`；真正的四角椭圆一律 fail |

返回 nullptr（降级到 mask）的条件：

- `localToDevice` 非 identity（即任何旋转/错切/非均匀缩放）—— **RRect 不会降级到 `GrConvexPolyEffect`**，因为圆角是曲线而非线性边
- 任一圆角 < 0.5 像素（`kRadiusMin`），会被 squash 为直角；若因此退化为全直角则走 Rect FP
- kComplex 的四角椭圆情况
- AA 类型是 `kFillBW` / `kInverseFillBW`（非 AA）

##### CircularRRectEffect（圆形角）

适用场景：**若干角是相同半径的圆形角**，其余是直角。支持 9 种 `cornerFlags` 组合（单角 / 一条边的两个相邻角 / 全部四角）——覆盖了移动端常见的 "tab 形"、"卡片顶部圆底直" 等布局。

Shader 用九宫格策略：

```glsl
// 关键 uniform：innerRect（内缩后的矩形）、radiusPlusHalf = (r + 0.5, 1/(r + 0.5))
// 对有圆角的象限：算到内矩形角点的距离
vec2 dxy0 = innerRect.xy - gl_FragCoord.xy;   // 左/上方向正
vec2 dxy1 = gl_FragCoord.xy - innerRect.zw;   // 右/下方向正
vec2 dxy = max(max(dxy0, dxy1), 0.0);
half alpha = clamp(radiusPlusHalf.x - length(dxy), 0.0, 1.0);
// 对直角一侧：叠加线性 alpha 实现边界过渡
```

##### EllipticalRRectEffect（椭圆角）

适用场景：**kSimple 椭圆角**（四角相同但 `rx ≠ ry`）或 **kNinePatch**（左右/上下对称的椭圆角，上下半径可不同）。**不支持 kComplex 四角椭圆**。

Shader 用椭圆隐函数近似有向距离：

```glsl
// 关键 uniform：innerRect、invRadiiSqd = (1/rx², 1/ry²)（kSimple 用 vec2，kNinePatch 用 vec4）
vec2 Z = dxy * invRadiiSqd;
half implicit = dot(Z, dxy) - 1.0;                // 椭圆隐函数
half gradDot  = max(4.0 * dot(Z, Z), 1e-4);       // 梯度平方
half dist     = implicit * inversesqrt(gradDot);  // 近似像素距离
half alpha    = clamp(0.5 - dist, 0.0, 1.0);
```

FP16 设备或极大半径时启用一个 `scale` uniform 做归一化防止精度丢失。

#### GrConvexPolyEffect

接收一组线性边方程（最多 8 条），每条表达为 `(a, b, c)`，满足 `a² + b² = 1`（归一化法线）。shader 内对每条边计算 `signed distance = a*x + b*y + c`，取所有边的最小距离做 AA saturate：

```
float distance = minAllEdges(a * gl_FragCoord.x + b * gl_FragCoord.y + c);
float coverage = clamp(distance + 0.5, 0, 1);
```

适用于：
- 仿射变换后的矩形（4 条边）
- 任意凸多边形（例如 RRect 的直边部分 + 切角近似，但 Skia 仅对纯凸多边形使用）

### 3.3 TGFX 差异

| 方面 | Skia | TGFX 现状 |
|------|------|-----------|
| ClipElement 保存源几何 | 是（SkRRect + 矩阵分离） | 否（只存烘焙后的 Path） |
| 轴对齐 RRect FP | GrRRectEffect（圆/椭圆分派） | 无 |
| 变换矩形 FP | GrConvexPolyEffect | 无 |
| Analytic FP 上限 | 4 | 无显式上限（但只有 AARectEffect 一种） |

---

## 4. 对比分析

| 场景 | 现状通道 | 目标通道 | 节省 |
|------|---------|---------|------|
| 像素对齐矩形 | scissor | scissor | - |
| AA 轴对齐矩形（含平移/缩放/镜像/90°倍数旋转） | AARectEffect | AARectEffect | - |
| AA 任意角度旋转矩形 | mask 纹理 | AAConvexPolyEffect | 1 离屏 pass + 1 采样 |
| AA 错切矩形 | mask 纹理 | AAConvexPolyEffect | 1 离屏 pass + 1 采样 |
| AA 轴对齐 RRect | mask 纹理 | AARRectEffect | 1 离屏 pass + 1 采样 |
| AA 非轴对齐 RRect（任意角度旋转/错切） | mask 纹理 | mask 纹理 | - |
| 带透视矩形/RRect | mask 纹理 | mask 纹理 | - |
| 一般路径 | mask 纹理 | mask 纹理 | - |

---

## 5. 重构/实现方案

### 5.1 ClipElement 扩展

为了在 `applyClip` 拿到"源几何 + 矩阵"而非烘焙后的 path，`ClipElement` 增加分类和源几何字段。

```cpp
enum class ClipShape {
  Rect,     // 轴对齐矩形（源空间）
  RRect,    // 轴对齐 RRect（源空间）
  Path      // 一般路径
};

class ClipElement {
 public:
  ClipElement(const Rect& rect, const Matrix& matrix, bool antiAlias);
  ClipElement(const RRect& rRect, const Matrix& matrix, bool antiAlias);
  ClipElement(const Path& path, bool antiAlias);

  ClipShape shape() const;
  // Valid when shape() == Rect.
  const Rect& rect() const;
  // Valid when shape() == RRect.
  const RRect& rRect() const;
  // viewMatrix brings the source shape (Rect / RRect) into device space.
  // For shape() == Path, this is identity (path already stored in device space).
  const Matrix& viewMatrix() const;

  const Path& path() const;              // always available (lazy materialized)
  const Rect& bounds() const;            // device-space bounds
  bool isAntiAlias() const;
  bool isValid() const;
  bool isPixelAligned() const;

  // ... 其余接口保持不变
 private:
  ClipShape _shape = ClipShape::Path;
  Rect _rect = {};
  RRect _rRect = {};
  Matrix _viewMatrix = Matrix::I();
  Path _path = {};             // 源几何烘焙矩阵后的 device-space path
  Rect _bounds = {};
  bool _antiAlias = false;
  int _invalidatedByIndex = -1;
};
```

`Canvas::clipRect` / `Canvas::clipPath` 的对应改造：

```cpp
void Canvas::clipRect(const Rect& rect, bool antiAlias) {
  clipStack->clipRect(rect, _matrix, antiAlias);
}
void Canvas::clipRRect(const RRect& rRect, bool antiAlias) {     // 新增（如没有）
  clipStack->clipRRect(rRect, _matrix, antiAlias);
}
void Canvas::clipPath(const Path& path, bool antiAlias) {
  if (Rect r; path.isRect(&r)) {
    clipStack->clipRect(r, _matrix, antiAlias);
    return;
  }
  if (RRect rr; path.isRRect(&rr)) {
    clipStack->clipRRect(rr, _matrix, antiAlias);
    return;
  }
  auto clipPath = path;
  clipPath.transform(_matrix);
  clipStack->clipPath(clipPath, antiAlias);
}
```

`ClipStack::clipRect` / `clipRRect` 内部：
- 存源 `Rect`/`RRect` + `_matrix`，并用该矩阵把 path/bounds 烘焙到设备空间供后续 `tryCombine` / `tightContains` 等使用
- `ClipElement::transform(matrix)` 被 `ClipStack::transform` 调用时，同步更新 `_viewMatrix = matrix * _viewMatrix`，并作用到 `_path` 和 `_bounds`

> **注意**：如果后续经过 `ClipStack::transform(matrix)` 后，`_viewMatrix` 包含透视分量，`applyClip` 判定时需回退到 Path 分类处理。

### 5.2 新增 FP

#### AARRectEffect（轴对齐 RRect）

```cpp
class AARRectEffect : public FragmentProcessor {
 public:
  // rect: device-space 轴对齐矩形
  // radii[4]: 四角半径（TL, TR, BR, BL），x/y 可不等
  // 返回 nullptr 表示当前 RRect 形态不被支持（应走 mask）
  static PlacementPtr<AARRectEffect> Make(BlockAllocator* allocator,
                                           const Rect& rect,
                                           const Point radii[4]);
  std::string name() const override { return "AARRectEffect"; }

 private:
  Rect rect = {};
  Point radii[4] = {};
};
```

着色器核心（椭圆角，支持四角不同半径）：

```glsl
// 每角相对于外切矩形计算到圆心的椭圆距离
// inner: rect 向内收缩各自半径后的矩形（四角椭圆的"中心盒"）
vec2 dxy0 = innerLT - gl_FragCoord.xy;   // 左/上方向正
vec2 dxy1 = gl_FragCoord.xy - innerRB;   // 右/下方向正
vec2 dxy  = max(max(dxy0, dxy1), 0.0);
// 判定当前像素落在哪个角（选对应的半径对）
vec2 rxy  = select_radii_per_corner(gl_FragCoord.xy);
// 椭圆距离场（近似）： d = length(dxy / rxy) - 1，转成像素距离
vec2 scaled = dxy / rxy;
float d = (1.0 - length(scaled)) * min(rxy.x, rxy.y);
float coverage = clamp(d + 0.5, 0.0, 1.0);
```

简化说明：四角半径不同时需要按角选对应的 `rxy`；圆角（`r.x == r.y`）时退化为圆形距离场 `length(dxy) - r`。Make 返回 nullptr 的形态：半径 < 0.5（降级为 Rect 由调用方处理）、退化 RRect（rect 为空等）。

#### AAConvexPolyEffect（凸多边形，用于变换矩形）

```cpp
class AAConvexPolyEffect : public FragmentProcessor {
 public:
  // edges: 每条边 (a, b, c)，满足 a²+b²=1，内部区域 a*x + b*y + c >= 0
  // 最多 kMaxEdges = 8 条；超出返回 nullptr
  static PlacementPtr<AAConvexPolyEffect> Make(BlockAllocator* allocator,
                                                const std::vector<Vec3>& edges);
  static constexpr int kMaxEdges = 8;
  std::string name() const override { return "AAConvexPolyEffect"; }

 private:
  std::vector<Vec3> edges;
};
```

着色器核心：

```glsl
// edges 以 vec3 uniform 数组上传，长度 N 作为 #define 常量
float d = 1e9;
for (int i = 0; i < N; ++i) {
    d = min(d, edges[i].x * gl_FragCoord.x + edges[i].y * gl_FragCoord.y + edges[i].z);
}
float coverage = clamp(d + 0.5, 0.0, 1.0);
```

**CPU 侧边方程生成**（针对仿射变换后的矩形）：

```cpp
// 输入：源空间 Rect + 仿射 viewMatrix（无透视）
// 输出：4 条归一化边方程
std::array<Vec3, 4> RectToEdges(const Rect& rect, const Matrix& m) {
  Point p[4] = {{rect.left, rect.top},
                {rect.right, rect.top},
                {rect.right, rect.bottom},
                {rect.left, rect.bottom}};
  m.mapPoints(p, 4);                             // 变换到设备空间
  std::array<Vec3, 4> edges;
  for (int i = 0; i < 4; ++i) {
    auto& a = p[i];
    auto& b = p[(i + 1) % 4];
    // 边方向 (b - a)，内法线按 CCW 选择；这里假设 rect 顶点顺序保持 CCW
    float nx =  (b.y - a.y);
    float ny = -(b.x - a.x);
    float len = sqrt(nx * nx + ny * ny);
    nx /= len; ny /= len;
    float c = -(nx * a.x + ny * a.y);
    edges[i] = {nx, ny, c};
  }
  // 若 viewMatrix 行列式为负（镜像），所有法线取反以保证内部 >= 0
  if (m.getScaleX() * m.getScaleY() - m.getSkewX() * m.getSkewY() < 0) {
    for (auto& e : edges) e = -e;
  }
  return edges;
}
```

> `Vec3` 可直接用 `std::array<float, 3>` 或 `Point3F`，按项目现有类型复用。

### 5.3 applyClip 改造

`applyClip` Stage 3 元素分派扩展（`src/gpu/OpsCompositor.cpp:729-786`）：

```cpp
constexpr size_t kMaxAnalyticFPs = 4;
size_t analyticFPCount = 0;

for (...) {
  auto& element = elements[i];
  if (!element.isValid()) continue;

  // 硬边界矩形：scissor 已覆盖
  if (element.shape() == ClipShape::Rect &&
      element.viewMatrix().isIdentity() &&        // 没有旋转/缩放
      (element.isPixelAligned() || !element.isAntiAlias())) {
    continue;
  }

  if (analyticFPCount < kMaxAnalyticFPs) {
    auto fp = makeAnalyticFP(element, clipFP);       // 见下
    if (fp) {
      clipFP = std::move(fp);
      ++analyticFPCount;
      continue;
    }
  }
  elementsForMask.push_back(&element);
}
```

改造后的 `makeAnalyticFP`（返回 nullptr 表示不支持，由调用方降级）：

```cpp
PlacementPtr<FragmentProcessor> OpsCompositor::makeAnalyticFP(
    const ClipElement& element, PlacementPtr<FragmentProcessor>& inputFP) {
  const auto& m = element.viewMatrix();
  if (m.hasPerspective()) {
    return nullptr;                                  // 透视不支持
  }

  PlacementPtr<FragmentProcessor> clipFP = nullptr;

  if (element.shape() == ClipShape::Rect) {
    if (IsAxisAligned(m)) {
      // 矩形 + 轴对齐矩阵：走 AARectEffect
      Rect deviceRect = m.mapRect(element.rect());
      FlipYIfNeeded(&deviceRect, renderTarget.get());
      clipFP = AARectEffect::Make(drawingAllocator(), deviceRect);
    } else {
      // 矩形 + 仿射（旋转/错切/带符号缩放）：走 AAConvexPolyEffect
      auto edges = RectToEdges(element.rect(), MatrixWithYFlip(m));
      clipFP = AAConvexPolyEffect::Make(drawingAllocator(), edges);
    }
  } else if (element.shape() == ClipShape::RRect) {
    if (IsAxisAligned(m)) {
      Rect deviceRect = m.mapRect(element.rRect().rect);
      Point radii[4] = ScaleRadii(element.rRect(), m);
      FlipYIfNeeded(&deviceRect, renderTarget.get());
      clipFP = AARRectEffect::Make(drawingAllocator(), deviceRect, radii);
    } else {
      return nullptr;                                 // 旋转 RRect 暂走 mask
    }
  }

  if (!clipFP) return nullptr;
  if (!inputFP) return clipFP;
  return FragmentProcessor::Compose(drawingAllocator(),
                                    std::move(inputFP), std::move(clipFP));
}
```

辅助判定：

```cpp
// 矩阵是否"轴对齐"：没有旋转/错切/透视
inline bool IsAxisAligned(const Matrix& m) {
  return m.getSkewX() == 0 && m.getSkewY() == 0 && !m.hasPerspective();
}
```

> `Canvas::clipPath` 里如果输入 path 是轴对齐 rect 且 `_matrix` 仅平移/正缩放，应构造 `ClipShape::Rect` 而非 `ClipShape::Path`，以命中快路径。

### 5.4 几何合并兼容性

`ClipElement::tryCombine` / `tightContains` / `looseIntersects` 目前基于 `_path` + `_bounds`。新结构保留了烘焙后的 `_path` 和设备空间 `_bounds`，**现有合并逻辑无需改动**。只需在构造和 `transform` 时同步维护 `_path` 和 `_viewMatrix` 一致性。

### 5.5 处理流程图

```
applyClip
├─ Stage 1-2：Empty/WideOpen/scissor 与现状一致
├─ Stage 3：遍历 elements
│  ├─ Rect + identity(或仅像素对齐平移) + (非AA | 已对齐) → skip (scissor 覆盖)
│  ├─ shape==Rect  + axisAligned + AA        → AARectEffect        ┐
│  ├─ shape==Rect  + affine(非轴对齐)        → AAConvexPolyEffect  ├─ analyticFPCount < 4
│  ├─ shape==RRect + axisAligned             → AARRectEffect       ┘
│  ├─ 以上任一构造失败 / 上限满 / 透视 / Path → elementsForMask
└─ Stage 4：elementsForMask 非空 → getClipMaskFP（现状逻辑）
```

---

## 6. 测试计划

### 6.1 单元测试（逻辑）

| 测试 | 场景 | 预期 |
|------|------|------|
| ClipStack_ClipRectPreservesShape | `clipRect` 后 `elements[0].shape() == Rect` | 通过 |
| ClipStack_RotatedRectStillRectShape | 旋转 45° 的 `clipRect` 后 shape 仍为 Rect，viewMatrix 含旋转 | 通过 |
| ClipStack_ClipPathWithRect | `clipPath` 传入 rect path，自动归类为 `Rect` | 通过 |
| ClipStack_ClipPathWithRRect | `clipPath` 传入 RRect path，自动归类为 `RRect` | 通过 |
| RectToEdges_CCWNormals | 对单位矩形生成 4 条内法线朝内的归一化边方程 | 通过 |
| RectToEdges_MirrorMatrix | 负行列式矩阵下法线方向正确 | 通过 |
| AnalyticFP_RejectPerspective | viewMatrix 含透视时 `makeAnalyticFP` 返回 nullptr | 通过 |
| AnalyticFP_MaxLimit | 连续 clip 5 个 RRect，第 5 个落入 mask 纹理 | 通过 |

### 6.2 截图测试

在 `ClipTest`（或新建 `AnalyticClipTest`）中新增用例。Surface 512×512，四边边距 ≥ 50 像素，坐标整数。

| 用例 | 内容 |
|------|------|
| `AnalyticClipTest/RotatedRectClip` | 45° 旋转矩形裁剪一个填满的彩色大矩形，对比边缘 AA |
| `AnalyticClipTest/ShearedRectClip` | 错切矩阵下的矩形裁剪 |
| `AnalyticClipTest/AxisAlignedRRectClip` | 轴对齐 RRect（四角不同半径）裁剪 |
| `AnalyticClipTest/CircularRRectClip` | 四角相等的圆形 RRect 裁剪 |
| `AnalyticClipTest/StackedAnalyticClips` | 连续 3 个 analytic clip 叠加（矩形+旋转矩形+RRect） |
| `AnalyticClipTest/OverflowFallbackToMask` | 5 个 RRect 叠加（超过上限），验证第 5 个走 mask 仍正确 |
| `AnalyticClipTest/PerspectiveRectClip` | 透视矩形裁剪，验证走 mask 通道结果正确 |

每个用例先用 `shape->getPath().getBounds()` 打印取值，调整位置使内容居中。

### 6.3 边界情况

- 矩形半径 < 0.5：`AARRectEffect::Make` 返回 nullptr 时，`makeAnalyticFP` 继续尝试 Rect 路径还是直接降级？需明确：退化为 `AARectEffect`（半径忽略）。记录在代码注释。
- 非 AA 的旋转矩形：走 `AAConvexPolyEffect` 时 CPU 侧不外扩，shader 用 `d >= 0` 而非 saturate。实现上用同一 FP 接 AA/非 AA 参数。
- `Matrix::mapRect` 对非轴对齐矩阵会返回外接包围盒，不能用于判定；必须先 `IsAxisAligned(m)`。

---

## 7. 实现步骤

| 阶段 | 任务 | 涉及文件 |
|------|------|---------|
| 1 | 扩展 `ClipElement` 保存 shape 分类与源几何 | `src/core/ClipStack.h`、`src/core/ClipStack.cpp` |
| 2 | `ClipStack::clipRect` / `clipRRect` 接口与 `Canvas` 对接 | `src/core/ClipStack.{h,cpp}`、`src/core/Canvas.cpp`、`include/tgfx/core/Canvas.h` |
| 3 | 实现 `AARRectEffect`（含 GLSL 版本） | `src/gpu/processors/AARRectEffect.{h,cpp}`、`src/gpu/glsl/processors/GLSLAARectEffect*` 对应文件 |
| 4 | 实现 `AAConvexPolyEffect`（含 GLSL 版本） | `src/gpu/processors/AAConvexPolyEffect.{h,cpp}`、`src/gpu/glsl/processors/GLSLAAConvexPolyEffect*` |
| 5 | 改造 `OpsCompositor::applyClip` / `makeAnalyticFP`，加 `kMaxAnalyticFPs` 上限 | `src/gpu/OpsCompositor.{h,cpp}` |
| 6 | 单元测试（ClipStack 结构、`RectToEdges`、FP 上限） | `test/src/ClipStackTest.cpp`（或新增） |
| 7 | 截图测试（AnalyticClipTest 系列） | `test/src/ClipTest.cpp`（或新增），`test/baseline/version.json` |
| 8 | 基准验证 + 调优（半径阈值、MaxEdges 是否从 8 改为 4） | - |

---

## 8. 参考资料

- Skia Ganesh ClipStack：`src/gpu/ganesh/ClipStack.cpp`（`apply`、`analytic_clip_fp`）
- Skia GrRRectEffect：`src/gpu/ganesh/effects/GrRRectEffect.cpp`
- Skia GrConvexPolyEffect：`src/gpu/ganesh/effects/GrConvexPolyEffect.{h,cpp}`
- Skia GrFragmentProcessor::Rect：`src/gpu/ganesh/GrFragmentProcessor.cpp:615-656`
- TGFX 裁剪现状：
  - `src/gpu/OpsCompositor.cpp:729-879`
  - `src/core/ClipStack.{h,cpp}`
  - `src/gpu/processors/AARectEffect.h`
  - `src/gpu/glsl/processors/GLSLAARectEffect.cpp`
  - `src/core/Canvas.cpp:186-195`
