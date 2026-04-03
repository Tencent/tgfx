# 3D 上下文中背景样式（Background LayerStyles）支持方案

## 1. 背景

### 1.1 问题描述

父图层启用 `preserve-3d`（`canPreserve3D() == true`）后，TGFX 一刀切禁用了整个 3D 子树中所有图层的背景样式（`LayerStyleExtraSourceType::Background`），包括 BackgroundBlurStyle 等。但在 CSS 规范和 Chromium 实现中，父 `preserve-3d` + 子 `backdrop-filter` 是正常工作的组合。

### 1.2 需求

使 3D 上下文中的图层能够正常应用背景样式（如 BackgroundBlurStyle），与 Chromium 行为一致。

### 1.3 设计决策

当前禁用的根因：早期 Canvas 不支持设置 3×3 矩阵（透视变换），无法在 3D 上下文中通过常规方式绘制背景内容。因此 3D 上下文中一刀切禁用了 BackgroundBlurStyle。后来 Canvas 已支持 3×3 矩阵，但这个功能没有跟进补上，属于遗留任务。

核心决策：**在 BSP 合成阶段新增背景样式的执行能力**，通过在 DrawPolygon3D 中存储源图层的 `Layer*` 裸指针，使其能在 3D 排序确定绘制顺序后，通过指针访问图层的 `layerStyles()` 列表，基于已绘制的背景像素执行背景样式（如 BackgroundBlurStyle）。参考 Chromium 在合成阶段执行 backdrop-filter 的思路，以及 Chromium DrawPolygon 中 `original_ref_` 指向原始 DrawQuad 的设计。

### 1.4 术语定义

| 术语 | 含义 |
|---|---|
| 3D 上下文 | `DrawArgs.render3DContext != nullptr` 的绘制环境 |
| BSP 树 | 二叉空间分割树，用于 3D 多边形的深度排序 |
| DrawPolygon3D | 表示 3D 空间中经变换的可分割多边形 |
| 录制阶段 | 图层内容绘制到 PictureRecorder，生成 Image |
| 合成阶段 | BSP 遍历 + DrawOps 提交到 render target |

---

## 2. 现有实现分析

### 2.1 3D 合成管线数据结构

```
DrawPolygon3D
  _points: vector<Vec3>          // 屏幕空间变换后的 3D 顶点
  _normal: Vec3                  // 多边形法向量
  _depth: int                    // 图层树深度，用于排序
  _sequenceIndex: int            // 同深度内的序号
  _isSplit: bool                 // 是否由 BSP 分割产生
  _alpha: float                  // 图层透明度
  _antiAlias: bool               // 是否抗锯齿
  _image: shared_ptr<Image>      // 录制阶段生成的图层内容 Image
  _matrix: Matrix3D              // 3D 变换矩阵

  splitAnother(polygon, front, back, isCoplanar)  // 用自身平面分割另一个多边形
  toQuads() -> vector<Quad>                        // 转为渲染用 Quad 列表
```

```
Context3DCompositor
  _width, _height: int
  _targetColorProxy: shared_ptr<RenderTargetProxy>  // 合成用 render target
  _polygons: deque<unique_ptr<DrawPolygon3D>>       // 待合成多边形列表
  _drawOps: vector<PlacementPtr<DrawOp>>            // 积累的绘制操作
  _depthSequenceCounters: unordered_map<int, int>   // 每个 depth 的序号计数器

  addImage(image, matrix, depth, alpha, antiAlias)  // 添加图像到合成器
  finish() -> shared_ptr<Image>                     // 执行 BSP 排序 + 合成，返回结果
  drawPolygon(polygon)                              // 绘制单个多边形
  drawQuads(polygon, subQuads)                      // 生成 Quads3DDrawOp
```

```
BspTree
  _root: unique_ptr<BspNode>

  traverseBackToFront(action)   // 从后到前遍历，对每个多边形调用 action
  buildTree(node, polygons)     // 递归构建 BSP 树

BspNode
  data: unique_ptr<DrawPolygon3D>
  coplanarsFront: vector<unique_ptr<DrawPolygon3D>>
  coplanarsBack: vector<unique_ptr<DrawPolygon3D>>
  frontChild: unique_ptr<BspNode>
  backChild: unique_ptr<BspNode>
```

### 2.2 合成流程

```
Context3DCompositor::finish():
  1. sort(_polygons, DrawPolygon3DOrder)          // 按 (depth, sequenceIndex) 排序
  2. BspTree bspTree(move(_polygons))             // 构建 BSP 树
  3. bspTree.traverseBackToFront(drawPolygon)     // 从后到前遍历，收集 DrawOps
  4. addOpsRenderTask(_targetColorProxy, drawOps) // 一次性提交所有 DrawOps
  5. return TextureImage::Wrap(_targetColorProxy->asTextureProxy())
```

关键点：合成阶段没有使用 `BackgroundContext` 同步绘制背景的机制（非 3D 场景通过 `args.blurBackground` 额外 Canvas 同步绘制来收集背景像素，但 3D 合成路径绕过了这套流程）。

### 2.3 Layer.cpp 中禁用 Background 的三处代码

以下三处代码确保录制阶段不执行背景样式，使图层内容 Image 不含背景效果。本方案**保留这些禁用点**，改为在合成阶段执行背景样式。

**处 1：`drawByStarting3DContext()`（行 1907）**
```cpp
contextArgs.styleSourceTypes = StyleSourceTypesFor3DContext;
// StyleSourceTypesFor3DContext = {None, Contour}，不含 Background
```

**处 2：`createChildArgs()`（行 1943-1944）**
```cpp
childArgs.styleSourceTypes = StyleSourceTypesFor3DContext;
childArgs.blurBackground = nullptr;
```

**处 3：`drawLayerStyles()`（行 2131-2133）**
```cpp
if (layerStyle->position() != position ||
    !HasStyleSource(args.styleSourceTypes, layerStyle->extraSourceType())) {
  continue;  // Background 类型被 skip
}
```

### 2.4 BackgroundBlurStyle 正常工作流程（非 3D）

#### 双 Canvas 同步绘制机制

2D 场景下，背景样式的核心机制是**主 Canvas 和 backgroundContext 的 Canvas 同步绘制**：

```
Layer::draw() 入口：
  createBackgroundContext(maxOutset)
    → BackgroundContext::Make(drawRect, maxOutset, ...)
    → Surface 大小 = drawRect + outset（已扩展，覆盖模糊半径所需的额外采样区域）
  args.blurBackground = backgroundContext

drawContents():
  drawLayerStyles(Below)          → 背景样式从 backgroundContext 获取背景
  content->drawDefault(canvas)          → 写入主 Canvas
  content->drawDefault(backgroundCanvas) → ★ 同步写入背景 Canvas
  drawChildren()                        → 子图层同样同步写入两个 Canvas
  drawLayerStyles(Above)
```

**关键设计**：背景样式的绘制结果**只写入主 Canvas，不写入 backgroundContext**（`drawLayerStyles` 行 2185-2187）。这样后续图层获取的背景不包含前序图层的背景样式效果，只包含图层的普通内容。

#### 背景获取流程

