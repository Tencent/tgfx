# 矢量图层退化几何描边效果优化

> 状态：实现中
> 分支：`bugfix/jasonrjchen_vector_layer_gradient_stroke`
> 负责人：jasonrjchen / Dom

## 1. 背景

矢量图层（`VectorLayer`）的几何形状退化为点或线时（Rectangle / Ellipse 任一维度 = 0；Polystar 的 outerRadius = 0），需要让**实线 / 虚线描边**配合 **LineJoin / LineCap** 的组合产生与 Figma 一致的视觉效果。当前 tgfx 在这类输入上的输出与 Figma 存在差异（外描边几乎不可见 / 端点形状错误）。

**Cap 与 Join 的生效条件**

- **Cap**：仅虚线生效，作用于每段 dash 的两端。
- **Join**：实线 / 虚线都生效，作用于路径自身的拐角。

**两类独立根因**

本任务实测发现两个互相独立的根因，需要分别修复（详见 §5）：

1. **几何源头退化**：Rectangle / Ellipse 的 W=0 或 H=0、Polystar 的 outerRadius=0 让中心线 path 退化为点或线段，stroker 没有可用的拐角 / 端点几何（§5.2）。
2. **`Shape::getBounds()` 上界违约**：`ApplyStrokeToBounds` 的 outset 公式没考虑 square cap 沿对角方向延伸 `SW·√2/2`，导致 `StrokeShape::getBounds()` 在 square cap 场景下偏小，下游 GPU 三角化用错误画布裁掉 path 的对角延伸部分（§5.6）。

## 2. 现有实现分析

### 2.1 矢量描边主线流程

VectorLayer 一组 elements（几何 + Trim + Stroke）从 path 走到屏幕分为 4 个阶段。理解这条主线是后续判断退化兜底应该改在哪一层的前提。

```
[阶段 1] 几何阶段                  VectorElement::apply
   几何类生成中心线 path，写入 context->geometries[]
   ─ Rectangle::apply       src/layers/vectors/Rectangle.cpp
   ─ Ellipse::apply         src/layers/vectors/Ellipse.cpp
   ─ Polystar::apply        src/layers/vectors/Polystar.cpp
   ─ ShapePath::apply       src/layers/vectors/ShapePath.cpp
   ─ TrimPath::apply        沿弧长截短 geometries[] 里的中心线
        │
        ▼
[阶段 2] StrokeStyle::apply        src/layers/vectors/StrokeStyle.cpp:326
   不画，只装配 StrokePainter 推入 context->painters[]：
   ─ stroke 参数（width / cap / join / miter）
   ─ pathEffect = CreateDashPathEffect(_dashes, _dashOffset, _dashAdaptive, _stroke)
       └─ adaptive=true → AdaptiveDashEffect
       └─ adaptive=false → SkDashPathEffect 包装
   ─ originalShapes[]（仅 strokeAlign != Center 时拷贝阶段 [1] 的几何 shape）
        │
        ▼
[阶段 3] StrokePainter::prepareShape   src/layers/vectors/StrokeStyle.cpp:51
   (3a) wrapShaderWithFit(innerShape->getPath().getBounds())
   (3b) Shape::ApplyEffect(pathEffect)：把阶段 [2] 创建的 AdaptiveDashEffect / SkDashPathEffect
        包装成 EffectShape 挂到 innerShape 上。这一步本身不切 dash——
        EffectShape::onGetPath 触发时才会调 pathEffect->filterPath(&path)
        即 AdaptiveDashEffect::filterPath（src/core/AdaptiveDashEffect.cpp:153，详见 §2.2）
   (3c) StrokeAlign 分支：
        ├ Inside / Outside → applyStrokeAndAlign（StrokeStyle.cpp:150）
        │     ① Shape::ApplyStroke(width × 2) → 内部读 shape.getPath()
        │        ⇒ 在此触发 (3b) 挂载的 dash filterPath，先输出 dash 化中心线
        │        ⇒ 再交给 stroker（CPU 侧 SkPathStroker）展宽
        │     ② Shape::Merge(..., Intersect/Difference) ← Boolean Op
        └ Center → paint.style = Stroke; paint.stroke = stroke
              dash 延后：阶段 [4] 取 path 时才触发 filterPath，再交 GPU stroker
        │
        ▼
[阶段 4] Canvas::drawShape(shape, paint)
   paint.style 决定走 fill 还是 stroke：
   ─ Stroke + Rect/RRect/Oval → drawContext fast-path（仅无 dash 且无 path effect 时匹配）
   ─ Stroke + 其它 path（含挂了 dash 的 EffectShape）→ StrokeShape::onGetPath
                            ├─ shape.getPath()                     ← dash 化路径
                            │    └─ EffectShape::onGetPath
                            │         └─ AdaptiveDashEffect::filterPath  ← Center 路径下 dash 在此触发
                            └─ Stroke::applyToPath                  ← 接收 dash 化路径
                                 └─ SkPaint::getFillPath
                                      └─ SkStroke::strokePath / SkPathStroker（edge offset）
   ─ Fill（Inside/Outside 已物化的填充几何）→ 直接 fill
        │
        ▼
   GPU 三角化 + 光栅化
```

**三个不要混淆的点**：

- **挂载与执行分离**：dash 在阶段 [3b] 通过 `Shape::ApplyEffect` 挂载到 shape 上（不切片），真正调用 `AdaptiveDashEffect::filterPath` 切 dash 的时机取决于 strokeAlign：
  - Inside / Outside：阶段 [3c] 的 `Shape::ApplyStroke` 内部读 `shape.getPath()` 时立刻触发 → CPU 侧顺序"dash → stroker → Boolean Op"。
  - Center：阶段 [3] 不取 path，dash 推迟到阶段 [4] 的 `StrokeShape::onGetPath` 取 path 时才触发 → CPU 切 dash + GPU/通用 stroker 展宽。
- **画的不是"原始 path 的 stroker 输出 + 后期切 dash"**，而是先把中心线切碎成离散开放 sub-path、再让 stroker 给每段 dash 加宽度。所以每段 dash 的两端都是 stroker 看到的"开放路径端点"，会按 LineCap 渲染。
- **stroker 执行时机由 strokeAlign 决定**：Inside/Outside 在阶段 [3]（CPU 侧，跟随 Boolean Op），Center 在阶段 [4]（GPU 流水线启动时）。但算法都是同一个 `SkPathStroker`，所有"退化几何 stroker 失灵"的根因都在它的 `degenerate_vector` / `EqualsWithinTolerance` 判定。

§2.1 把 dash 在主线里的位置定到了"阶段 [3b] 挂载、阶段 [3c] 或 [4] 触发"，但具体 `AdaptiveDashEffect::filterPath` 内部如何把一条中心线切成 dash 序列、为什么矩形虚线会出现拐角 LineJoin 缺失等现象，需要展开看实现。

### 2.2 AdaptiveDashEffect 切 dash 的流程

`AdaptiveDashEffect::filterPath`（`src/core/AdaptiveDashEffect.cpp:153-239`）是 `_dashAdaptive=true` 时实际执行 dash 切片的入口。它接收一条中心线 path，沿弧长按 `_intervals` 实-空交替切成一组开放 sub-path 写回。