```
drawLayerStyles():
  case Background:
    background = getBackgroundImage(args, contentScale, &offset)
    layerStyle->drawWithExtraSource(canvas, content, contentScale, background, offset, alpha)

getBackgroundImage():
  drawBackgroundImage(args, canvas)     → 从 backgroundContext 截取当前快照
    → blurBackground->getBackgroundImage()   → Surface::makeImageSnapshot()

BackgroundBlurStyle::onDrawWithExtraSource():
  blurFilter = getBackgroundFilter(contentScale)
  blurBackground = extraSource->makeWithFilter(blurFilter, &offset, &clipRect)
  maskShader = Shader::MakeImageShader(content, Decal, Decal)
  paint.setMaskFilter(MaskFilter::MakeShader(maskShader, false))
  paint.setBlendMode(Src)
  canvas->drawImage(blurBackground, offset.x, offset.y, &paint)
```

#### outset 扩展处理

模糊半径需要采样超出图层边界的像素。2D 场景下通过以下机制处理：

1. **`maxBackgroundOutset`**：每个 Layer 的背景样式模糊半径通过 `propagateLayerState()` 向上传播给祖先
2. **`BackgroundContext::Make(drawRect, maxOutset, ...)`**：创建背景 Surface 时，按 `maxOutset` 向外扩展 `drawRect`
3. **`filterBackground(srcRect, contentScale)`**：脏矩形更新时按模糊半径扩展脏区范围

### 2.5 优缺点

**优点**：
- 架构简洁，3D 合成路径不需要处理样式逻辑
- BSP 分割不需要考虑样式信息传播

**缺点**：
- 功能缺失：3D 上下文中 BackgroundBlurStyle 完全不生效
- 与 CSS/Chromium 行为不一致

---

## 3. 参考设计：Chromium 合成器中的 backdrop-filter

### 3.1 核心架构