```
filterPath(path)
   │
   ├─ ① BuildContours(path)                      AdaptiveDashEffect.cpp:42-130
   │     按 SkPath::kMove_Verb（即 moveTo 操作的 verb 类型）拆 contour，每条 verb
   │     （line/quad/conic/cubic）包成单 verb 的 SkPathMeasure → 一个 segment
   │     Close verb 触发：标 isClosed=true，必要时补 closing line segment
   │
   ├─ ② 双层循环：for contour → for segment
   │     ─ 每进入新 contour 重置：
   │         skipFirstSegment = contour.isClosed
   │         deferFirstSegment = false
   │         needMoveTo = true
   │
   ├─ ③ Tiny segment 守卫（line 182-197）
   │     segmentLength < 0.1 且抠掉它后 contour 长度几乎不变 → 整段照搬到 resultPath
   │     工程兜底，规避 path merge 后留下的微段
   │
   ├─ ④ 自适应缩放（line 198-208，"Adaptive" 名字来源）
   │     patternRatio = max(round(segmentLength / intervalLength), 1.0)
   │     scale        = segmentLength / (patternRatio × intervalLength)
   │     currentPos   = -_phase × scale
   │     totalDashCount += patternRatio × (patternCount/2)
   │     totalDashCount > MaxDashCount → return false（熔断）
   │
   ├─ ⑤ while 循环铺 dash（line 210-225）
   │     while currentPos < segmentLength:
   │         dashLength = _intervals[patternIndex] × scale
   │         if IsEven(patternIndex) && currentPos + dashLength > 0:    // 偶数索引 = 实线
   │             if currentPos < 0 && skipFirstSegment:
   │                 deferFirstSegment = true
   │                 deferredLength    = dashLength + currentPos        // 跨 contour 起点的 dash 暂不画
   │             else:
   │                 segment->getSegment(currentPos, currentPos + dashLength,
   │                                     &resultPath, needMoveTo)
   │         currentPos   += dashLength
   │         patternIndex  = (patternIndex + 1) % patternCount
   │         needMoveTo    = true
   │
   ├─ ⑥ contour 结尾 defer 补段（line 231-233）
   │     if deferFirstSegment:
   │         contour.segments.front()->getSegment(0, deferredLength,
   │                                              &resultPath, false)   // needMoveTo=false 接续上一笔
   │
   └─ PathRef::WriteAccess(*path) = resultPath; setFillType(fillType)
```

**关键设计取舍与已知局限**：

- **per-segment 独立 scale**（一段 verb 一组 dash 周期）让 dash 在每条 edge 末尾都对齐到完整周期边界，但不同长度的 edge 上 dash 视觉宽度可能略有差异。
- **`_phase=0` 下 `currentPos` 从 0 起步**，**永远不会进入 `currentPos < 0 && skipFirstSegment` 分支**——`deferFirstSegment` 机制对默认输入完全不生效。
- 即便 `_phase` 配置让首段 dash 跨越 contour 起点，**defer 机制也只补救 contour 起点 P0 一个顶点**：让 P0 处的 dash 拼成连续 polyline，由 stroker 生成 LineJoin。其他顶点（矩形的 P1/P2/P3、星形的所有非起点顶点）处，相邻 segment 的首/尾 dash 各自是独立 sub-path（各有 moveTo），stroker 在该顶点处生成两个 Cap 重叠，**没有 LineJoin**。
- `needMoveTo = IsEven(patternIndex)` 在 while 退出时设置，理论上支持"一段 dash 从 segment A 末尾延续到 segment B 开头"。但自适应 scale 让每段 segment 末尾恒落在 patternIndex=0 切换边界（`IsEven(0)=true`）→ 下一段开头 needMoveTo 仍是 true，跨 segment 续画机制实际不生效。

后两条局限解释了 §4 表格里"非退化矩形 + 虚线 → Miter join 在拐角处不显形"现象的根因——**所有非起点顶点处都缺失 LineJoin**。修复需要让 segment 间共享相位状态、在接缝处用 `needMoveTo=false` 接续上一段 dash，超出本次退化几何兜底的范围，作为独立改造项跟踪。

### 2.3 SkPathStroker 外扩流程

dash 切片产出的开放 sub-path 进入阶段 [3c] / [4] 后，最终都交给 `SkPathStroker`（`third_party/pathkit/src/core/SkStroke.cpp`）做沿法线方向的两侧外扩。理解它的状态机是判断"为什么退化几何 + 虚线在 Butt Cap 下输出空、在 Square/Round Cap 下输出朝坐标轴对称的菱形"的关键。

**入口与遍历**

`SkStroke::strokePath`（`SkStroke.cpp:1340`）按 verb 派发到 `stroker.moveTo / lineTo / quadTo / ...`：

```
moveTo → 记 fFirstPt = fPrevPt = pt，重置 fSegmentCount = 0
lineTo → 见下方"单条 lineTo 外扩流程"
close  → finishContour(true, ...)
done   → finishContour(false, ...) 把 fOuter 输出到 dst
```

**核心数据结构**

stroker 维护两条 path：

- `fOuter` —— 外侧轮廓（path 顺时针走向的左侧 = "外")
- `fInner` —— 内侧轮廓（右侧 = "内")

外扩本质：对每条 edge 沿单位法线方向 ±radius 各画一条平行 edge，最后用 Cap（开放）或 close + reversePath（闭合）把两条 path 拼成单一闭环——形成"中心线两侧各扩 radius 的带状 fill 区域"。

**单条 lineTo 外扩流程**（`SkStroke.cpp:437-452`）

```
SkPathStroker::lineTo(currPt, iter)
  ├─ ① teenyLine 检测（line 438-444）
  │    teenyLine = EqualsWithinTolerance(prevPt, currPt, NearlyZero / ResScale)
  │    Butt Cap + teenyLine → return                ← 短到忽略，且无 Cap 兜底
  │    teenyLine + (JoinCompleted || hasValidTangent) → return
  │
  ├─ ② preJoinTo（line 447 → line 262-291）
  │    set_normal_unitnormal(prevPt, currPt, ...)（line 267 → line 69-79）
  │       unitNormal = (after - before).normalize()
  │       向量长度过小 → setNormalize 返回 false（line 72-74）
  │    失败时按 Cap 类型分支:
  │       Butt Cap → return false → 整条 lineTo abort（line 268-270）
  │       Square / Round Cap → 默认法线 (radius, 0)、unitNormal (1, 0) 兜底（line 271-275）
  │
  │    成功 / 兜底后:
  │       fSegmentCount == 0（第一条 edge）：
  │           记录 fFirstNormal / fFirstUnitNormal / fFirstOuterPt
  │           fOuter.moveTo(prevPt + normal)
  │           fInner.moveTo(prevPt - normal)
  │       后续 edge：
  │           fJoiner(&fOuter, &fInner, fPrevUnitNormal, fPrevPt, *unitNormal, ...)
  │           ← 这一步生成 Miter spike / Bevel 切角 / Round join 几何
  │
  ├─ ③ line_to（line 450 → line 400-403）
  │    fOuter.lineTo(currPt + normal)               ← 外侧平行 edge
  │    fInner.lineTo(currPt - normal)               ← 内侧平行 edge
  │
  └─ ④ postJoinTo（line 451 → line 293-300）
       fJoinCompleted = true
       fPrevPt = currPt
       fPrevUnitNormal = unitNormal
       fPrevNormal = normal
       fSegmentCount += 1
```

**contour 收尾**（`finishContour`，`SkStroke.cpp:302-346`）

```
close=true（闭合 path，对应 stroker.close 调用）:
  fJoiner(..., fPrevUnitNormal, fPrevPt, fFirstUnitNormal, ...)  ← 末段 → 首段的 join
  fOuter.close()
  // 把 fInner 反向拼到 fOuter 末尾，形成"外环 + 内环"双轮廓 fill 区域
  fOuter.moveTo(fInner.getLastPt())
  fOuter.reversePathTo(fInner)
  fOuter.close()

close=false（开放 path，对应 stroker.done 调用）:
  fCapper(&fOuter, fPrevPt, fPrevNormal, lastInnerPt, ...)        ← 末端 Cap
  fOuter.reversePathTo(fInner)
  fCapper(&fOuter, fFirstPt, -fFirstNormal, fFirstOuterPt, ...)   ← 起点 Cap
  fOuter.close()
```

**Cap / Join 函数指针**

`fCapper`、`fJoiner` 在构造时按 `SkPaint::Cap` / `SkPaint::Join` 选定不同函数：

- `ButtCapper` —— 不画任何 cap 几何（直接连接 fOuter 与 fInner 反向端点）
- `SquareCapper` —— 沿端点切线方向延伸 radius 的方形帽
- `RoundCapper` —— 半径 radius 的半圆
- `MiterJoiner` —— 沿外法线方向延伸到两条 edge 法线的交点（Miter spike）
- `BevelJoiner` —— 切角线段
- `RoundJoiner` —— 圆弧

**退化几何下的失效点（与本任务相关）**

把上述流程套到 dash 切出的 L 形 sub-path（3 点：进入 → 拐角 → 离开，每段 lineTo 长 ≈ 2.77e-5）上：

- **teenyLine 提前 return**：edge 长可能小于 `PK_ScalarNearlyZero / ResScale` → Butt Cap 在第 ① 步直接淘汰，Square/Round Cap 也可能因 `fJoinCompleted || hasValidTangent` 跳过。
- **法线归一化失败**：`setNormalize` 对极短向量返回 false → Butt Cap 在第 ② 步 abort 整条 lineTo，Square/Round Cap 用默认法线 `(1, 0)` 兜底——**法线方向被强制到 +X 轴**，与真实切线方向无关。
- **Join 几何失真**：若相邻两条 edge 都用了 `(1, 0)` 默认法线，`fJoiner` 看到两条法线相同，认为是直线连续，**不生成 Miter spike / Bevel 切角**；若其中一条法线正确、另一条默认，spike 方向由"错误平均"决定，输出朝向偏离真实外法线。
- **finishContour 走 fSegmentCount==0 分支**：若所有 lineTo 都在第 ① / ② 步被淘汰（Butt Cap 极短 path 的常见结果），fOuter / fInner 仍为空，最终输出 path `isEmpty=1`。

实测（DegenerateStroke_FigmaRefer 列 6 矩形 W=H=0）印证了这条链路：Butt Cap 输出 `bounds=[0,0,0,0]`、Square/Round Cap 输出"朝 N/E/S/W 而非 NE/NW/SE/SW"的菱形——cap 几何沿默认法线 `(1, 0)` 各自展宽，4 个 sub-path 叠加后主轴落在坐标轴上。

修复方向（不在本次范围）：让 Rectangle / Ellipse 兜底值大到使 dash 切出的 sub-path 长度 > stroker 数值精度阈值（实测阈值约 0.001），或在 AdaptiveDashEffect 层对 path 总长 < dash 周期时跳过切 dash 直接输出连续 path。

### 2.4 退化几何的处理现状

§2.1 / §2.2 描绘的是"几何完好"假设下的主线流程。当几何退化（Rectangle / Ellipse 任一维度=0、Polystar `outerRadius=0`）时，阶段 [1] 输出的 path 进入阶段 [3] / [4] 的 stroker 后会被判定为零长度 edge 直接丢弃，需要在阶段 [1] 给几何加兜底来规避。下面盘点 4 类几何在阶段 [1] 的退化处理现状。

**几何类的退化处理**

- **Rectangle** (`src/layers/vectors/Rectangle.cpp:67-91`)：`5e-4` 兜底（commit `c9ae4c9c`）让 W=0 / H=0 维度被替换为极小值，保留 4 个 90° 顶点和非零边。stroker 输出与 Figma 视觉对齐。
- **Ellipse** (`src/layers/vectors/Ellipse.cpp:56-70`)：直接 `addOval(emptyRect)`，无任何兜底。empty rect 上 `addOval` 退化为 0 面积 contour，stroker 输出空。
- **Polystar** (`src/layers/vectors/Polystar.cpp:36-137`)：`ConvertStarToPath` / `ConvertPolygonToPath` 按 `outerRadius` 算极坐标顶点，`outerRadius == 0` 时所有顶点重合于中心，stroker 输出空。

**StrokeStyle 与 stroker 对退化输入的处理**

`StrokeStyle::apply` 早退条件（`_colorSource == nullptr || _stroke.width <= 0.0f || context->geometries.empty()`）不会因退化几何提前返回——退化输入会进入 stroker。`Stroke::applyToPath` 走 Skia `SkPaint::getFillPath`，零长度 edge 在 `SkPathStroker::lineTo` 中被 `EqualsWithinTolerance` 判定为 teeny 后早退，全退化几何（所有 edge zero-length）完全无输出。

这意味着只要让退化几何在传入 stroker 前保留极小但非零的边长（如 Rectangle 的 `5e-4`），就能让 stroker 把它当作"正常但极小"的几何处理，配合 Outside Boolean Op 后输出由 strokeWidth 撑起的可见形状。Rectangle 通过 `5e-4` 兜底验证了这条路径有效；Polystar / Ellipse 缺乏等价兜底。

### 2.5 实例：W=H=0 矩形 + 虚线 Square Cap + Miter Join

以 `DegenerateStroke_FigmaRefer` 行 1 列 6 为例（Rectangle Size=0×0, StrokeWidth=40, dash 10/80, dashOffset=5, dashAdaptive=true, LineCap=Square, LineJoin=Miter, StrokeAlign=Outside），跟踪三个阶段的 path 内容与计算过程。Figma 期望此场景输出**边长 40 的轴对齐方形**，tgfx 实测输出**朝 N/E/S/W 4 个方向的菱形**——下文实例数据揭示这一差距的成因。

#### 阶段 ① 几何中心线 path

`Rectangle::apply` 命中 `5e-4` 兜底（`src/layers/vectors/Rectangle.cpp:67-91`），把 0×0 替换为 5e-4×5e-4 的闭合矩形。中心位于原点，path 顺时针绕（屏幕坐标 +Y 朝下）：

```
moveTo  (0.00025,  -0.00025)        右上
lineTo  (0.00025,   0.00025)        右下
lineTo (-0.00025,   0.00025)        左下
lineTo (-0.00025,  -0.00025)        左上
lineTo  (0.00025,  -0.00025)        回到右上
close
```

4 个顶点构成边长 5e-4 的矩形，每条 edge 长 5e-4。

#### 阶段 ② AdaptiveDashEffect::filterPath 切 dash 后

`_intervals = {10, 80}`、`intervalLength = 90`、`_phase = 5`（dashOffset=5）。每条 edge 长 5e-4：

```
patternRatio = max(round(5e-4 / 90), 1.0) = 1
scale        = 5e-4 / (1 × 90) ≈ 5.556e-6
currentPos   = -_phase × scale = -5 × 5.556e-6 ≈ -2.778e-5
dashLength   = 10 × scale ≈ 5.556e-5（dash 段）
gapLength    = 80 × scale ≈ 4.444e-4（间隔段）
```

每条 edge 上 dash 起点在 `currentPos = -2.778e-5`（segment 起点之前 2.778e-5），dash 终点在 `currentPos + dashLength = 2.778e-5`。这意味着首个 dash 跨越 segment 起点（即矩形拐角），起点端的 2.778e-5 长度落在前一条 segment 末尾，终点端的 2.778e-5 落在当前 segment 开头。