Chromium 分为两层：**cc/trees/** 负责属性树（Property Tree）构建，决定 RenderSurface 创建和 `sorting_context_id` 传播；**viz/service/display/** 负责实际的 BSP 排序和渲染。`backdrop-filter` 作为 **RenderPass 属性**而非图层内容的一部分。

#### 属性树层（cc/trees/）

```
// cc/trees/transform_node.h
TransformNode {
    int sorting_context_id = 0;           // 3D 渲染上下文 ID，0 表示无 3D 上下文
    bool flattens_inherited_transform = true; // 是否展平继承的 3D 变换
    gfx::Transform local;                 // 本地变换矩阵
    gfx::Transform to_parent;             // 到父节点的组合变换
}

// cc/trees/effect_node.h
EffectNode {
    RenderSurfaceReason render_surface_reason;  // 创建 RenderSurface 的原因
    FilterOperations backdrop_filters;           // 背景滤镜
    float backdrop_filter_quality = 1.0;
    std::optional<SkPath> backdrop_filter_bounds;
    ElementId backdrop_mask_element_id;
}

// cc/trees/effect_node.h
RenderSurfaceReason 枚举（与本分析相关的值）：
  kNone
  kRoot                   // 根节点
  k3dTransformFlattening  // 3D 变换展平
  kBackdropFilter         // backdrop-filter（高优先级，独立于 3D 上下文）
  kFilter                 // CSS filter
  kOpacity                // opacity < 1
  ...
```

#### BSP 排序层（viz/service/display/）

```
// viz/service/display/draw_polygon.h
DrawPolygon {
    points_: vector<Point3F>             // 3D 空间中的多边形顶点
    normal_: Vector3dF                   // 归一化法线
    order_index_: int                    // 文档序号，共面时用于 tie-breaking
    original_ref_: raw_ptr<const DrawQuad>  // 指向原始 DrawQuad（BSP 分割后仍指向原始 Quad）
    is_split_: bool                      // 是否经过 BSP 分割

    SplitPolygon(polygon, *front, *back, *is_coplanar)  // 用自身平面分割另一个多边形
    ToQuads2D(*quads)                    // 扇形三角剖分，转为 2D QuadF 列表
    TransformToLayerSpace(inverse)       // 排序后变回图层空间，法线重置为 (0,0,-1)
}

// viz/service/display/bsp_tree.h
BspNode {
    node_data: unique_ptr<DrawPolygon>                    // 分割平面
    coplanars_front: vector<unique_ptr<DrawPolygon>>      // 共面的前向多边形
    coplanars_back: vector<unique_ptr<DrawPolygon>>       // 共面的后向多边形
    back_child: unique_ptr<BspNode>
    front_child: unique_ptr<BspNode>
}

BspTree {
    root_: unique_ptr<BspNode>
    BuildTree(node, polygon_list)                         // 递归构建 BSP 树
    TraverseWithActionHandler(handler)                    // 画家算法遍历
    // GetCameraPositionRelative: 仅检查 normal.z() > 0 判断前后
}
```

**关键设计**：`DrawPolygon::original_ref_` 是**原始指针**（非拷贝），BSP 分割时 front/back 子多边形**共享同一个 `original_ref_`**。这意味着携带 backdrop-filter 的 `RenderPassDrawQuad` 被分割后，所有碎片仍然指向原始 `RenderPassDrawQuad`，其 `render_pass_id` 可用于查询 backdrop filter 信息。

### 3.2 关键算法

#### ComputeRenderSurfaceReason — 决定层是否需要独立 RenderSurface

源码位置：`cc/trees/property_tree_builder.cc`

```cpp
RenderSurfaceReason ComputeRenderSurfaceReason(Layer* layer, ...) {
    if (is_root) return kRoot;
    if (layer->HasMaskLayer()) return kMask;
    if (!layer->filters().IsEmpty()) return kFilter;
    // backdrop-filter 直接触发，不受 preserve-3d 影响
    if (layer->HasBackdropFilter()) return kBackdropFilter;
    if (blend_mode != kSrcOver) return kBlendMode;
    if (opacity < 1.0 && num_descendants_that_draw > 1) return kOpacity;
    if (layer_clips_external_content && !preserves_2d_axis_alignment)
        return kClipAxisAlignment;
    return kNone;
}
```

**关键点**：backdrop-filter 是独立的、高优先级的触发条件，不论是否在 3D 上下文中。

#### AddEffectNodeIfNeeded — 在 EffectTree 中创建节点

源码位置：`cc/trees/property_tree_builder.cc`

```cpp
void AddEffectNodeIfNeeded(Layer* layer, ...) {
    bool requires_node = layer->HasOpacity() || layer->HasBackdropFilter() || ...;
    if (!requires_node) return;
    EffectNode node;
    node.backdrop_filters = layer->backdrop_filters();
    node.backdrop_filter_bounds = layer->backdrop_filter_bounds();
    node.backdrop_filter_quality = layer->backdrop_filter_quality();
    node.render_surface_reason = ComputeRenderSurfaceReason(layer, ...);
    if (!node.backdrop_filters.IsEmpty() && layer->mask_layer())
        node.backdrop_mask_element_id = layer->mask_layer()->element_id();
    effect_tree.Insert(node, parent_id);
}
```

#### DirectRenderer::DrawRenderPass — 3D sorting context 分流与 BSP 排序

源码位置：`viz/service/display/direct_renderer.cc`（行 705-812）

这是 BSP 排序的**入口点**。核心逻辑：按 `sorting_context_id` 将 DrawQuad 分为普通 2D 绘制和 3D BSP 排序两条路径。

```cpp
void DirectRenderer::DrawRenderPass(const AggregatedRenderPass* render_pass) {
    circular_deque<unique_ptr<DrawPolygon>> poly_list;
    int next_polygon_id = 0;
    int last_sorting_context_id = 0;

    for (auto it = quad_list.BackToFrontBegin(); ...) {
        const DrawQuad& quad = **it;

        // sorting_context_id 变化时，flush 当前 3D 多边形列表
        if (last_sorting_context_id != quad.shared_quad_state->sorting_context_id) {
            last_sorting_context_id = quad.shared_quad_state->sorting_context_id;
            FlushPolygons(&poly_list, ...);  // 构建 BSP 树 + 遍历绘制
        }

        // sorting_context_id != 0 → 3D 排序上下文
        if (quad.shared_quad_state->sorting_context_id != 0) {
            auto new_polygon = make_unique<DrawPolygon>(
                *it, RectF(quad.visible_rect),
                quad.shared_quad_state->quad_to_target_transform, next_polygon_id++);
            if (new_polygon->normal().LengthSquared() > 0.0)
                poly_list.push_back(move(new_polygon));
            continue;  // 不直接绘制，等待 FlushPolygons
        }

        // sorting_context_id == 0 → 普通 2D 绘制
        DoDrawQuad(&quad, nullptr);
    }
    FlushPolygons(&poly_list, ...);  // 处理最后一批
}
```

**关键点**：携带 backdrop-filter 的 `RenderPassDrawQuad` 在 3D sorting context 中被当作**普通 DrawQuad** 加入 `poly_list`，与其他 quad 一起参与 BSP 排序。BSP 算法不区分 quad 类型。

#### FlushPolygons — 构建 BSP 树并遍历绘制

源码位置：`viz/service/display/direct_renderer.cc`（行 598-611）

```cpp
void DirectRenderer::FlushPolygons(circular_deque<unique_ptr<DrawPolygon>>* poly_list, ...) {
    if (poly_list->empty()) return;
    BspTree bsp_tree(poly_list);                                    // 构建 BSP 树
    BspWalkActionDrawPolygon action_handler(this, scissor, flag);   // 绘制回调
    bsp_tree.TraverseWithActionHandler(&action_handler);            // 画家算法遍历
}
```

#### DoDrawPolygon — BSP 遍历中每个多边形的绘制

源码位置：`viz/service/display/direct_renderer.cc`（行 553-571）

```cpp
void DirectRenderer::DoDrawPolygon(const DrawPolygon& poly, ...) {
    SetScissorStateForQuad(*poly.original_ref(), ...);
    if (!poly.is_split()) {
        DoDrawQuad(poly.original_ref(), nullptr);     // 未分割 → 直接绘制原始 quad
    } else {
        vector<QuadF> quads;
        poly.ToQuads2D(&quads);                       // 分割后 → 扇形剖分为 2D quads
        for (auto& q : quads)
            DoDrawQuad(poly.original_ref(), &q);      // 以 clip_region 方式绘制每个子四边形
    }
}
```

**关键设计**：无论是否被 BSP 分割，绘制时都通过 `original_ref_` 找到**原始 DrawQuad**。如果原始 quad 是 `RenderPassDrawQuad`，`DoDrawQuad` 会走到 `DrawRenderPassQuad` → `CalculateRPDQParams` → 查询 `backdrop_filters` → `PrepareCanvasForRPDQ`，从而执行 backdrop-filter。BSP 分割产生的 `clip_region` 仅用于限制绘制范围，不影响效果查询逻辑。

#### SkiaRenderer 绘制 backdrop-filter

源码位置：`viz/service/display/skia_renderer.cc`

**CalculateRPDQParams**（行 3010-3150）：查询 `BackdropFiltersForPass(render_pass_id)` 获取 backdrop filter，转换为 `sk_sp<SkImageFilter>` 存入 `DrawRPDQParams.backdrop_filter`。

**PrepareCanvasForRPDQ**（行 1610-1648）：

```cpp
void SkiaRenderer::PrepareCanvasForRPDQ(const DrawRPDQParams& rpdq_params, DrawQuadParams* params) {
    // 1. 裁剪到 backdrop_filter_bounds（减少采样范围）
    rpdq_params.SetBackdropFilterClip(current_canvas_, params);
    // 2. 配置 layer paint，backdrop 时用 SrcOver
    SkPaint layer_paint = params->paint(nullptr);
    params->opacity = 1.f;
    if (rpdq_params.backdrop_filter) layer_paint.setBlendMode(SkBlendMode::kSrcOver);
    // 3. ★ 创建 saveLayer，Skia 读取当前 canvas 已绘制内容作为背景
    current_canvas_->saveLayer(SkCanvasPriv::ScaledBackdropLayer(
        &bounds, &layer_paint,
        rpdq_params.backdrop_filter.get(),      // backdrop filter（如 blur）
        rpdq_params.backdrop_filter_quality,     // 降采样系数
        0));
    // 4. 清除 backdrop 边界外的内容
    rpdq_params.ClearOutsideBackdropBounds(current_canvas_, params);
}
```

**执行流程**：`ScaledBackdropLayer` 让 Skia 截取当前 canvas 上已绘制的像素，对其应用 `backdrop_filter`（如高斯模糊），将结果作为新 layer 的初始内容，然后在其上绘制当前元素自身内容，最后 `restore()` 合并回 canvas。

### 3.3 非 Preserve-3D 场景（标准 2D 合成路径）

```
父图层 (transform-style: flat)
├── 子图层 A（普通内容）
└── 子图层 B（backdrop-filter: blur(10px)）

1. 子图层 B 触发 kBackdropFilter → 创建独立 RenderSurface
2. 子图层 A 的内容绘制到父图层的 RenderPass
3. 绘制子图层 B 的 RenderPassDrawQuad 时：
   - Skia 截取父级 RenderPass 中已绘制的内容（含子图层 A）
   - 对截取内容执行 blur(10px)
   - 在模糊结果之上绘制子图层 B 自身内容
   - 合成回父级
```

### 3.4 Preserve-3D 场景（3D 合成路径）

Property Tree 构建阶段，preserve-3d 的属性传播：

```cpp
void AddTransformNodeIfNeeded(Layer* layer, ...) {
    TransformNode node;
    // 父层 preserve-3d → 子层继承 sorting_context_id
    if (layer->sorting_context_id() != 0) {
        node.sorting_context_id = layer->sorting_context_id();
    }
    // preserve-3d 时不展平
    node.flattens_inherited_transform = layer->should_flatten_transform();
    transform_tree.Insert(node, parent_id);
}
```

**关键点**：
- 父层设置 `preserve-3d` → `sorting_context_id` 为非零值
- 子层继承同一个 `sorting_context_id`
- `flattens_inherited_transform = false` → 保留完整 3D 矩阵

preserve-3d 上下文中 backdrop-filter 的完整绘制链路：

```
父图层 (transform-style: preserve-3d, sorting_context_id = 42)
├── 子图层 A（rotateY(30deg), sorting_context_id = 42）
└── 子图层 B（rotateY(-10deg), backdrop-filter: blur(10px), sorting_context_id = 42）

1. Property Tree 构建（cc/trees/）：
   - 子图层 A: TransformNode(sorting_context_id=42)
   - 子图层 B: TransformNode(sorting_context_id=42) + EffectNode(kBackdropFilter)
   - 子图层 B 创建独立 RenderSurface → 生成 RenderPassDrawQuad

2. DirectRenderer::DrawRenderPass（viz/service/display/）：
   - 遍历 quad_list，A 和 B 的 RenderPassDrawQuad 都有 sorting_context_id=42
   - 两者都被包装为 DrawPolygon 加入 poly_list
   - FlushPolygons → 构建 BSP 树 → 画家算法遍历

3. BSP 排序与绘制（假设排序后 A 在 B 前面）：
   - BspWalkActionDrawPolygon 遍历每个 polygon：
     a. TransformToLayerSpace(inverse) — 变回图层空间
     b. DoDrawPolygon → DoDrawQuad(original_ref, clip_region)

4. 绘制子图层 A：
   - original_ref 是普通 DrawQuad → 直接绘制到 canvas

5. 绘制子图层 B：
   - original_ref 是 RenderPassDrawQuad → DrawRenderPassQuad
   - CalculateRPDQParams 查询 BackdropFiltersForPass → 找到 blur(10px)
   - PrepareCanvasForRPDQ：
     → Skia 读取 canvas 已绘制内容（包含子图层 A 的像素）
     → 对读取内容执行 blur(10px)
     → 在模糊结果上绘制子图层 B 自身内容
     → restore() 合成回 canvas

6. 如果子图层 B 被 BSP 分割（与 A 空间相交）：
   - 每个碎片的 original_ref 仍指向同一个 RenderPassDrawQuad
   - 每个碎片独立调用 DoDrawQuad(original_ref, &clip_region)
   - 每次都会触发 PrepareCanvasForRPDQ → 独立的 saveLayer + backdrop filter
   - 碎片交界处可能产生模糊接缝（与 §5.4 的分析一致）
```

### 3.5 关键设计原则

**效果与内容分离**：Chromium 将 `backdrop-filter` 作为 `EffectNode` 的属性，而非烘焙进图层内容。backdrop-filter 在合成阶段执行，不需要录制时知道背景内容。RenderSurface 的创建是属性驱动的，独立于 3D 上下文。

**RenderSurface 不阻断 3D 排序**：backdrop-filter 层虽然有独立的 RenderSurface，但其 `RenderPassDrawQuad` 携带 `sorting_context_id`，仍然参与 3D 深度排序。RenderSurface 只是一个中间缓冲，不改变该层在 3D 空间中的排序位置。

**背景捕获时机**：backdrop-filter 的背景捕获发生在 SkiaRenderer 绘制 `RenderPassDrawQuad` 的时刻。在 flat 模式下按 DOM 顺序绘制，在 preserve-3d 模式下按 3D 深度排序绘制。两种情况下，"背景"都正确反映了视觉上应该在当前层"后面"的内容。

**展平边界处理**：`k3dTransformFlattening` 创建的 RenderSurface 作用于 3D→flat 的过渡边界，确保 3D 子树被完整合成后再参与外部 flat 合成。这与 backdrop-filter 是两个独立的机制。

---

## 4. 对比分析

### 4.1 架构对比

| 维度 | TGFX 现有实现 | Chromium |
|---|---|---|
| backdrop-filter 执行时机 | 非 3D：录制阶段（`drawLayerStyles`）；3D：完全禁用 | 合成阶段（`SkiaRenderer`），不区分 2D/3D |
| 背景来源 | 非 3D：`getBackgroundImage()` 通过重新绘制获取；3D：无 | 从 render target 已绘制像素截取 |
| 3D 排序兼容性 | 不兼容，3D 上下文中一刀切禁用 | 天然兼容，效果在排序后执行 |
| 效果信息存储 | 非 3D：`DrawArgs.blurBackground`（录制时传递）；3D：无 | `RenderPass.backdrop_filters`（合成时传递） |
| BSP 分割时效果传播 | 不涉及（3D 上下文中效果被禁用） | 自然传播（效果附在 RenderPass / original_ref_ 上） |
| 改动范围 | 无（3D 场景下直接禁用） | 全局架构（RenderSurface + RenderPass） |

### 4.2 Preserve-3D 场景对比

| 维度 | 非 Preserve-3D (flat) | Preserve-3D |
|---|---|---|
| sorting_context_id | 0（无 3D 排序） | 非零值（3D 深度排序） |
| flattens_inherited_transform | true | false |
| backdrop-filter 触发 Surface | ✅ kBackdropFilter | ✅ kBackdropFilter（不变） |
| 子层 RenderPass 创建 | 有独立 RenderPass | 有独立 RenderPass（不变） |
| Quad 排序方式 | DOM 顺序/绘制顺序 | 3D Z 深度排序 |
| backdrop 读取的"背景" | 父级 RenderPass 中已绘制内容 | 3D 排序后已绘制内容 |
| RenderPassDrawQuad 变换 | 2D 变换 | 完整 3D 变换矩阵 |

---

## 5. 父元素同时设置 preserve-3d 与 backdrop-filter 的根本矛盾

本节分析的是 **CSS 规范层面** 为何同一元素不能同时设置 `preserve-3d` 和 `backdrop-filter`，与第 6 章的实现方案无关。实现方案处理的是**父元素 preserve-3d + 子元素 backdrop-filter** 的组合，不涉及此矛盾。

### 5.1 CSS 规范层面

CSS Transforms Level 2 §7.1 定义了"分组属性"(grouping properties)。当元素自身同时拥有 preserve-3d 和某个分组属性时，preserve-3d 的**使用值被强制变为 flat**。分组属性包括：`overflow`(非 visible/clip)、`opacity`(<1)、`filter`(非 none)、`clip`/`clip-path`、`isolation`(isolate)、`mask-image`、`mix-blend-mode`(非 normal)、`contain`(paint) 等。

**`backdrop-filter` 目前不在规范的分组属性列表中**，但 W3C 有一个 2019 年开启至今未关闭的 issue ([fxtf-drafts#357](https://github.com/w3c/fxtf-drafts/issues/357)) 专门讨论是否应该将其加入。实际浏览器行为不一致。

### 5.2 角色矛盾

- **preserve-3d 的语义**：该元素是一个"透明的 3D 容器"，不创建独立的合成层，子元素直接穿过它参与外部 3D 空间排序
- **backdrop-filter 的语义**：该元素是一个"有实体的平面"，需要创建 RenderSurface 截取背后像素并施加模糊

一个元素不能同时"是一个独立合成平面"又"不是一个独立合成平面"——这是架构层面的根本矛盾。

### 5.3 简单场景下的可行性分析

**不存在子元素穿插的简单场景**：

```
祖父元素 (有背景)
  └─ 父元素 (preserve-3d + backdrop-filter)
       └─ 子A (在父元素前方，无交叉)
```

绘制顺序：祖父 → 父元素 → 子A。父元素绘制时，背后像素（祖父内容）确实完整可用。理论上可以做 backdrop 捕获和模糊。

**这说明理论上简单场景是可行的**——矛盾不是"绝对不可能实现"，而是复杂场景下语义无法定义。

### 5.4 BSP 切割场景下的根本障碍：模糊的不可分片性

当 preserve-3d 上下文中子元素与父元素发生 3D 相交时，Chromium 会使用 BSP（Binary Space Partition）算法将相交多边形切割为不交叉的片段。

**场景示例**：

```
祖父元素、父元素(preserve-3d + backdrop-filter)、子A、子B
子A 将父元素切割为"父近"和"父远"
父元素将子B 切割为"子B近"和"子B远"

BSP 排序后绘制顺序（远→近）：
  1. 子A       (最远)
  2. 子B远
  3. 父元素远   ← 此时背后有：祖父 + 子A + 子B远 ✅
  4. 子B近
  5. 父元素近   ← 此时背后有：祖父 + 子A + 子B远 + 父元素远 + 子B近 ✅
```

从绘制顺序看，每个父元素片段绘制时"背后的像素"都是完整的。**但问题出在 blur 操作本身**：

**blur 是一个非局部（non-local）操作**——`blur(20px)` 意味着每个输出像素需要采样周围 20px 范围内的所有输入像素。当父元素被切成两个片段分别做 backdrop blur 时：

```
┌─────────────────┐
│  父元素远        │  ← 独立执行 blur，只采样本片区域
│  (模糊效果 A)    │
├── 切割边界 ──────┤  ← 接缝：两次 blur 在此处各自缺少对方的采样数据
│  父元素近        │  ← 独立执行 blur，只采样本片区域
│  (模糊效果 B)    │
└─────────────────┘
```

- **父元素远**的底部边缘做 blur 时，需要采样向下 20px 的像素，但那部分属于**父元素近**的区域——此时还未绘制
- **父元素近**的顶部边缘做 blur 时，需要采样向上 20px 的像素，但那部分属于**父元素远**——虽然已绘制但已经被后续内容覆盖

结果是切割边界处产生**可见的模糊接缝 (seam)**，两片的模糊效果在交界处不连续。

**这是根本的数学障碍**：blur 卷积算子要求输入在空间上连续，分片独立执行无法等价于整体执行。这不是"背后像素是否完整"的问题，而是"同一次模糊操作的输入被物理切割后无法还原"的问题。

### 5.5 结论

| 场景 | backdrop-filter 是否可行 | 原因 |
|---|---|---|
| **子元素无交叉的简单情况** | ✅ 理论可行 | 父元素绘制时背后像素完整，且不存在分片 |
| **子元素与父元素 3D 相交** | ❌ 效果不正确 | 父元素被 BSP 切割后分片独立 blur 产生接缝 |
| **多子元素互相交叉** | ❌ 更严重 | 切割片段更多，接缝更多，效果完全失控 |

CSS 规范选择了**一刀切**的策略：不区分简单和复杂场景，而是用 grouping property 机制统一禁止同一元素上 preserve-3d 与需要展平的效果共存。原因是只对简单场景开绿灯会引入难以定义的边界条件，且用户难以预判自己的场景是否会触发切割。

---

## 6. 实现方案：在 BSP 合成阶段新增背景样式执行能力

### 6.1 设计概述

在 BSP 合成阶段新增背景样式（`LayerStyleExtraSourceType::Background`）的执行逻辑。当前 3D 上下文中背景样式被完全禁用，无任何执行路径。

核心设计决策：**在 DrawPolygon3D 中存储源图层的 `Layer*` 裸指针**。合成阶段通过该指针直接访问图层的完整 `layerStyles()` 列表，从而支持所有类型的背景样式。

#### 设计依据

**存储 `Layer*` 裸指针的优势：**

1. **通用性**：TGFX 的 LayerStyle 是多态体系，包含 BackgroundBlurStyle、DropShadowStyle、InnerShadowStyle 等子类。存储 Layer 指针可以访问完整的 `layerStyles()` 列表，一套机制支持所有背景样式类型。
2. **零维护成本**：新增 LayerStyle 子类时，不需要修改 DrawPolygon3D 的数据结构。
3. **与 Chromium 思路一致**：Chromium 的 `DrawPolygon::original_ref_` 也是裸指针，指向原始 DrawQuad 以获取完整的渲染信息。

**裸指针的生命周期安全性：**

整个绘制是同步调用栈，Layer 对象在 DrawPolygon3D 的整个生命周期内不可能被释放：

1. `DisplayList::render()` 或 `Layer::draw()` 作为入口，到 `compositor->finish()` 结束，全在一次同步调用中完成
2. `_children` 是 `vector<shared_ptr<Layer>>`，遍历期间 `shared_ptr` 在栈上持有
3. DrawPolygon3D 在 `endRecording()` → `onImageReady()` → `compositor->addImage()` 中创建，在紧随其后的 `finishAndDrawTo()` → `finish()` 中消费销毁
4. 不存在跨帧缓存——每帧都从图层树重新遍历生成新的 DrawPolygon3D 集合

**TGFX 渲染模型的适配性：**

TGFX 采用「谁锁 Context 谁就是渲染线程」的单线程同步模型（Device mutex + lockContext/unlock），Layer 在整个绘制过程中可直接访问，不需要数据快照。未来如果引入多线程渲染，可增量重构——将 `Layer*` 替换为数据快照结构即可。

#### 实现步骤总览

1. DrawPolygon3D 新增 `_sourceLayer` 成员（`Layer*`，首参数，不允许 nullptr）
2. Context3DCompositor 持有 `_backgroundContext`，BSP 遍历时从中获取背景、检测并执行背景样式、逐步同步写入 polygon 内容
3. 录制阶段保持对背景样式的禁用（`StyleSourceTypesFor3DContext` 不含 Background），确保图层内容 Image 不含背景效果
4. Layer.cpp 中将 Layer 指针传递到 3D 合成管线
5. 移除 `Render3DContext::finishAndDrawTo()` 中将 3D 合成结果整体写入 backgroundContext 的逻辑（已由 BSP 遍历中逐步同步取代）

### 6.2 API 设计

#### DrawPolygon3D 扩展

```cpp
class DrawPolygon3D {
 public:
  // Constructs a polygon with a source layer reference.
  // sourceLayer must not be nullptr.
  DrawPolygon3D(Layer* sourceLayer, std::shared_ptr<Image> image, const Matrix3D& matrix,
                int depth, int sequenceIndex, float alpha, bool antiAlias);

  // Returns the source layer.
  Layer* sourceLayer() const { return _sourceLayer; }

 private:
  // Points to the source Layer for accessing layerStyles() during compositing.
  // Lifetime is guaranteed by the synchronous rendering call stack.
  Layer* _sourceLayer = nullptr;
};
```

#### Context3DCompositor 扩展

```cpp
class Context3DCompositor {
 public:
  Context3DCompositor(const Context& context, int width, int height,
                      std::shared_ptr<BackgroundContext> backgroundContext);

  void addImage(Layer* sourceLayer, std::shared_ptr<Image> image, const Matrix3D& matrix,
                int depth, float alpha, bool antiAlias);

 private:
  void drawBackgroundStyles(const DrawPolygon3D* polygon);
  void syncToBackgroundContext(const DrawPolygon3D* polygon);

  std::shared_ptr<BackgroundContext> _backgroundContext = nullptr;
};
```

### 6.3 核心数据结构变更

#### DrawPolygon3D

新增成员：
```
_sourceLayer: Layer*   // 指向源图层，不允许 nullptr，BSP 分割时传递给子碎片
```

**构造函数（public）**新增 `sourceLayer` 首参数：
```
DrawPolygon3D(sourceLayer, image, matrix, depth, sequenceIndex, alpha, antiAlias)
// DEBUG_ASSERT(sourceLayer != nullptr)
```

**构造函数（private，分割用）**同样传递 `sourceLayer` 首参数：
```
DrawPolygon3D(sourceLayer, image, matrix, points, normal, depth, sequenceIndex, alpha, antiAlias)
```

**`splitAnother()`**：分割时将 `polygon->_sourceLayer` 传递给 front 和 back 子 polygon。裸指针零开销传递，所有碎片共享同一个源图层引用。

#### Context3DCompositor

新增成员：
```
_backgroundContext: shared_ptr<BackgroundContext>  // 外部传入，已经过 outset 扩展处理
```

新增成员方法：
```
drawBackgroundStyles(polygon):
  // 见 6.4 关键算法伪代码

syncToBackgroundContext(polygon):
  // 将 polygon 的内容同步绘制到 _backgroundContext 的 Canvas 上
  // 使背景 Canvas 跟踪主 render target 的绘制进度
  // ★ 背景样式的结果不写入 backgroundContext，与 2D 行为一致
```

### 6.4 关键算法伪代码

#### 背景绘制的双 Canvas 同步机制

2D 场景下，TGFX 使用主 Canvas 和 backgroundContext 的 Canvas 同步绘制：每个图层的内容同时写入两个 Canvas，backgroundContext 持续跟踪已绘制的内容。需要背景时，从 backgroundContext 截取当前快照作为背景 Image。背景样式（如 BackgroundBlurStyle）的**结果只写入主 Canvas，不写入 backgroundContext**，避免后续图层的背景中包含前序图层的背景效果。

3D 合成阶段沿用相同机制：`_targetColorProxy` 对应主 Canvas，`_backgroundContext` 对应背景 Canvas。BSP 遍历中，每个 polygon 的内容绘制到两者；背景样式的结果只绘制到 `_targetColorProxy`。

#### BSP 遍历中的背景样式处理

```
Context3DCompositor::finish():
  sort(_polygons, DrawPolygon3DOrder)
  BspTree bspTree(move(_polygons))

  bspTree.traverseBackToFront([this](polygon):
    // Step 1: 如果当前 polygon 有背景样式，先执行
    if hasBackgroundStyle(polygon):
      drawBackgroundStyles(polygon)

    // Step 2: 绘制 polygon 内容到主 render target（与现有逻辑相同）
    drawPolygon(polygon)

    // Step 3: 同步绘制到 backgroundContext（让后续 polygon 能获取背景）
    //         ★ 只同步内容，不同步背景样式的结果
    syncToBackgroundContext(polygon)
  )

  // 一次性提交所有 DrawOps 到 render target（保持现有逻辑不变）
  auto opArray = context->drawingAllocator()->makeArray(move(_drawOps))
  context->drawingManager()->addOpsRenderTask(_targetColorProxy, opArray, PMColor::Transparent())
  return TextureImage::Wrap(_targetColorProxy->asTextureProxy())

hasBackgroundStyle(polygon):
  for style in polygon->sourceLayer()->layerStyles():
    if style->extraSourceType() == LayerStyleExtraSourceType::Background:
      return true
  return false
```

#### drawBackgroundStyles

```
Context3DCompositor::drawBackgroundStyles(polygon):
  auto layer = polygon->sourceLayer()
  // sourceLayer is guaranteed non-null by DrawPolygon3D constructor.

  // Step 1: 从 backgroundContext 获取背景 Image
  // backgroundContext 已包含：外部背景（3D 子树之前的内容）+ 已绘制的 polygon 内容
  // 且 outset 扩展已由外部 createBackgroundContext() 处理，无需再扩展
  if _backgroundContext == nullptr:
    return
  bgImage = _backgroundContext->getBackgroundImage()
  if bgImage == nullptr:
    return

  // Step 2: 获取 polygon 的 content image（图层内容，用作 mask）
  contentImage = polygon->image()

  // Step 3: 遍历 layer 的所有背景样式，逐个执行
  // 参考 Layer::drawLayerStyles 中 Background 分支的调用方式：
  //   layerStyle->drawWithExtraSource(canvas, content, contentScale, background, offset, alpha)
  // 绘制结果写入 _targetColorProxy（主 render target），不写入 _backgroundContext
  for style in layer->layerStyles():
    if style->extraSourceType() != LayerStyleExtraSourceType::Background:
      continue
    backgroundOffset = ... // 计算背景 Image 相对于 content 的偏移
    style->drawWithExtraSource(canvas, contentImage, contentScale,
                               bgImage, backgroundOffset, polygon->alpha())
```

#### syncToBackgroundContext

```
Context3DCompositor::syncToBackgroundContext(polygon):
  if _backgroundContext == nullptr:
    return
  auto bgCanvas = _backgroundContext->getCanvas()
  AutoCanvasRestore autoRestore(bgCanvas)
  // bgCanvas 已有初始矩阵 surfaceMatrix（世界坐标 → Surface 像素坐标）
  // 叠加 polygon 的变换（图层本地坐标 → 世界坐标）
  bgCanvas->concat(polygon->matrix().asMatrix())
  bgCanvas->drawImage(polygon->image())
```

#### 背景来源与 outset 处理

**外部 backgroundContext 的创建**（已有逻辑）：

`Create3DContext()` 调用时，`args.blurBackground`（即 backgroundContext）从外部传入。这个 backgroundContext 在 `Layer::draw()` 或 `DisplayList::drawRootLayer()` 中通过 `createBackgroundContext(maxOutset)` 创建，其 Surface 尺寸已经按 `maxBackgroundOutset`（从子图层的 BackgroundBlurStyle 模糊半径向上传播而来）扩展过。

**3D 合成器内部无需额外扩展**：
- 外部背景（3D 子树之前的内容）：来自 `_backgroundContext->getBackgroundImage()`，outset 已处理
- 3D 内部已绘制的 polygon 内容：通过 `syncToBackgroundContext()` 持续同步到 backgroundContext，也在 outset 范围内

**backgroundContext 中背景的完整组成**：

```
backgroundContext 的 Canvas 上内容（按绘制顺序）：
  1. 3D 子树外部的内容（由 Layer::draw() 中预先绘制）
  2. BSP 遍历中已绘制的 polygon 内容（由 syncToBackgroundContext 逐步写入）
  ★ 不包含任何 polygon 的背景样式结果（与 2D 行为一致）
```

**坐标对齐说明**：

1. **backgroundContext Canvas 的初始矩阵**：`BackgroundContext::Make()` 创建时已为 Canvas 设置了 `surfaceMatrix`（世界坐标 → Surface 像素坐标的变换）。2D 场景下，`Layer::draw()` 中会在此基础上叠加 `localToGlobalMatrix`，随后每个子图层的 `drawChild()` 递归叠加各自的变换矩阵。写入 backgroundContext 时无需手动处理 surfaceMatrix，Canvas 已内置。

2. **syncToBackgroundContext 的坐标变换**：polygon 的 image 在图层本地空间 (0,0,w,h)，需要两步变换才能正确写入 backgroundContext：
   ```
   图层本地坐标 → polygon->matrix().asMatrix() → 世界坐标（3D 上下文根坐标系）
   世界坐标 → backgroundContext Canvas 初始矩阵 → Surface 像素坐标
   ```
   具体操作为在 bgCanvas 上 `concat(polygon->matrix().asMatrix())` 后 `drawImage(polygon->image())`。bgCanvas 已有的初始矩阵完成第二步变换。

3. **drawBackgroundStyles 中背景 Image 的坐标变换**：`_backgroundContext->getBackgroundImage()` 返回的 Image 在 Surface 像素坐标系中。参考 `Layer::getBackgroundImage()` 的处理方式，需要经过完整的坐标变换链最终映射到图层局部坐标系（经 contentScale 缩放后），使其与 contentImage 在同一坐标系下：
   ```
   Surface 像素坐标 → backgroundMatrix()（surfaceMatrix 的逆） → 世界坐标
   世界坐标 → globalToLocalMatrix → 图层局部坐标 → contentScale 缩放
   ```
   最终传给 `LayerStyle::drawWithExtraSource()` 的 background Image 和 contentImage 都在图层局部坐标系中。

4. **polygon 顶点坐标系**：`addImage()` 时 `Render3DContext::onImageReady()` 已将 `pictureOffset - renderRect` 偏移叠加到 `imageTransform` 中。`DrawPolygon3D` 构造时将 Image 的 (0,0,w,h) 通过 matrix 映射到屏幕空间。因此 `polygon->points()` 的坐标已在 render target 坐标系内。

5. **Layer 指针访问安全性**：`polygon->sourceLayer()` 返回的裸指针在整个 BSP 遍历 + 合成期间始终有效。调用栈上的 `shared_ptr<Layer>`（来自父图层的 `_children` vector）保证了 Layer 对象不会被释放。

### 6.5 集成改造点

#### Layer.cpp 修改

**保留录制阶段对 Background 的禁用**

`drawByStarting3DContext()`（行 1907）和 `createChildArgs()`（行 1943-1944）中的 `StyleSourceTypesFor3DContext` 禁用逻辑**保持不变**。这确保 `drawLayerStyles()` 在录制阶段不执行 Background 类型的样式，图层内容 Image 不含背景效果，由合成阶段负责执行。

`createChildArgs()` 中 `childArgs.blurBackground = nullptr` 也**保持不变**，因为 3D 子树内部的图层不需要通过 `DrawArgs.blurBackground` 获取背景（改由合成阶段的 `_backgroundContext` 提供）。

**传递 backgroundContext 到 Context3DCompositor**

`Create3DContext()`（行 270-302）已将 `args.blurBackground` 传给 `Layer3DContext::Make()`，后者传给 `Render3DContext`。当前 `Render3DContext` 只在 `finishAndDrawTo()` 中用 `_backgroundContext` 来写入 3D 合成结果。

改造：将 `_backgroundContext` 进一步传递给 `Context3DCompositor`，使合成器在 BSP 遍历中能读取和写入背景。

```cpp
// Render3DContext 构造时传递给 compositor
Render3DContext::Render3DContext(..., shared_ptr<BackgroundContext> backgroundContext)
    : _compositor(make_shared<Context3DCompositor>(
          context, width, height, backgroundContext)),
      ...
```

**传递 Layer 指针到 3D 合成管线**

在 `drawChild()` 中，`beginRecording()` 时传递 child 的 Layer 指针：

```
drawChild():
  if context3D:
    targetCanvas = context3D->beginRecording(childTransform3D, antialiasing, child)
  // ...
```

`Layer3DContext::beginRecording` 扩展：
```cpp
Canvas* beginRecording(const Matrix3D& childTransform, bool antialiasing,
                       Layer* sourceLayer = nullptr);
```

`TransformState` 扩展：
```cpp
struct TransformState {
  TransformState(const Matrix3D& transform, bool antialiasing, Layer* sourceLayer)
      : transform(transform), antialiasing(antialiasing), sourceLayer(sourceLayer) {}
  Matrix3D transform = {};
  bool antialiasing = true;
  Layer* sourceLayer = nullptr;
};
```

`Layer3DContext::endRecording` 内部将 `sourceLayer` 传给 `onImageReady`：
```cpp
void onImageReady(shared_ptr<Image> image, const Matrix3D& imageTransform,
                  const Point& pictureOffset, int depth, bool antialiasing,
                  Layer* sourceLayer);
```

`Render3DContext::onImageReady` 内部传给 `_compositor->addImage()`：
```cpp
_compositor->addImage(sourceLayer, image, finalTransform, depth, 1.0f, antialiasing);
```

#### BSP 分割中 `_sourceLayer` 的传播

`DrawPolygon3D::splitAnother()` 分割时：
```cpp
*front = unique_ptr<DrawPolygon3D>(new DrawPolygon3D(
    polygon->_sourceLayer, polygon->_image, polygon->_matrix, move(frontPoints),
    polygon->_normal, polygon->_depth, polygon->_sequenceIndex, polygon->_alpha,
    polygon->_antiAlias));
*back = unique_ptr<DrawPolygon3D>(new DrawPolygon3D(
    polygon->_sourceLayer, polygon->_image, polygon->_matrix, move(backPoints),
    polygon->_normal, polygon->_depth, polygon->_sequenceIndex, polygon->_alpha,
    polygon->_antiAlias));
```

裸指针传递零开销，所有碎片共享同一个源图层引用。

---

## 7. 测试计划

### 7.1 截图测试

- 父 `preserve-3d` + 子 BackgroundBlurStyle：验证模糊效果正确应用
- 多个 3D 子图层互相遮挡 + 其中一个有 BackgroundBlurStyle：验证模糊背景来自 BSP 排序后的已绘制像素
- 3D 子图层被 BSP 分割为多个碎片 + BackgroundBlurStyle：验证碎片各自独立模糊

### 7.2 单元测试

- `DrawPolygon3D` 分割后 `_sourceLayer` 正确传播（front/back 都指向同一个 Layer）
- `syncToBackgroundContext()` 正确将 polygon 内容写入 backgroundContext
- 3D 合成完成后 backgroundContext 包含所有 polygon 内容（不含背景样式结果）

### 7.3 边界情况

- 图层有背景样式但模糊参数为 0：不触发实际模糊
- backgroundContext 为 nullptr（外部未创建背景上下文）：跳过背景样式逻辑
- render target 尺寸为 0：跳过
- BSP 退化（所有多边形共面）：正常走 coplanar 路径
- 多个图层都有背景样式：每个 polygon 独立触发 flush + 背景样式绘制

---

## 8. 实现步骤

| 阶段 | 文件 | 说明 |
|---|---|---|
| 1 | `DrawPolygon3D.h/.cpp` | 新增 `_sourceLayer` 成员（`Layer*`，首参数，不允许 nullptr）。两个构造函数新增参数。`splitAnother()` 传播给子 polygon |
| 2 | `Context3DCompositor.h/.cpp` | 构造函数新增 `backgroundContext` 参数。`addImage()` 新增 `sourceLayer` 首参数。新增 `drawBackgroundStyles()` 和 `syncToBackgroundContext()`。`finish()` 中 BSP 遍历改为三步循环：背景样式 → 内容绘制 → 同步到背景 Canvas。DrawOps 仍保持一次性提交 |
| 3 | `Layer3DContext.h/.cpp` | `beginRecording()` 新增 `sourceLayer` 参数。`endRecording()` 将 sourceLayer 传给 `onImageReady()`。`TransformState` 新增 `sourceLayer` 字段 |
| 4 | `Render3DContext.h/.cpp` | 构造时将 `_backgroundContext` 传给 `Context3DCompositor`。`onImageReady()` 将 `sourceLayer` 传给 `_compositor->addImage()`。移除 `finishAndDrawTo()` 中将 3D 合成结果整体写入 backgroundContext 的逻辑（已由 compositor 内部逐步同步取代） |
| 5 | `Layer.cpp` | 保留 `StyleSourceTypesFor3DContext` 禁用不变。`drawChild()` 中将 child 的 Layer 指针传给 `beginRecording()` |
| 6 | `BackgroundBlurTest.cpp` | 添加 3D + BackgroundBlurStyle 的截图测试和单元测试 |

---

## 9. 风险与挑战

### 9.1 BSP 分割导致的碎片化

一个带背景样式的图层可能被 BSP 分割为多个碎片。每个碎片各自独立截取不同区域的背景并处理，在碎片交界处可能有轻微视觉不一致。

**缓解**：实际场景中需要 BSP 分割的图层（与其他图层空间相交）较少。

### 9.2 drawBackgroundStyles 的绘制机制

背景样式的绘制结果需要写入 `_targetColorProxy`（主 render target）。当前 `Context3DCompositor` 通过累积 `_drawOps` 后一次性提交。背景样式的绘制结果需要作为独立的 DrawOps 插入到正确的位置（在当前 polygon 的内容 DrawOps 之前），或通过独立的 `addOpsRenderTask` 提交。各 LayerStyle 子类的 `drawWithExtraSource()` 方法需要适配 3D 合成器的坐标变换和绘制方式。

### 9.3 syncToBackgroundContext 的性能开销

BSP 遍历中每个 polygon 都需要通过 `syncToBackgroundContext()` 将内容绘制到 backgroundContext 的 Canvas 上。这增加了额外的绘制操作。

**缓解**：只有当 3D 子树中存在背景样式时才需要启用同步逻辑。如果没有任何 polygon 有背景样式，可以跳过 `syncToBackgroundContext()` 和 `drawBackgroundStyles()`，回退到现有行为。

### 9.4 Layer 指针的线程安全

Layer 指针在 3D 合成阶段被直接访问。当前 TGFX 采用单线程同步渲染模型（Device mutex），Layer 对象在整个绘制调用栈内不可能被释放或修改。如果未来引入多线程渲染，此处需要改为数据快照机制。

---

## 10. 替代方案对比

| 方案 | 优点 | 缺点 | 适用场景 |
|---|---|---|---|
| **A. Layer* 裸指针 + 合成阶段执行（推荐）** | 改动最小；通用性好，支持所有背景样式；零维护成本 | 碎片交界处的边界问题；syncToBackgroundContext 的性能开销 | 需要 3D + 背景样式且希望最小化改动 |
| B. 提取具体样式参数到数据结构 | 数据自包含，无外部引用 | 每种样式类型需要单独的数据结构；新增样式需扩展 | 需要隔离 Layer 访问时 |
| C. 引入 DrawQuad 中间数据层 | 架构完备，面向多线程扩展 | 改动量极大；每次新增 Layer 属性都要同步改 DrawQuad | 计划引入多线程渲染时 |
| D. 保持禁用 | 零风险、零开发量 | 功能缺失，与 CSS/Chromium 不一致 | 业务上不需要 3D + 背景样式组合 |

---

## 11. 参考资料

### TGFX 源码路径

| 文件 | 路径 |
|---|---|
| DrawPolygon3D | `src/layers/compositing3d/DrawPolygon3D.h/.cpp` |
| Context3DCompositor | `src/layers/compositing3d/Context3DCompositor.h/.cpp` |
| BspTree | `src/layers/compositing3d/BspTree.h/.cpp` |
| Layer3DContext | `src/layers/compositing3d/Layer3DContext.h/.cpp` |
| Render3DContext | `src/layers/compositing3d/Render3DContext.h/.cpp` |
| Layer | `src/layers/Layer.cpp` |
| DrawArgs | `src/layers/DrawArgs.h` |
| BackgroundContext | `src/layers/BackgroundContext.h` |
| BackgroundBlurStyle | `include/tgfx/layers/layerstyles/BackgroundBlurStyle.h` |
| BackgroundBlurStyle (impl) | `src/layers/layerstyles/BackgroundBlurStyle.cpp` |
| LayerStyle | `include/tgfx/layers/layerstyles/LayerStyle.h` |
| RenderTargetProxy | `src/gpu/proxies/RenderTargetProxy.h` |
| TextureProxy | `src/gpu/proxies/TextureProxy.h` |
| TextureImage | `src/core/images/TextureImage.h` |

### Chromium 参考

| 文件 | 职责 |
|---|---|
| `cc/trees/property_tree_builder.cc` | 构建属性树，`ComputeRenderSurfaceReason`、`AddEffectNodeIfNeeded` |
| `cc/trees/draw_property_utils.cc` | 计算绘制属性、RenderSurface 列表、裁剪扩展 |
| `cc/trees/effect_node.h` | EffectNode 数据结构、RenderSurfaceReason 枚举 |
| `cc/trees/transform_node.h` | TransformNode 数据结构、sorting_context_id |
| `cc/layers/render_surface_impl.h` | RenderSurfaceImpl 类、DrawProperties |
| `cc/trees/layer_tree_host_impl.cc` | `CalculateRenderPasses`、渲染通道创建 |
| `viz/service/display/draw_polygon.h/.cc` | DrawPolygon 类、`original_ref_`、`SplitPolygon`、`ToQuads2D`、`TransformToLayerSpace` |
| `viz/service/display/bsp_tree.h/.cc` | BspTree、BspNode、`BuildTree`、`TraverseWithActionHandler`、`GetCameraPositionRelative` |
| `viz/service/display/bsp_walk_action.h/.cc` | BspWalkAction 接口、`BspWalkActionDrawPolygon`（获取逆矩阵 → TransformToLayerSpace → DoDrawPolygon） |
| `viz/service/display/bsp_compare_result.h` | BSP 比较结果枚举（BSP_FRONT、BSP_BACK、BSP_SPLIT、BSP_COPLANAR_*） |
| `viz/service/display/direct_renderer.h/.cc` | `DrawRenderPass`（sorting_context_id 分流）、`FlushPolygons`（BSP 构建 + 遍历）、`DoDrawPolygon`（clip_region 绘制） |
| `viz/service/display/skia_renderer.cc` | DrawRPDQParams（backdrop_filter 参数）、`CalculateRPDQParams`、`PrepareCanvasForRPDQ`（ScaledBackdropLayer）、实际绘制 RenderPassDrawQuad |

### 与 Chromium 架构的映射关系

| Chromium 概念 | TGFX 对应（本方案后） |
|---|---|
| RenderSurface (kBackdropFilter) | DrawPolygon3D + Layer* sourceLayer |
| RenderPass.backdrop_filters | Layer::layerStyles() 中 extraSourceType==Background 的样式 |
| DrawPolygon::original_ref_ (raw_ptr 指向 DrawQuad) | DrawPolygon3D::_sourceLayer (raw_ptr 指向 Layer) |
| sorting_context_id 参与 BSP 排序 | BSP 树自然排序 |
| DirectRenderer::FlushPolygons (BSP 构建 + 遍历) | Context3DCompositor::finish() (BSP 构建 + 遍历) |
| DoDrawPolygon (clip_region 绘制) | drawPolygon() + toQuads() |
| BspWalkActionDrawPolygon (遍历回调) | traverseBackToFront lambda |
| SkiaRenderer::PrepareCanvasForRPDQ (ScaledBackdropLayer) | Context3DCompositor::drawBackgroundStyles() |
| SkiaRenderer 从 canvas 已绘制像素截取 | 从 `_targetColorProxy` 读取纹理 (asTextureProxy + TextureImage::Wrap) |
| content quad + backdrop-filter quad 分离 | polygon 内容绘制 + drawBackgroundStyles() 分离 |
| DrawRenderPass 中 sorting_context_id 分流 | 3D 上下文由 Render3DContext 独立处理（天然分流） |