`AdaptiveDashEffect` 的跨界拼接机制把首段 dash 分成两半：
- **首段 dash 起点端 deferred**：通过 `skipFirstSegment + deferFirstSegment` 暂存，最终在 contour 末尾以 `needMoveTo=false` 接续上一 segment。
- **后续 segment 的首段 dash 起点端**：以 `needMoveTo=false` 接续上一 segment 末段。

最终 4 个 dash 拼成 4 条 L 形 sub-path（每条对应矩形一个拐角，3 个点 + 2 条 lineTo）：

```
# Sub-path 1（右下拐角）
moveTo  (0.00025,    0.000222)      上边末段（拐角前 2.78e-5）
lineTo  (0.00025,    0.00025)       右下拐角
lineTo  (0.000222,   0.00025)       下边首段（拐角后 2.78e-5）

# Sub-path 2（左下拐角）
moveTo (-0.000222,   0.00025)
lineTo (-0.00025,    0.00025)
lineTo (-0.00025,    0.000222)

# Sub-path 3（左上拐角）
moveTo (-0.00025,   -0.000222)
lineTo (-0.00025,   -0.00025)
lineTo (-0.000222,  -0.00025)

# Sub-path 4（右上拐角）
moveTo  (0.000222,  -0.00025)
lineTo  (0.00025,   -0.00025)
lineTo  (0.00025,   -0.000222)
```

每条 lineTo 长度 = 5.556e-5 / 2 ≈ **2.78e-5**。每条 sub-path 中间的拐角点（如 sub-path 1 的 (0.00025, 0.00025)）是该 path 的内部顶点，理论上应触发 LineJoin。

#### 阶段 ③ Shape::ApplyStroke（doubled width = 80）后

stroker `radius = 80 / 2 = 40`。`Shape::ApplyStroke` 对**整个 dash path 调用一次 `SkStroke::strokePath`**——不是分别对 4 条 sub-path 各跑一遍 stroker。`SkPathStroker` 按 verb 顺序遍历 path，遇到每个 `moveTo` 自动 flush 上一段 sub-path 的描边几何到输出 dst（`SkStroke.cpp:391-398`）：

```
iter 遍历 path verb：
  kMove_Verb (sub-path 1 起点)        → stroker.moveTo（fSegmentCount=0，初始化）
  kLine_Verb (sub-path 1 第一条)       → stroker.lineTo
  kLine_Verb (sub-path 1 第二条)       → stroker.lineTo
  kMove_Verb (sub-path 2 起点)        → stroker.moveTo（fSegmentCount>0，触发 finishContour 把 sub-path 1 的 fOuter/fInner 拼成闭环写入 dst）
  ...
  kDone_Verb                          → stroker.done（最后一段 sub-path 收尾写入 dst）
```

最终 dst 是一个**含 4 个 contour 的单一 path**。后续 `Shape::Merge(dst, originalShape, PathOp::Difference)` 也只调用一次，把这个多 contour path 整体与原矩形做 Boolean 运算，得到单一最终 path。下面继续看每条 sub-path 通过 `lineTo` / `finishContour` 时具体发生了什么。

**第一条 lineTo (5.556e-5 长) 触发 teenyLine 提前 abort**：
- `EqualsWithinTolerance(prevPt, currPt, PK_ScalarNearlyZero × fInvResScale)` 中 `PK_ScalarNearlyZero = 1/4096 ≈ 2.44e-4`
- 两点距离 2.78e-5 < 2.44e-4 → `teenyLine = true`
- Square Cap：`teenyLine && (fJoinCompleted=false || hasValidTangent=true)` → **return**（第一条 lineTo 不写任何描边几何）

**第二条 lineTo 进 preJoinTo**：
- `set_normal_unitnormal`：切线 (-2.78e-5, 2.78e-5)，归一化后单位切线 (-0.707, 0.707)，`RotateCCW` 后单位法线 (-0.707, -0.707)
- `normal = unitNormal × radius = (-28.28, -28.28)`（屏幕 NW 方向）
- `fSegmentCount = 0`（第一条 lineTo abort 没计数）→ 走"第一条 edge"分支：
  - `fOuter.moveTo(prevPt + normal) ≈ (-28.28, -28.28)`（屏幕 NW）
  - `fInner.moveTo(prevPt - normal) ≈ (28.28, 28.28)`（屏幕 SE）

**finishContour 走开放 path 路径**（`SkStroke.cpp:325-335`）：sub-path 1 的下一个 `moveTo`（sub-path 2 起点）到来时触发——在 fPrevPt 末端调用 `fCapper`（Square Cap）、`fOuter.reversePathTo(fInner)` 把 fInner 反向拼到 fOuter，再在起点调用 `fCapper`，最后 `fOuter.close()`，把这条闭环写入 dst。

最终 sub-path 1 描边输出（实测）是一个绕原点的菱形 contour：

```
moveTo  ( 28.28,  28.28)        SE 方向，距原点 40
lineTo  ( 28.28,  28.28)        重复（teenyLine 留下的零长度 lineTo）
lineTo  ( 0,      56.57)        S 方向，距原点 56.57
lineTo  (-56.57,  0)            W 方向，距原点 56.57
lineTo  (-28.28, -28.28)        NW 方向，距原点 40
lineTo  ( 0,     -56.57)        N 方向，距原点 56.57
lineTo  ( 56.57,  0)            E 方向，距原点 56.57
lineTo  ( 28.28,  28.28)        回到起点
close
```

dump 输出 6 段 lineTo，但其中两组顶点共线，**几何上是 4 顶点的轴对齐菱形**：

- (56.57, 0) → (28.28, 28.28) → (0, 56.57) **三点共线**（都在直线 `y = -x + 56.57` 上，即菱形 E→S 边）；
- (-56.57, 0) → (-28.28, -28.28) → (0, -56.57) **三点共线**（直线 `y = -x - 56.57`，菱形 W→N 边）。

合并共线段后，contour 1 的真实顶点只有 **4 个 NSEW 方向距原点 56.57 的点**——即一个 4 个尖端朝坐标轴方向的菱形。SE / NW 方向的 (28.28, 28.28) 和 (-28.28, -28.28) 是 stroker `fOuter`/`fInner` 起点的 normal 偏移落点，恰好落在菱形对角边上，未构成独立尖端。

4 条 sub-path 各产出一个类似的 NSEW 菱形 contour（路径数据上起点 / 共线中点位置不同，但几何形状一致）。Boolean Op (`Difference` with original 5e-4 矩形) 后，4 个 contour 合并：

```
AfterBooleanOp bounds = [-56.57, -56.57, 56.57, 56.57]
```

最终输出是 4 个 NSEW 菱形的并集——形状仍是 4 尖端朝坐标轴方向的菱形，与实测视觉一致。

#### 差距根因

理论上每条 sub-path 是 `moveTo + lineTo + lineTo`（3 点拓扑）：拐角点是 path 内部顶点，stroker 应在拐角处生成 Miter spike。以 sub-path 1（矩形右上拐角处的 dash 切片）为例，拐角的两条邻边沿 +x 和 +y 方向，外法线沿 SE 方向（朝矩形外），spike 端点沿外法线偏移：

- spike 到拐角点的距离 = `strokeWidth / (2·sin(α/2))` = `40 / (2·sin(45°)) ≈ 28.28`（α=90° 为矩形内角，下同）
- spike 端点坐标 = `(28.28·cos(45°), 28.28·sin(45°)) = (20, 20)`

4 条 sub-path 各自对应矩形 4 个拐角，spike 端点分别落在 `(20, 20)`、`(-20, 20)`、`(-20, -20)`、`(20, -20)`。这 4 个点连成一个边长 40 的轴对齐方形——与 §3.1 Figma Miter 期望"边长 = strokeWidth 的轴对齐方形"在数值上一致。

实测 stroker 没有走"3 点 path 正常 join"分支，原因在第一条 lineTo 被 `teenyLine` 守卫提前 abort（dash 段 2.78e-5 < `PK_ScalarNearlyZero / fInvResScale ≈ 2.44e-4`）：

- `fSegmentCount` 没递增，Miter join 计算（`preJoinTo` 中的 `fJoiner` 调用）不发生
- 第二条 lineTo 单独走 stroker，被当作"开放单 edge"处理 + Square Cap 拼接 → 拓扑退化为"两端各一个 cap 矩形"，cap 沿切线方向（NE/NW/SE/SW）延伸 sw/2=40，cap 矩形的 4 个角落到坐标轴方向距原点 40√2 ≈ 56.57 的位置

根因是 dash 切出的 sub-path 长度（2.78e-5）远小于 stroker 的 `teenyLine` 阈值（2.44e-4），触发了 `SkPathStroker` 的精度退化路径，导致拐角处真实 LineJoin 无法生成。修复方向见 §5.2.1。

## 3. 参考设计：Figma 退化几何渲染规则

### 3.1 实线退化的输出形状

W=H=0 退化几何 + 实线描边的输出形状由 LineJoin 决定。从 FigmaRefer 列 3 / 4 / 5 测量得到：

| 原几何 | LineJoin = Miter | LineJoin = Bevel | LineJoin = Round |
|---|---|---|---|
| Rectangle | 边长 = strokeWidth 的轴对齐方形 | 4 顶点菱形（顶点距中心 strokeWidth/2） | 直径 = strokeWidth 的圆 |
| Triangle (n=3) | 3 顶点正三角形（顶点距中心 strokeWidth） | 3 切角倒三角形 | 直径 = strokeWidth 的圆 |
| Polystar Star (n=5) | 10 顶点星形（外凸 spike ≈ 1.618·strokeWidth） | 10 切角齿轮形 | 直径 = strokeWidth 的圆 |
| Ellipse | ❌ 不绘制 | ❌ 不绘制 | ❌ 不绘制 |

测量规律：

- Miter 输出形状的最远顶点到中心距离 = `strokeWidth/(2·sin(α/2))`，α 为原几何顶点内角；
- Bevel 输出形状的最远顶点到中心距离 = `strokeWidth/2`；
- Round 输出形状是直径 = strokeWidth 的圆。

椭圆**没有 vertex**——Join 无作用域，实线下退化无任何渲染依据，输出为空。虚线下因每段 dash 引入端点（Cap 生效），椭圆退化仍可绘制（见 §3.2）。

FigmaRefer 中所有退化几何的最尖角是五角星 36°（1/sin(18°) ≈ 3.24），低于默认 miter limit 4，因此 Miter spike 都被完整保留。tgfx 沿用默认 miter limit 即可，与几何是否退化无关。

**机制推测（未经 Figma 源码 / 官方文档验证）**：上述输出形状的差异（矩形 = 方形 vs 菱形 vs 圆，三角形 / 星形又各自不同）只能由原几何的拓扑信息决定——具体是顶点位置和每个顶点处两条邻边的法向量。这两组信息是 Miter / Bevel / Round join 几何生成的标准输入。推测 Figma 在退化几何（W=H=0）下并未真正"丢失"这些拓扑信息，描边器看到的仍是完整顶点序列，只是相邻顶点的距离退化为零。tgfx 的 `5e-4` 兜底（§2.4）正是这种推测在工程上的等价实现——保留 4 个 90° 顶点和邻边非零长度，让 Skia stroker 走正常 join 流程，在**实线**场景下产出与 Figma 视觉一致的输出形状。虚线场景因 dash 切片后 sub-path 长度可能低于 stroker `teenyLine` 阈值，需要额外处理（见 §2.5、§5.2.1）。

### 3.2 虚线退化的输出形状

FigmaRefer.png 列 6 / 7 / 8（Square Cap + Miter / Square Cap + Bevel / Round Cap + Bevel）显示虚线退化输出与实线**不同**：

- 矩形 + 虚线 Square Bevel = 方形（不是实线 Bevel 的菱形）；
- 三角形 + 虚线 Square Miter = 6 角星（顶点数翻倍）；
- 五角星 + 虚线 Square Bevel = 多齿圆形（齿数多于 5）；
- 椭圆虚线下能渲染（实线下不绘制，见 §3.1）——虚线引入的 dash 端点让椭圆退化也有 Cap 可用。

**机制推测（未经 Figma 源码 / 官方文档验证）**：虚线退化输出与实线不同——这只能由"dash 切割引入额外端点 + Cap 在每个端点生成几何"决定。退化几何被 dash 切成若干极短开放子段后，每段两端获得 Cap 作用域：Square cap 在每个端点生成 strokeWidth/2 的方形帽，多个端点位置（继承原几何顶点拓扑）叠加产生合成形状——这解释了三角形虚线为何输出 6 角星（3 顶点 × 2 端点叠加）、椭圆虚线为何能渲染（dash 让原本无端点的椭圆退化也获得 Cap 作用域）。Cap 类型和 dash pattern 共同决定合成形状的细节。

## 4. 对比分析

| 退化方式 | 场景 | tgfx 现状 | Figma 期望 | 差距 | 处理 |
|---|---|---|---|---|---|
| 非退化 | 所有几何 + 实线 | 与 Figma 对齐 | 一致 | 无 | — |
| 非退化 | 矩形 / 三角形 / 五角星 + 虚线 | dashOffset 默认 0 时 dash 端点对齐拐角，Round Cap 圆头覆盖拐角；即便 dashOffset = dashLength/2，也仅 contour 起点 P0 处的 LineJoin 由 `deferFirstSegment` 拼接生效，其他顶点仍是两段独立 sub-path 在该点 Cap 重叠（§2.2） | 所有拐角 Miter 尖角清晰 | 调用方约定 `dashOffset = dashLength / 2` 让 P0 顶点 LineJoin 生效；其他顶点的 LineJoin 缺失需 `AdaptiveDashEffect` 跨 segment 共享相位状态修复 | 调用方约定本次落地（测试按 §5.5 构造）；非起点顶点 LineJoin 缺失为独立改造项（§7 后续工作） |
| 非退化 | 椭圆 + 虚线 | 与 Figma 对齐（dashAdaptive=true 段数为整数） | 一致 | 无 | — |
| 退化为点 | Rectangle + 实线 | 与 Figma 对齐 | 方形 / 菱形 / 圆 | 无 | — |
| 退化为点 | Rectangle + 虚线 + Miter join | 输出 4 尖端朝坐标轴的菱形（dash 切片后 sub-path 触发 stroker `teenyLine` 阈值，Miter join 不生成） | 边长 = strokeWidth 的轴对齐方形 | 缺：dash 切片对极短 path 的 early-return | 本次（§5.2.1） |
| 退化为点 | Rectangle + 虚线 + 其他 Cap × Join 组合 | 与 Figma 对齐 | 合成形状 | 无 | — |
| 退化为点 | Triangle / Polystar Star + 实线 / 虚线 | 输出空 | 多边形 / 星形 / 合成形状 | 缺：极小值兜底 | 本次（§5.2.3） |
| 退化为点 | Ellipse + 实线 / 虚线 | 输出空 | 不绘制 / 合成形状 | 缺：极小值兜底 | 本次（§5.2.2） |
| 退化为边 | Rectangle / Ellipse + 虚线 | 输出极小方块 | 沿单边 dash 序列 | 缺：单边退化 + 虚线渲染 | 后续工作（§7） |

> 注：Polystar（含 `pointCount=3` 的三角形）由单一 `_outerRadius` 驱动，无独立 W / H 维度，本身不存在"退化为边"。但 Figma 参考图 col 9（边退化 0×60）的 Polystar 行是用替代几何画出的占位形态——为了让 tgfx 的 col 9 整列输出可对照，本任务用"内层 group 缩放"模拟单边退化（详见 §5.2.4）。`ShapePath` 由用户自定义路径，本方案不处理。

## 5. 实现方案

### 5.1 设计原则

1. **退化用极小值兜底**：在 `Rectangle` / `Ellipse` / `Polystar` 的 `apply` 阶段判定是否退化，退化时把 0 维度替换为极小值（参考 Rectangle 当前 `5e-4`），让 stroker 在保留几何拓扑的基础上正常产出描边形状。
2. **改动以几何类为主**：StrokeStyle、stroker 代码、`applyStrokeAndAlign` 保持不变。`AdaptiveDashEffect` 仅在 dash 切片对极短 path 失效的特定场景（§5.2.1）允许新增 early-return 判定，不改 dash 切片主逻辑。
3. **改动作用域最小化**：极小值兜底仅在 W=0 / H=0 / `outerRadius == 0` 分支内生效；§5.2.1 的 `AdaptiveDashEffect` 改动仅在 contour 总长低于 stroker `teenyLine` 阈值的极短 path 上生效，需通过测试确认对正常长度 path 无影响。
4. **ShapePath 不处理**：路径由调用方提供，TGFX 不做退化判定；输入退化时 stroker 输出空，由调用方按需自行修正。

### 5.2 各几何类的退化处理

#### 5.2.1 Rectangle

当前 `5e-4` 兜底（commit `c9ae4c9c`）覆盖 W=H=0 全退化与单边退化两种情况，整体效果：

- **W=H=0 + 实线**：输出与 Figma 视觉对齐（实测）。
- **单边退化 + 实线**：在 `VectorLayerTest::RectangleAsLine` 有稳定 baseline。
- **单边退化 + 虚线**：与 Figma 存在差距（§4 退化为边行），属于后续工作。
- **W=H=0 + 虚线 + Miter join**：与 Figma 期望差距明显——Figma 期望边长 40 的轴对齐方形，tgfx 输出 4 尖端朝坐标轴的菱形（§2.5 已分析）。根因是 dash 切出的 sub-path 长度 2.78e-5 远低于 `SkPathStroker::teenyLine` 阈值 2.44e-4，stroker 跳过 LineJoin 计算后退化为"两端各一个 Square Cap 矩形"的拼接。

W=H=0 + 虚线 + Miter join 的差距必须本次解决（属于"实线已对齐、同退化几何虚线不该错"的回归类问题）。候选方案：

- **方案 A：增大兜底值至 ≥ 1.1e-3**。实测 MinExtent ≥ ~1.1e-3 时 stroker 走"3 点 path 正常 join"分支，输出方形与 Figma 一致。代价：影响所有 W=H=0 退化矩形的现有 baseline（不仅是虚线 Miter，包括实线 / Bevel / Round 等所有已对齐组合），需重跑 baseline 并逐项确认对齐 Figma 不退化。
- **方案 B：在 `AdaptiveDashEffect::filterPath` 入口判定**：当 `contour.length` 小于 dash pattern 周期 / 给定阈值的极短 path 时，跳过 dash 切片，直接返回原 path 让 stroker 当作实线处理。代价：影响极短 path（无论是否退化）的虚线行为，需考察非退化但 contour 总长极短（如 1×1 矩形 + 虚线）场景是否仍合规。

倾向方案 B：作用域更精确（只影响"短到 stroker 都画不出 dash"的 path），不动 Rectangle 几何兜底，对实线 baseline 零影响。具体阈值与判定条件在实现阶段实测确定。

#### 5.2.2 Ellipse

在 `Ellipse::apply` 引入极小值兜底：当 `_size.width` 或 `_size.height` 为 0 时替换为 `5e-4`，让 `addOval` 产出极小但非零面积的椭圆。

Figma 在退化椭圆上的语义是：实线不绘制（无 vertex 无 join 作用域）；虚线由每段 dash 引入端点，Cap 叠加产出合成形状（§3）。tgfx 用极小值兜底的实测结果（在本任务实现 `Ellipse.cpp` 兜底后跑 `DegenerateStroke_FigmaRefer` 得到）：

- 实线 + Outside Stroke：输出视觉不可见，与 Figma "不绘制" 等价；
- 虚线（含 `dashAdaptive=true`）：输出合成形状，与 Figma 一致。

具体机制（为什么实线下输出不可见、虚线下能产出形状）涉及 `Shape::ApplyStroke` 与 `Shape::Merge` 在极小 path 上的具体行为，本方案不做深入分析；以实测对齐 Figma 为准。

#### 5.2.3 Polystar

在 `Polystar::apply` 引入极小值兜底：当 `_outerRadius == 0` 时替换为 `5e-4`。Star 类型的 `_innerRadius`（若原始 `_innerRadius == 0`）同样替换为 `5e-4`。所有顶点分布在极小半径圆上，stroker 可正确产出 join 几何。

> Polystar 与 Rectangle / Ellipse 的几何拓扑不同（n 个顶点而非 4 个 / 0 个），同一 `5e-4` 值是否在所有 LineJoin × LineCap 组合下都对齐 Figma，需通过 §6.1 截图测试 + §7 阶段 4 实测确认；若实测发现差距，再单独调整极小值或引入针对性兜底。

#### 5.2.4 Polystar 单边退化的测试构造

Polystar 由单一 `outerRadius` 驱动，没有独立 W / H 维度，**没有源码层面的"单边退化"语义**——所以不在 `Polystar::apply` 里加兜底。但 FigmaRefer 的 col 9（边退化 0×60）在 Polystar 行也画了占位输出（row 2 / row 3 col 9）。为了让 tgfx 的 col 9 整列输出可对照，在**测试用例**里用嵌套 group 模拟"x 方向退化"：

```
outerGroup (position, contains [shape, fill, stroke])
  ├── innerGroup (scale = (kx, 1))
  │     └── polystar (full-radius geometry)
  ├── fillStyle
  └── strokeStyle
```

innerGroup 的 scale 仅作用于 polystar 几何（在 inner 空间被压扁到 x ≈ 5e-3）。stroke 在 outerGroup 一层，capture 到的 `innerMatrix` 已含 inner scale，但 capture 的 path 是 polystar 原始几何 → `Shape::ApplyMatrix(shape, innerMatrix)` 把 path 缩到 inner 空间后再 stroke，stroker 看到的输入是 x 退化的退化几何，stroke width = 20 在 inner 空间执行；之后 `outerMatrix = inv(innerMatrix) · geometry.matrix = I`，**stroke width 不被外层矩阵再次缩放**。

参数：
- row 2（Polystar Polygon n=3）：`outerRadius = 60 / 1.5 = 40`（让总 y 高度 = 60，与 row 1 col 9 矩形 0×60 视觉对齐），`kx = 5e-3 / (2 · 40 · cos30°) ≈ 7.22e-5`。
- row 3（Polystar Star n=5）：`outerRadius = 50, innerRadius = 25`（与 row 3 其它列保持一致，不调整 y 高度），`kx = 5e-3 / (2 · 50 · cos18°) ≈ 5.26e-5`。

此构造仅用于截图测试对照 Figma 占位图，不属于 source-tree 的退化兜底逻辑。

### 5.3 极小值取值

沿用 Rectangle 当前实现的 `5e-4`：

- 大于 `FLOAT_NEARLY_ZERO`（1/4096 ≈ 2.44e-4），stroker 处理**未经 dash 切片**的 path 时不会判定 edge 为零长度而早退；
- 远小于 GPU 子像素网格（约 1/16），视觉上不可见。

Polystar / Ellipse 沿用同一数值，避免单独调参。

**dash 场景的局限**：dash 切片会把 contour 总长按 dash pattern 周期重新分配（如周长 ~`9 × strokeWidth` 的退化矩形被切成 18 段后，每段长 ~`2.78e-5`），切片后 sub-path 长度远小于 stroker `teenyLine` 阈值，导致 stroker 在 sub-path 内退化。这种场景不通过调整极小值兜底解决（任何放大都会破坏现有 baseline），改由 §5.2.1 的 dash 切片 early-return 机制处理。

### 5.4 未取证项的处理策略

极小值兜底路径在 Rectangle 上有大部分场景实测对齐 Figma（W=H=0 + 实线 / 单边退化 + 实线），且发现一处差距（W=H=0 + 虚线 + Miter join，见 §5.2.1）。Polystar / Ellipse 沿用同一兜底机制后，**正确性需通过 §6.1 截图测试 + §7 阶段 4 实测验证**。若实测发现某些组合下输出与 Figma 不一致，处理策略分两类：

- 与 §2.5 同类型的 dash 切片精度退化 → 复用 §5.2.1 的 `AdaptiveDashEffect` early-return 机制；
- 其他类型差距 → 针对该组合单独引入显式构造规则。

本方案不预设此类复杂性。

### 5.5 调用方约定：dashOffset

闭合路径（矩形 / 多边形 / 星形等带顶点的几何）使用虚线描边时，**调用方需将 `StrokeStyle::dashOffset` 设为 `dashLength / 2`**，让首个 dash 段的中点对齐 path 起点（= contour 拐角）。

**机制**：dashOffset 默认 0 时，首段 dash 从 segment 起点开始，dash 端点恰好落在拐角处，Round / Square Cap 在拐角生成圆头 / 方头，覆盖原本应由 LineJoin 渲染的拐角形态。设 dashOffset = dashLength/2 后，dash 中点对齐 segment 起点，配合 `AdaptiveDashEffect` 现有的跨界拼接逻辑（`skipFirstSegment` / `deferFirstSegment`），相邻 segment 末段 dash 与下一 segment 首段 dash 在拐角处合并为连续 path，拐角变为 path 内部顶点，stroker 按 LineJoin 类型渲染。

适用范围：仅对闭合 contour 有效（开放 path 的两端本身应由 Cap 渲染）。`DegenerateStroke_FigmaRefer` 测试用例在所有虚线列统一应用此约定。

### 5.6 ApplyStrokeToBounds 上界修复

§5.2 ~ §5.5 的兜底解决了"输入几何被退化为亚像素 path 后 stroker 输出包含哪些拐角"的问题，但还有一个独立根因：**stroker 输出 contour 的 bounds 上界估计偏小**，会让下游 GPU 三角化用错误的画布尺寸光栅化。

**接口契约**（`include/tgfx/core/Shape.h::getBounds`）：

> The bounds might be larger than the actual shape because the exact bounds can't be determined until the shape is computed.

`Shape::getBounds()` 是上界，可以高估，不能低估。下游 `ProxyProvider::createGPUShapeProxy` 用 `shape->getBounds()` 决定光栅化画布尺寸和路径平移量，`PathTriangulator::ToAATriangles` 用同一 bounds 作为 AA 三角化的 clipBounds——它们都假设 path 在 bounds 内。

**违约现象**：`StrokeShape::onGetBounds()` 调用 `ApplyStrokeToBounds`，原实现只对 input bounds outset `SW/2`（miter join 时乘 miterLimit）。这个公式忽略了 **square cap 沿 45° 对角方向延伸 `SW·√2/2`** 的几何，导致 square cap 时实际 stroker 输出的 contour 可能超出声明的 bounds。

**实测**：dashed 退化三角形（input bounds ≈ 0.009×0.0075px）+ doubled stroke (SW=100, square cap, bevel join)：
- `StrokeShape::getBounds()` 声明 = `[-50, -50, 50, 50]`（100×100）
- 实际 stroker 输出 path bounds = `[-68.30, -68.30, 68.30, 68.30]`（136.6×136.6）
- **声明值小于实际值 36.6 px**，违反上界契约

**下游观察到的视觉错误**：因 ProxyProvider 按 100×100 分配画布，path 的对角延伸部分（齿轮的尖角）落在画布外，被 PathTriangulator 的 clipBounds 裁掉，最终 shape 呈现为方形带四角凸出，而非完整齿轮。本次任务里 `DegenerateStroke_FigmaRefer` 的所有 square-cap 退化列都受此影响。

**几何上界推导**（穷举 cap / join 几何，列出 contour 上每点到 input bounds 的最大距离）：

| 类型 | 真实延伸上界 |
|---|---|
| Butt cap | `SW/2`（法向） |
| Round cap | `SW/2`（任意方向） |
| **Square cap** | **`SW·√2/2`（对角方向）** |
| Round join | `SW/2` |
| Bevel join | `SW/2` |
| Miter join | `SW·miterLimit/2` |

每条 stroker 输出 contour 上的点都属于"沿边平移 ±N"或"cap 几何"或"join 几何"之一，距离 input bounds 不超过对应分类的上界值。这是几何严格的——self-intersection 不引入新点，只是不同段 contour 互相穿过。

**修复**：`src/core/utils/StrokeUtils.cpp::ApplyStrokeToBounds` 把 outset 公式改为取所有适用类型的上界最大值：

```cpp
auto width = TreatStrokeAsHairline(stroke, matrix) ? 1.0f : stroke.width;
auto halfWidth = width * 0.5f;
auto expand = halfWidth;
if (applyMiterLimit && stroke.join == LineJoin::Miter) {
  expand = std::max(expand, halfWidth * stroke.miterLimit);
}
if (stroke.cap == LineCap::Square) {
  expand = std::max(expand, halfWidth * FLOAT_SQRT2);
}
expand = ceilf(expand);
bounds->outset(expand, expand);
```

`std::max` 而非相乘——一个 contour 点要么在 cap 几何里要么在 join 几何里，两者上界不会同时贡献。

**影响面**：`ApplyStrokeToBounds` 有 17 个调用点（包括 `StrokeShape::onGetBounds`、`MeasureContext`、`PathContent::measureBounds`、`RenderContext` 几处文本/网格/图像描边度量等）一并受益。修复后 `Shape::getBounds()` 在 square cap 场景下从"小概率违约"变为"几何严格上界"。

#### 5.6.1 §5.2 与 §5.6 的独立性证明

§5.2 兜底改的是 stroker 输入（保证退化几何有可用拐角/端点），§5.6 改的是 stroker 输出的 bounds 声明（让下游 GPU 三角化用对画布）。两者发力位置不同：

```
[输入 path] --(§5.2 兜底)--> stroker --(§5.6 bounds 声明)--> ProxyProvider/PathTriangulator
```

确认两者**互不可替代**用一组对照实验：

**实验输入**：dashed 退化三角形 sub-path（已经是 §5.2 兜底后的结果，input bounds ≈ 0.009×0.0075）+ doubled stroke (SW=100, LineCap::Square, LineJoin::Bevel)。同一 path 包装为两种 Shape，喂给同一份 GPU 渲染管线：

| 包装方式 | `getBounds()` 来源 | bounds 值 | 渲染输出 |
|---|---|---|---|
| `StrokeShape`（懒求值，`onGetBounds = ApplyStrokeToBounds`） | 旧公式 | 100×100（违约 36.6 px） | 方形带四角凸出（错） |
| `Shape::MakeFrom(strokeShape->getPath())`（PathShape，`onGetBounds = path.getBounds()`） | 实际 path bounds | 136.6×136.6 | 完整 12 顶点齿轮（对） |

两边**输入 path 字节相同**（右边的 path 就是左边 `strokeShape->getPath()` 的产物），唯一差异是 `getBounds()` 声明值。这证明：**path 几何已经正确（§5.2 已生效）的前提下，仅 bounds 声明偏小就足以让下游渲染错误**——§5.6 是独立于 §5.2 的根因，不能被 §5.2 覆盖。

反方向（§5.2 不可被 §5.6 替代）由 §5.2.1 ~ §5.2.3 的退化几何描述本身支撑：当 W=H=0 时 stroker 的输入 path 只有一个点，拓扑上没有任何 cap/join 几何可用，仅修正 bounds 声明无法凭空生出端点；必须通过 §5.2 把输入扩展为有可见维度的 path，stroker 才能产出几何。

**走过的弯路（避免重复踩坑）**：定位 §5.6 之前曾尝试两个非根因方案：

1. *在 `StrokeStyle::applyStrokeAndAlign` 里物化为 PathShape*（B 守卫返回 `Shape::MakeFrom(shape->getPath())`）。视觉上能修，但作用面只覆盖 VectorLayer 走 Outside-align stroke 这一条路径，不修 `StrokeShape` 在其它调用方下的同类问题；且懒求值 stroker 被强制提前执行，`StrokeShape` 的设计初衷被绕过。
2. *在 `ApplyStrokeToBounds` 加保守常数因子*（如 outset · 1.5）。被否决——square cap (`√2/2`)、miter (`miterLimit/2`)、round join 等真实延伸上界各不相同，没有通用常数能覆盖所有合法配置；且即便对当前 case 适用，对 user 自定义 miterLimit 等场景仍会再次违约。

最终的 §5.6 是直接让 outset 公式按几何推导取每种 cap/join 的精确上界，从根因层面消除 `Shape::getBounds()` 的违约可能。

## 6. 测试计划

### 6.1 截图测试用例

`VectorLayerTest::DegenerateStroke_FigmaRefer`（4 行 × 10 列）：复刻 FigmaRefer.png 的 4 种几何 × 10 种描边参数组合，作为极小值兜底实现的视觉对照基线。所有虚线列按 §5.5 约定设置 `dashOffset = dashLength / 2`。Polystar 行的列 9（单边退化 0×60）按 §5.2.4 的嵌套 group + x 缩放方式构造测试输入。

> 注：FigmaRefer.png 第 10 列底部标签 `Stroke Width: 20` 有误，正确值为 50（与图中实际渲染一致）；测试用例按 50 构造。

该测试为本次新增，无既有 baseline——首次执行时由 §7 阶段 5 的 `/accept-baseline` 入库（用户操作，Agent 不可代为执行）。

### 6.2 回归保护

- **既有 `VectorLayerTest` 全部通过**，重点关注：
  - `RectangleAsLine`：Rectangle 单边退化 + 实线 baseline（§5.2.1 第 2 条 bullet 提到的稳定参考）；
  - 既有矩形 / 椭圆 / 星形 + 虚线测试：验证 §5.2.1 方案 B 在正常长度 path 上不影响 dash 切片输出。
- **`CanvasTest::DrawEmptyRect_*` 保持不变**。
- **极短非退化 path + 虚线场景**：§5.2.1 方案 B 引入"contour 总长低于阈值时跳过 dash 切片"逻辑，需通过测试确认非退化但 contour 总长极短（如 1×1 矩形 + 虚线）的 case 仍合规。若既有测试未覆盖，在 `VectorLayerTest` 中补充至少一例"contour 长度接近阈值"的非退化用例。

### 6.3 性能影响

- 几何类退化兜底：只在 W=0 / H=0 / `outerRadius == 0` 时触发，且仅做"用极小值替换 0 维度"的赋值操作。非退化几何完全不进入新代码。
- `AdaptiveDashEffect` early-return：每次 dash 切片入口新增一次 `contour.length` 与阈值（不超过 stroker `teenyLine` 量级）的浮点比较；超过阈值的 path 走原 dash 切片路径，行为与现状一致。开销极小，不影响 hot path。

## 7. 实现步骤

| 阶段 | 文件 | 描述 |
|---|---|---|
| 1 | `src/core/AdaptiveDashEffect.cpp` | 按 §5.2.1 倾向方案 B 修复 W=H=0 矩形 + 虚线 + Miter join 差距：在 `AdaptiveDashEffect::filterPath` 入口对 contour 总长低于阈值的极短 path 跳过 dash 切片。具体阈值实测确定。若实测阈值取不到合适值（影响非退化短 path 的 baseline），回退到方案 A 改 `src/layers/vectors/Rectangle.cpp` 的兜底值至 ≥ 1.1e-3 |
| 2 | `src/layers/vectors/Ellipse.cpp` | 按 §5.2.2 引入极小值兜底 |
| 3 | `src/layers/vectors/Polystar.cpp` | 按 §5.2.3 引入极小值兜底 |
| 4 | `src/core/utils/StrokeUtils.cpp` | 按 §5.6 修复 `ApplyStrokeToBounds`：square cap 取 `halfWidth · √2`，与 base / miter 上界用 `std::max` 取最大 |
| 5 | `test/src/VectorLayerTest.cpp` | 跑 `DegenerateStroke_FigmaRefer`，对照 Figma 验证输出。若有差异按 §5.4 策略评估是否补充 |
| 6 | 用户执行 `/accept-baseline` | 接受截图基准（Agent 不可操作） |

**后续工作（不在本次范围）**

- `AdaptiveDashEffect` 在闭合 contour 上非起点顶点（P1/P2/P3...）的 LineJoin 缺失（§2.2 / §4 第 2 行）。修复需要让 segment 间共享相位状态、在接缝处用 `needMoveTo=false` 接续上一段 dash。
- Rectangle / Ellipse 单边退化 + 虚线渲染（Figma 沿单边方向 dash 序列，tgfx 输出极小方块，§4）。
- FigmaRefer 未覆盖的其他参数组合（其他 Cap × Join 虚线组合、StrokeAlign Inside/Center、不同 dash pattern 等）。如有需要再补 Figma 截图。

## 8. 参考资料

- Figma 行为参考截图：`~/Desktop/Work/项目/2026/0506 矢量图层渐变填充和描边效果优化/FigmaRefer.png`（矩形 / 三角形 / 五角星 / 椭圆 × 10 列描边参数组合的 Figma 网页版渲染）
- Skia stroker 源码：`/Users/chenjie/Desktop/Develop/TGFX_WT_0/third_party/pathkit/src/core/SkStroke.cpp` / `SkStrokerPriv.cpp`
- 相关源码路径：
  - `src/layers/vectors/Rectangle.cpp`
  - `src/layers/vectors/Ellipse.cpp`
  - `src/layers/vectors/Polystar.cpp`
