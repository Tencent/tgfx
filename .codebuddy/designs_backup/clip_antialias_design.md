# TGFX 裁剪区独立抗锯齿技术方案

## 一、背景

### 1.1 问题描述

main 分支中，裁剪区（clip）的抗锯齿设置完全依赖绘制内容的 `brush.antiAlias`：

```cpp
auto aaType = getAAType(brush);  // AA 来自 brush
auto [clipMask, hasMask] = getClipMaskFP(clip, aaType, &scissorRect);
```

这导致 clip 的抗锯齿无法独立控制。

### 1.2 需求

允许每个裁剪区独立设置抗锯齿，与绘制内容的抗锯齿设置解耦。

### 1.3 设计决策

- **API 风格**：直接扩展 `clipPath/clipRect` 参数
- **ClipOp**：暂只支持 Intersect
- **默认 AA**：`false`（关闭），与 Skia 保持一致

### 1.4 术语定义

- **Intersect**：与已有裁剪区相交，结果是两者的交集
- **Difference**：从已有裁剪区中减去，结果是已有区域减去新区域
- **裁剪区**：需要绘制的区域，裁剪区外的内容不会被绘制

## 二、TGFX 当前实现

### 2.1 数据结构

```cpp
class MCState {
    Matrix matrix = {};
    Path clip = {};  // 直接使用 Path 存储裁剪区，初始为无限大（InverseFillType）
};
```

### 2.2 Clip 组合方式

多次 `clipPath` 调用通过 `Path::addPath` 合并成**单一 Path**，丢失各个 clip 的独立信息：

```cpp
void Canvas::clipPath(const Path& path) {
    clipPath.transform(mcState->matrix);
    mcState->clip.addPath(clipPath, PathOp::Intersect);
}
```

### 2.3 Clip 应用流程

```
Canvas::clipPath(path) → MCState::clip.addPath(合并)
Canvas::drawXXX() → OpsCompositor::addDrawOp → getClipMaskFP(clip, &scissorRect)
  ├─ 矩形且像素对齐 → 仅 scissor 硬裁剪
  ├─ 矩形但浮点 → ClipRectEffect FP + scissor
  └─ 非矩形 → getClipTexture()
        ├─ 复杂路径 → ShapeDrawOp GPU 渲染
        └─ 简单路径 → PathRasterizer CPU 光栅化
```

### 2.4 Clip 处理策略

| 策略             | 适用场景     | 处理方式               |
|------------------|--------------|------------------------|
| **Scissor**      | 像素对齐矩形 | GPU 硬件直接裁剪       |
| **ClipRectEffect** | 浮点矩形     | Shader 数学计算边界    |
| **Texture Mask** | 复杂路径     | 光栅化到纹理，采样 alpha |

### 2.5 Clip 缓存策略

TGFX 采用**单纹理缓存**，Key 为 `Path UniqueKey + AA 标志`：

```cpp
// 伪代码
key = PathRef::GetUniqueKey(clip.path)
if (aaType != None) key = Append(key, AntialiasFlag)
if (key == clipKey) return clipTexture  // 缓存命中
// 否则生成新纹理并更新 clipKey
```

**特点**：仅缓存最近一次的 clip 纹理，不支持边界包含复用。

## 三、Skia 参考设计

### 3.1 核心数据结构

#### 3.1.1 三层架构

```
ClipStack
  ├─ fElements: RawElement 栈    // 所有 clip 元素
  ├─ fSaves: SaveRecord 栈       // save/restore 历史
  └─ fMasks: Mask 栈             // SW Mask 缓存
```

#### 3.1.2 Element 结构

```cpp
struct Element {
    GrShape  fShape;           // 几何形状（rect/rrect/path）
    SkMatrix fLocalToDevice;   // 局部到设备坐标变换
    SkClipOp fOp;              // kIntersect 或 kDifference
    GrAA     fAA;              // 独立的 AA 标志
};
```

#### 3.1.3 RawElement 扩展

继承 Element 并添加管理功能：

```cpp
class RawElement : private Element {
    SkIRect fInnerBounds;        // 完全覆盖的区域
    SkIRect fOuterBounds;        // 最大可能覆盖的区域
    int     fInvalidatedByIndex; // 失效标记（-1 表示有效）
};
```

#### 3.1.4 SaveRecord 结构

```cpp
class SaveRecord {
    int      fStartingElementIndex;  // 该 save 拥有的第一个 element 索引
    int      fOldestValidIndex;      // 最老的有效 element 索引
    SkIRect  fInnerBounds;           // 累积的内边界
    SkIRect  fOuterBounds;           // 累积的外边界
    SkClipOp fStackOp;               // 整体操作类型
    ClipState fState;                // 裁剪状态枚举
    uint32_t fGenID;                 // 版本号，用于缓存
};
```

**fOldestValidIndex**：范围 `[fOldestValidIndex, count-1]` 内的 element 对当前 save 有效。

#### 3.1.5 ClipState 枚举

| 状态          | 判定条件       | apply() 处理         |
|---------------|----------------|----------------------|
| kEmpty        | 元素交集为空   | 直接返回 kClippedOut |
| kWideOpen     | 无元素         | 直接返回 kUnclipped  |
| kClipped      | 有裁剪元素     | 完整流程处理         |

### 3.2 Element 合并策略

采用**两层过滤**：先与 SaveRecord 聚合状态比较（快速剪枝），再与具体 element 逐一比较。

#### 3.2.1 合并调用链

```
Canvas::clipPath(path, op, aa)
  → ClipStack::clip(RawElement)
    → SaveRecord::addElement(toAdd)
        ├─ 第一层: get_clip_geometry(saveRecord, toAdd)
        │    ├─ kEmpty → 整体变空
        │    ├─ kAOnly → 新 element 冗余，丢弃
        │    ├─ kBOnly → 新 element 替换整个栈
        │    └─ kBoth → 进入第二层
        └─ 第二层: appendElement(toAdd)
             遍历 [fOldestValidIndex, count-1]
             对每个调用 existing.updateForElement(&toAdd)
```

#### 3.2.2 updateForElement 几何判断

`updateForElement` 根据几何关系修改 existing 和 toAdd 的 invalid 状态：

| ClipGeometry | existing    | toAdd       | 处理方式                               |
|--------------|-------------|-------------|----------------------------------------|
| kEmpty       | markInvalid | markInvalid | ClipState 变为 kEmpty                  |
| kAOnly       | 不变        | markInvalid | toAdd 冗余，不添加                     |
| kBOnly       | markInvalid | 不变        | existing 冗余，更新 fOldestValidIndex  |
| kBoth        | 可能 markInvalid | 可能被修改 | 尝试 combine（见下表），失败则共存     |

**kBoth 时的 combine 策略**（将 existing 合并到 toAdd 中）：

| 可合并条件                   | 合并方式                             |
|------------------------------|--------------------------------------|
| 都是 Intersect + 轴对齐矩形  | 矩形相交（像素对齐时可忽略 AA 差异） |
| 都是 RRect + 同变换 + 同 AA  | RRect 相交                           |
| 其他情况                     | 无法合并，共存                       |

#### 3.2.3 appendElement 元素有效性管理

`SaveRecord::appendElement` 负责管理当前 SaveRecord 内元素的有效性，核心是**遍历现有元素、更新状态、决定存储位置**。

**遍历阶段**：从新到老遍历 `[fOldestValidIndex, count-1]` 范围内的元素：

```cpp
// 伪代码
for (existing in elements.reverse()) {
    if (i < fOldestValidIndex) break;
    
    existing.updateForElement(&toAdd, *this);  // 几何判断，可能修改双方 invalid 状态
    
    if (toAdd.isInvalid()) {
        if (existing.isInvalid()) {
            fState = kEmpty;  // 两者都 invalid → clip 为空
            return true;
        }
        return false;  // toAdd 被 existing 包含，冗余，不添加
    }
    
    if (existing.isInvalid()) {
        // existing 被 toAdd 淘汰
        if (i >= fStartingElementIndex) {
            // 属于当前 SaveRecord，槽位可复用
            oldestActiveInvalid = &existing;
            oldestActiveInvalidIndex = i;
        }
    } else {
        // 两者都有效，共存
        oldestValid = i;
        youngestValid = max(youngestValid, i);
    }
    --i;
}
```

**后处理阶段**：更新状态并决定 toAdd 的存储位置：

```cpp
// 伪代码
// 1. 更新遍历起点
fOldestValidIndex = min(oldestValid, oldestActiveInvalidIndex);

// 2. 更新 ClipState
fState = (oldestValid == elements.count()) ? toAdd.clipType() : kComplex;

// 3. 裁剪栈尾无效元素
targetCount = youngestValid + 1;
if (!oldestActiveInvalid || oldestActiveInvalidIndex >= targetCount) {
    targetCount++;  // toAdd 需要新槽位
    oldestActiveInvalid = nullptr;
}
while (elements.count() > targetCount) {
    elements.pop_back();  // 删除尾部无效元素
}

// 4. 存储 toAdd
if (oldestActiveInvalid) {
    *oldestActiveInvalid = toAdd;  // 复用 invalid 槽位
} else {
    elements.push_back(toAdd);     // 新增槽位
}

// 5. 更新版本号，使缓存失效
fGenID = next_gen_id();
```

#### 3.2.4 两个索引与 Active 元素

| 索引                   | 含义                                                             |
|------------------------|------------------------------------------------------------------|
| `fOldestValidIndex`    | **整个 Element 栈**中最老的有效元素索引，可能位于之前的 SaveRecord |
| `fStartingElementIndex` | **当前 SaveRecord** 开始时的元素索引                              |

```
Elements Stack:
索引:    [0]      [1]      [2]      [3]      [4]
        ←── SaveRecord 0 ──→ ←── SaveRecord 1 (当前) ──→
        Non-Active           Active (活跃)
                             ↑
                     fStartingElementIndex = 2
        ↑
  fOldestValidIndex = 0

遍历范围: [fOldestValidIndex, count-1] = [0, 4]
Active 元素: [fStartingElementIndex, count-1] = [2, 4]
```

**Active 元素**：属于当前 SaveRecord 的元素（`i >= fStartingElementIndex`），可以被修改或复用槽位。Non-Active 元素属于之前的 SaveRecord，restore() 时需要恢复，不能复用其槽位。

**appendElement 中的变量作用域**：

| 变量                 | 范围                   | 用途                             |
|----------------------|------------------------|----------------------------------|
| `youngestValid`      | 仅 Active 区域（隐含） | 决定可以 pop 哪些 Active 元素    |
| `oldestValid`        | 跨所有 SaveRecord      | 更新 fOldestValidIndex（遍历边界） |
| `oldestActiveInvalid` | 仅 Active 区域（显式） | 槽位复用                         |

#### 3.2.5 fOldestValidIndex 的更新

**取 min 的原因**：

| 情况                   | oldestValid | oldestActiveInvalidIndex | 取 min 原因                |
|------------------------|-------------|--------------------------|----------------------------|
| invalid 在 valid 之后  | 2           | 3                        | min=2，正常                |
| invalid 在 valid 之前  | 3           | 2                        | min=2，确保复用槽位不被跳过 |

如果新元素存入 `oldestActiveInvalidIndex` 的槽位，下次遍历必须能访问到它。取 min 保证无论 invalid 槽位在哪，下次遍历都能覆盖。

### 3.3 Element 失效与恢复

`fInvalidatedByIndex` 记录使该 element 失效的 element 索引（-1 表示有效）。

**失效**：当 `updateForElement` 判定 existing 被 toAdd 完全包含（kBOnly）时，existing 被标记为 invalid，记录 `fInvalidatedByIndex = toAdd 的索引`。

**恢复**：`restore()` 删除当前 SaveRecord 的元素后，遍历之前 SaveRecord 的 element，如果其 `fInvalidatedByIndex >= 被删除元素的起始索引`，说明使其失效的元素已被删除，恢复为有效（原始 AA 值不变）。

```cpp
// 伪代码
void SaveRecord::restore(elements) {
    // 删除当前 SaveRecord 的元素
    while (elements.count() > fStartingElementIndex) {
        elements.pop_back();
    }
    // 恢复之前被 invalidate 的元素
    for (e in elements) {
        if (e.fInvalidatedByIndex >= fStartingElementIndex) {
            e.fInvalidatedByIndex = -1;  // 恢复有效
        }
    }
}
```

### 3.4 apply() 完整流程

```cpp
GrClip::Effect ClipStack::apply(GrRecordingContext* rContext, 
                                SurfaceDrawContext* sdc, 
                                GrDrawOp* op, 
                                GrAAType aa, 
                                GrAppliedClip* out, 
                                SkRect* bounds);  // bounds: Op 的绘制边界（in-out 参数）
```

**bounds 参数**：
- **输入**：Op 原始的绘制范围（`op->bounds()`）
- **处理中**：与 deviceBounds、clip elements 的 outerBounds 求交，不断收紧
- **输出**：被 clip 裁剪后的最终绘制范围，用于 `op->setClippedBounds(bounds)`

**setClippedBounds 的作用**：更新 Op 的 bounds 元数据，用于 Op 合并（Batching）优化。收紧后的 bounds 让更多 Op 之间"不重叠"，增加重排序和合并机会，减少 draw call。

#### 3.4.1 阶段 0：预处理

```cpp
// 创建 Draw 对象，代表"要被裁剪的 Op"
Draw draw(*bounds, aa);
if (!draw.applyDeviceBounds(fDeviceBounds)) return kClippedOut;  // 完全在屏幕外
bounds->intersect(deviceBounds);  // 第一次收缩

// 快速判断 ClipState
if (cs.state() == kEmpty) return kClippedOut;
if (cs.state() == kWideOpen) return kUnclipped;
```

#### 3.4.2 阶段 1：整体几何判断

```cpp
switch (get_clip_geometry(cs, draw)) {
    case kEmpty:  return kClippedOut;    // clip 和 draw 无交集
    case kBOnly:  return kUnclipped;     // draw 完全在 clip 内
    case kBoth:   break;                 // 需要详细处理
}
```

#### 3.4.3 阶段 2：遍历所有元素

```cpp
for (const RawElement& e : fElements.ritems()) {
    if (e.isInvalid()) continue;
    
    // 单个 Element vs Draw 的几何判断
    switch (get_clip_geometry(e, draw)) {
        case kEmpty:  return kClippedOut;
        case kBOnly:  continue;  // draw 完全在 element 内，该 element 不影响
        case kBoth:   break;     // 需要应用此 element
    }
    
    bool fullyApplied = false;
    
    // 尝试 1: Op 内部处理（clipToShape）
    auto result = op->clipToShape(sdc, e.op(), e.localToDevice(), e.shape(), e.aa());
    if (result == kClippedGeometrically) {
        bounds->intersect(e.outerBounds());  // 同步收紧 bounds
        fullyApplied = true;
    }
    // 尝试 2: 硬件方法（Scissor / Window Rectangles）
    if (!fullyApplied && e.isPixelAlignedRect()) {
        out->hardClip().addScissor(e.rect());
        fullyApplied = true;
    }
    if (!fullyApplied && e.op() == kDifference && windowRectsSupported) {
        out->hardClip().addWindowRectangle(e.rect());
        fullyApplied = true;
    }
    // 尝试 3/4: Analytic FP 或 Atlas FP（共享 4 个额度）
    if (!fullyApplied && remainingAnalyticFPs > 0) {
        std::tie(fullyApplied, clipFP) = analytic_clip_fp(e, ...);
        if (!fullyApplied && atlasPathRenderer) {
            std::tie(fullyApplied, clipFP) = clip_atlas_fp(e, ...);
        }
        if (fullyApplied) remainingAnalyticFPs--;  // 无论哪个成功都消耗额度
    }
    
    // 收集需要 Mask 处理的元素
    if (!fullyApplied) {
        elementsForMask.push_back(&e);
        maskRequiresAA |= (e.aa() == kYes);
    }
}
```

**clipToShape vs setClippedBounds**：

| 操作                     | 层级       | 目的                                 |
|--------------------------|------------|--------------------------------------|
| `op->clipToShape()`      | 几何层面   | Op 内部修改自己的顶点数据以适应 clip |
| `op->setClippedBounds()` | 元数据层面 | 更新 bounds 记录，用于合并/重排序决策   |

两者不冗余：clipToShape 是可选优化（部分 Op 支持），setClippedBounds 是必须操作（记录所有 clip 的综合效果）。

#### 3.4.4 阶段 3：Mask 处理

```cpp
if (!elementsForMask.empty()) {
    // Stencil 不支持 AA（numSamples <= 1）但需要 AA，或 Stencil 不可用
    if ((numSamples <= 1 && maskRequiresAA) || stencilUnavailable) {
        GetSWMaskFP(...);  // CPU 光栅化
    } else {
        render_stencil_mask(...);  // GPU 渲染
    }
}
```

#### 3.4.5 阶段 4：填充 GrAppliedClip

```cpp
out->hardClip().addScissor(scissorBounds, bounds);
out->hardClip().addWindowRectangles(windowRects, kExclusive);
out->addCoverageFP(std::move(clipFP));
```

### 3.5 各种 Clip 处理方法

#### 3.5.1 方法对比

| 方法 | 适用类型 | AA 支持 | 原理 | 性能 |
|:-----|:---------|:-------:|:-----|:----:|
| **Scissor** | 像素对齐矩形 | 否 | 硬件直接支持 | 最高 |
| **Window Rects** | Difference 矩形 | 否 | 硬件排除区域 | 高 |
| **Analytic FP** | AA 简单形状 | 是 | Shader 数学计算 | 较高 |
| **Atlas FP** | AA 复杂路径 | 是 | GPU tessellation | 中 |
| **Stencil Mask** | 非 AA 任意形状 | 否 | GPU 渲染到 stencil | 中 |
| **SW Mask** | AA 复杂路径（兜底） | 是 | CPU 光栅化 | 低 |

#### 3.5.2 各 Element 的 AA 独立生效

在 `apply()` 中，各 element 的 AA 是**独立生效**的，不会被合并成统一设置：

| 处理路径        | AA 处理方式                                   |
|-----------------|----------------------------------------------|
| **Analytic FP** | 根据 `e.fAA` 选择 edgeType (kFillAA/kFillBW) |
| **Atlas FP**    | 检查 `e.fAA == kYes`，非 AA 拒绝              |
| **SW Mask**     | 每个 element 调用 `drawShape(..., e.fAA)`    |
| **Stencil Mask** | 每个 element 调用 `helper.drawShape(..., e.fAA)` |

**maskRequiresAA 的作用**：仅决定使用 SW Mask 还是 Stencil，不影响各 element 的 AA 独立性。

### 3.6 Analytic FP

#### 3.6.1 数量限制

Analytic FP 和 Atlas FP **共享 4 个额度**（优先尝试 Analytic，失败再尝试 Atlas）：

| 限制原因          | 说明                                     |
|-------------------|------------------------------------------|
| **Shader 复杂度** | 每个 FP 插入计算代码，过多导致 shader 过重 |
| **Uniform 配额** | 每个 FP 需要 uniform 传递几何参数，GPU 有上限 |
| **收益递减**      | 98% 绘制只需 1-2 个 clip                  |
| **替代方案**      | 超出配额合并渲染到 Mask 纹理             |

#### 3.6.2 ClipRectEffect 数学原理（有向距离场）

```glsl
// 输入: Rect = vec4(left, top, right, bottom)，实际传入时 outset 0.5 像素

// 1. 计算到四条边的有向距离，clamp 到 [0,1]
vec4 dists4 = clamp(vec4(1,1,-1,-1) * (fragCoord.xyxy - Rect), 0, 1);

// 2. 合并对边: 内部=1，边缘=0~1，外部=0
vec2 dists2 = dists4.xy + dists4.zw - 1;

// 3. X/Y 方向覆盖率相乘
float coverage = dists2.x * dists2.y;
```

| 像素位置          | dists4        | dists2      | coverage |
|--------------------|---------------|-------------|----------|
| 矩形内部          | (1,1,1,1)     | (1,1)       | 1.0      |
| 左边缘 50%        | (0.5,1,1,1)   | (0.5,1)     | 0.5      |
| 右下角 50%×50%    | (1,1,0.5,0.5) | (0.5,0.5)   | 0.25     |
| 矩形外部          | (0,1,1,1)     | (0,1)       | 0.0      |

**0.5 像素 outset 的作用**：将 1 像素宽的 AA 过渡带**居中**在原始矩形边界上。原始边界处 coverage=0.5，向内/外各延伸 0.5 像素到 1.0/0.0。

#### 3.6.3 多个 FP 的组合机制

多个 FP 通过 `GrBlendFragmentProcessor` 的 `kModulate` 模式**嵌套组合**：

```
第一次: clipFP = rect1FP
第二次: clipFP = blend(rect2FP, rect1FP)
第三次: clipFP = blend(rect3FP, blend(rect2FP, rect1FP))
```

生成的 shader 结构（伪代码）：

```glsl
half4 rect1_main() { return coverage1; }
half4 rect2_main() { return coverage2; }
half4 rect3_main() { return coverage3; }

half4 blend_inner() { return rect2_main() * rect1_main(); }
half4 blend_outer() { return rect3_main() * blend_inner(); }

void main() {
    gl_FragColor = inputColor * blend_outer();
}
```

**FP 树结构**：

```
       BlendFP (kModulate)
       /              \
  rect3FP         BlendFP (kModulate)
                   /          \
              rect2FP      rect1FP
```

N 个 Rect clip ≈ N × 4 行 shader 代码 + N-1 个 blend 操作。

#### 3.6.4 FP 生成逻辑

```cpp
// 伪代码
GrFPResult analytic_clip_fp(const Element& e, fp) {
    edgeType = e.fAA ? kFillAA : kFillBW;
    
    if (e.isAxisAlignedRect())
        return Rect(fp, edgeType, e.rect());
    if (e.isRRect())
        return RRectEffect::Make(fp, edgeType, e.rrect());
    if (e.isConvexPolygon())
        return ConvexPolyEffect::Make(fp, edgeType, e.path());
    
    return GrFPFailure(fp);
}
```

### 3.7 Atlas FP（仅 AA）

Atlas Path Renderer 使用 **GPU tessellation** 将路径渲染到共享纹理图集。

**为什么仅支持 AA**：

| 场景           | 推荐方案               | 原因                             |
|----------------|------------------------|----------------------------------|
| 非 AA 简单形状 | Scissor / Window Rects | 硬件直接支持                   |
| 非 AA 复杂路径 | **Stencil Mask**       | GPU 渲染，1-bit 存储，无需纹理采样 |
| AA 复杂路径   | **Atlas / SW Mask**    | 需要多级 coverage，必须用纹理     |

**Stencil vs Atlas 对比**：

| 方面 | Stencil              | Atlas                    |
|------|----------------------|--------------------------|
| 存储 | 1-bit（在/不在）     | 8-bit（0~255 coverage）   |
| 边缘 | 锐利（锯齿）         | 平滑（抗锯齿）             |
| 采样 | 无需纹理采样       | 需要纹理采样             |
| 适用 | 非 AA               | AA                       |

Tessellation 在三角形边缘自然产生**亚像素级 coverage 变化**，非常适合 AA。非 AA 用 Stencil 的 1-bit 存储更高效。

### 3.8 Mask 缓存策略

#### 3.8.1 SW Mask 缓存

Key = `genID + bounds`，支持**边界包含复用**：

```cpp
// 伪代码：查找可复用的 mask
for (mask in masks.reverse()) {
    if (mask.genID != current.genID) break;  // genID 不同则停止
    if (mask.bounds.contains(drawBounds)) {  // bounds 必须覆盖 draw
        proxy = findCachedProxy(mask.key);
        if (proxy) return proxy;
    }
}
// 未命中则渲染新 mask 并注册缓存
```

**为什么同时检查 genID 和 bounds**：
- **genID 相同**：clip 几何形状相同
- **bounds 不同**：mask 覆盖的屏幕区域不同

同一 clip 状态（genID 相同）下可能有多个不同 bounds 的 mask：

```
Clip 状态: 一个复杂 path (genID = 42)

Draw 1: bounds = (0, 0, 100, 100)     → Mask A: genID=42, bounds=(0,0,100,100)
Draw 2: bounds = (200, 200, 400, 400) → Mask B: genID=42, bounds=(200,200,400,400)
Draw 3: bounds = (50, 50, 80, 80)     → 复用 Mask A（bounds 被包含）
```

每个 element 使用自己的 AA 设置独立绘制：

```cpp
// 伪代码
void draw_to_sw_mask(helper, element) {
    helper.drawShape(e.shape, e.matrix, e.fAA, alpha);  // AA 在这里生效
}
```

#### 3.8.2 Stencil Mask 缓存

缓存存储在 OpsTask 中，Key = `genID + bounds + numAnalyticFPs`：

```cpp
// 伪代码
bool mustRenderClip(genID, bounds, numFPs) {
    return opsTask.lastGenID != genID ||
           !opsTask.lastBounds.contains(bounds) ||
           opsTask.lastNumFPs != numFPs;
}
```

支持边界包含复用，但生命周期随 OpsTask。

#### 3.8.3 genID 与缓存失效

```cpp
// 特殊值
kInvalidGenID  = 0;  // 未初始化
kEmptyGenID    = 1;  // 空裁剪
kWideOpenGenID = 2;  // 全开裁剪

// 添加或替换 element 时更新
fGenID = next_gen_id();
```

**invalidateMasks 触发时机**（genID 变化后由调用方主动触发）：

| 时机                 | 触发条件                                     |
|----------------------|----------------------------------------------|
| restore()            | pop SaveRecord 时，清空该 record 的所有 masks |
| replaceClip()        | 替换 clip 时，清空当前 record 的 masks        |
| clip() 添加新元素    | 修改了活跃的 SaveRecord（genID 变化）          |

```cpp
// invalidateMasks 实现：只清空当前 SaveRecord 的 masks
void SaveRecord::invalidateMasks(proxyProvider, masks) {
    while (masks->count() > fStartingMaskIndex) {
        masks->back().invalidate(proxyProvider);  // 从 GPU 缓存中移除
        masks->pop_back();
    }
}
```

| 场景            | SW Mask                    | Stencil              |
|-----------------|----------------------------|----------------------|
| clip 元素变化   | genID 变化，旧 mask 不匹配   | genID 变化，触发重新渲染 |
| restore()       | 调用 `invalidateMasks()` 主动删除 | genID 变化           |
| OpsTask 结束   | 纹理由 ProxyProvider 管理   | 缓存字段重置         |

#### 3.8.4 缓存策略对比

| 特性           | TGFX               | Skia SW Mask               | Skia Stencil               |
|----------------|--------------------|----------------------------|----------------------------|
| **Key 构成**   | Path UniqueKey + AA | genID + bounds             | genID + bounds + numFPs    |
| **缓存粒度**   | 单个纹理           | 栈式多级                     | 单个                       |
| **边界复用**   | 不支持             | 大 mask 复用于小 draw       | 支持                       |
| **失效时机**   | Key 变化            | genID 变化 / restore        | OpsTask 结束               |

**为什么 SW Mask 用多级缓存而 Stencil 用单级**：SW Mask 的 CPU 光栅化成本高，值得多级缓存；Stencil 的 GPU 渲染快，单级足够。

## 四、TGFX 与 Skia 实现对比

| 方面             | TGFX                 | Skia                               |
|------------------|----------------------|------------------------------------|
| **Clip 存储**    | 单一 Path            | ClipStack（元素列表）               |
| **AA 存储**      | 全局 `forceAntiAlias` | 每个 element 独立 `fAA`           |
| **元素合并**     | Path::addPath 合并   | 尝试 combine，失败保持独立           |
| **save/restore** | 直接复制 MCState      | SaveRecord 栈 + element 失效/恢复 |
| **预处理优化**   | 无                   | ClipState 快速判断                 |
| **硬件裁剪**     | Scissor              | Scissor + Window Rectangles        |
| **Analytic FP**  | AARectEffect         | rect/rrect/convex poly             |
| **复杂路径**     | GPU 或 CPU 光栅      | SW Mask / Stencil / Atlas          |
| **Mask 缓存**   | 单一 clipTexture     | 多层缓存（genID + bounds）         |

## 五、TGFX 重构方案

### 5.1 设计目标

1. **AA 独立性**：每个 clip element 的抗锯齿设置独立生效
2. **API 简洁**：扩展现有 `clipRect/clipPath` 接口，新增 `antiAlias` 参数
3. **渐进式重构**：尽量复用现有代码，减少改动范围
4. **仅支持 Intersect**：不支持 Difference 操作，简化实现

### 5.2 独立存储 Element 的优势

相比于将所有 clip 合并为单一 Path，独立存储每个 ClipElement 的核心价值是 **提前剪枝**：

1. **跳过不影响绘制的 element**：如果某个 element 的 bound 完全包含 drawBounds，可直接跳过该 element 的处理
2. **逐个选择最优策略**：矩形用 Scissor/ClipRectEffect，只有复杂路径才走 Mask
3. **支持未来扩展**：便于添加 Difference 操作、Window Rectangles、Atlas FP 等高级特性

```cpp
for (element : elements) {
    if (element.bound.contains(drawBounds)) {
        continue;  // 跳过，该 element 不影响此次绘制
    }
    if (element.isRect) {
        applyRectEffect();  // 轻量方案
    } else {
        collectForMask();   // 只有必要时才走 Mask
    }
}
```

### 5.3 Canvas API

```cpp
class Canvas {
 public:
    /**
     * Replaces the clip with the intersection of the clip and the specified rectangle.
     * @param rect The rectangle to intersect with the current clip.
     * @param antiAlias If true, the clip edge will be anti-aliased.
     */
    void clipRect(const Rect& rect, bool antiAlias = false);

    /**
     * Replaces the clip with the intersection of the clip and the specified path.
     * @param path The path to intersect with the current clip.
     * @param antiAlias If true, the clip edge will be anti-aliased.
     */
    void clipPath(const Path& path, bool antiAlias = false);
};
```

**API 迁移说明**：

现有 `setForceClipAntialias(bool)` API 将被废弃。迁移方式：

| 旧 API | 新 API |
|--------|--------|
| `canvas->setForceClipAntialias(true); canvas->clipPath(path);` | `canvas->clipPath(path, true);` |
| `canvas->setForceClipAntialias(false); canvas->clipRect(rect);` | `canvas->clipRect(rect, false);` |

- 旧 API 的 `forceAntiAlias` 默认为 `true`，新 API 的 `antiAlias` 默认为 `false`（与 Skia 一致）
- 迁移时需注意默认值变化，显式传入 `antiAlias` 参数以保持原有行为

### 5.4 核心数据结构

#### 5.4.1 ClipElement

存储单个 clip 的几何信息和 AA 设置：

```cpp
class ClipElement {
    Path path;          // 几何形状（统一用 Path 存储）
    bool antiAlias;     // 独立的 AA 标志
    Rect bound;         // 边界缓存
    bool isRect;        // 是否为矩形（用于优化）
    
    // Index that invalidated this element. -1 means valid.
    int invalidatedByIndex = -1;
    
    bool isValid() const { return invalidatedByIndex < 0; }
    
    // 判断边界是否像素对齐（整数坐标）
    bool isPixelAligned() const {
        return bound.left == floorf(bound.left) &&
               bound.top == floorf(bound.top) &&
               bound.right == floorf(bound.right) &&
               bound.bottom == floorf(bound.bottom);
    }
};
```

#### 5.4.2 ClipRecord

管理单次 save 层级的裁剪状态，作为 ClipStack 内部的记录结构：

```cpp
struct ClipRecord {
    size_t           startIndex;       // 该 save 层级拥有的第一个 element 索引
    size_t           oldestValidIndex; // 遍历起点，整个 Element 栈的全局最老有效索引
    Rect             bound;            // 所有有效 element 的累积边界
    ClipState        state;            // 当前裁剪状态
    uint32_t         uniqueID;         // 版本号，用于缓存失效判断
    
    // 默认构造：WideOpen 状态，无限大边界，uniqueID 使用全局递增 ID
    ClipRecord()
        : startIndex(0)
        , oldestValidIndex(0)
        , bound(Rect::MakeLTRB(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX))
        , state(ClipState::WideOpen)
        , uniqueID(nextUniqueID()) {}  // 使用全局递增 ID，避免与缓存 key 0 冲突
};
```

**Active 与 Non-Active 元素**：

- **Active 元素**：`i >= startIndex`，属于当前 save 层级，槽位可复用
- **Non-Active 元素**：`i < startIndex`，属于之前 save 层级，只能标记 invalid，不能复用槽位

这样设计确保 restore() 时能正确恢复历史状态。

#### 5.4.3 ClipStack

独立管理裁剪状态，封装 element 合并、save/restore 等逻辑：

```cpp
class ClipStack {
public:
    ClipStack() {
        records.emplace_back();  // 初始化时 push 一个默认 ClipRecord（WideOpen 状态）
    }
    
    void clip(const Path& path, bool antiAlias);
    void save();
    void restore();
    
    // 查询接口，供 apply() 使用
    ClipState state() const { return current().state; }
    const Rect& bound() const { return current().bound; }
    const std::vector<ClipElement>& elements() const { return _elements; }
    size_t oldestValidIndex() const { return current().oldestValidIndex; }
    uint32_t uniqueID() const { return current().uniqueID; }
    
    // 回放接口，用于替换内部状态
    void reset(const ClipRecord& record, const std::vector<ClipElement>& elements);

private:
    void addElement(const ClipElement& toAdd);
    void replaceWithElement(const ClipElement& toAdd);
    void appendElement(const ClipElement& toAdd);
    
    const ClipRecord& current() const {
        DEBUG_ASSERT(!records.empty());
        return records.back();
    }
    
    ClipRecord& current() {
        DEBUG_ASSERT(!records.empty());
        return records.back();
    }
    
    std::vector<ClipElement> _elements;
    std::vector<ClipRecord> records;  // 永不为空，至少有一个默认 ClipRecord
};
```

#### 5.4.4 Canvas 结构

```cpp
class Canvas {
    std::vector<Matrix> matrixStack_;  // 永不为空，初始化时 push 单位矩阵
    ClipStack           clipStack_;
    
    const Matrix& matrix() const { return matrixStack_.back(); }
    Matrix& matrix() { return matrixStack_.back(); }
    
    void clipRect(const Rect& rect, bool antiAlias) {
        auto transformedRect = matrix().mapRect(rect);
        clipStack_.clip(Path::Rect(transformedRect), antiAlias);
    }
    
    void clipPath(const Path& path, bool antiAlias) {
        auto transformedPath = path;
        transformedPath.transform(matrix());
        clipStack_.clip(transformedPath, antiAlias);
    }
    
    void save() {
        matrixStack_.push_back(matrix());
        clipStack_.save();
    }
    
    void restore() {
        matrixStack_.pop_back();
        clipStack_.restore();
    }
    
    void translate(float dx, float dy) {
        matrix().preTranslate(dx, dy);
    }
    
    void drawRect(const Rect& rect, const Paint& paint) {
        drawContext_->drawRect(rect, matrix(), clipStack_, paint.getBrush(), paint.getStroke());
    }
};
```

#### 5.4.5 ClipState 快速判断

```cpp
enum class ClipState {
    Empty,     // 裁剪区为空，所有绘制都被裁掉
    WideOpen,  // 无裁剪，所有绘制都可见
    Rect,      // 单个矩形
    Complex    // 多个元素或非矩形，需要遍历处理
};
```

### 5.5 Element 合并策略

采用简化版的两层过滤，仅处理 Intersect 操作。

#### 5.5.1 ClipStack::clip() 入口

```cpp
void ClipStack::clip(const Path& path, bool antiAlias) {
    ClipElement toAdd{path, antiAlias, path.getBounds(), path.isRect()};
    addElement(toAdd);
}
```

#### 5.5.2 ClipStack::addElement() 第一层过滤

```cpp
void ClipStack::addElement(const ClipElement& toAdd) {
    auto& cur = current();
    
    // 快速判断：已经是空，无需继续
    if (cur.state == ClipState::Empty) {
        return;
    }
    
    // 空元素 → 整体变空
    if (toAdd.bound.isEmpty()) {
        cur.state = ClipState::Empty;
        return;
    }
    
    // 第一层：与整体边界快速比较
    if (!toAdd.bound.intersects(cur.bound)) {
        cur.state = ClipState::Empty;
        return;
    }
    if (toAdd.bound.contains(cur.bound) && toAdd.isRect) {
        return;  // toAdd 完全包含当前裁剪区，冗余
    }
    
    // WideOpen 特殊处理：直接用新元素初始化
    if (cur.state == ClipState::WideOpen) {
        replaceWithElement(toAdd);
        return;
    }
    
    // 第二层：与各 element 逐一比较
    appendElement(toAdd);
}
```

#### 5.5.3 ClipStack::replaceWithElement() 替换整个栈

```cpp
void ClipStack::replaceWithElement(const ClipElement& toAdd) {
    auto& cur = current();
    
    // 清空当前 ClipRecord 的 Active 元素
    _elements.resize(cur.startIndex);
    
    // 添加新元素
    _elements.push_back(toAdd);
    cur.bound = toAdd.bound;
    cur.state = toAdd.isRect ? ClipState::Rect : ClipState::Complex;
    cur.oldestValidIndex = _elements.size() - 1;
    cur.uniqueID = nextUniqueID();
}
```

#### 5.5.4 UpdateElements() 更新元素状态（全局函数）

```cpp
static inline void UpdateElements(ClipElement& existing, ClipElement& toAdd,
                                  const ClipRecord& record) {
    DEBUG_ASSERT(toAdd.isValid());
    
    // existing 已失效，不影响 toAdd
    if (!existing.isValid()) {
        return;
    }
    
    auto geo = ResolveClipGeometry(existing, toAdd);
    
    if (geo == ClipGeometry::Empty) {
        existing.invalidatedByIndex = record.startIndex;
        toAdd.invalidatedByIndex = record.startIndex;
    } else if (geo == ClipGeometry::AOnly) {
        // A 包含 B，裁剪结果 = Intersect(A, B) = B，A 冗余可淘汰
        existing.invalidatedByIndex = record.startIndex;
    } else if (geo == ClipGeometry::BOnly) {
        // B 包含 A，裁剪结果 = Intersect(A, B) = A，B 冗余可淘汰
        toAdd.invalidatedByIndex = record.startIndex;
    } else if (toAdd.tryCombine(existing)) {
        existing.invalidatedByIndex = record.startIndex;
    }
}
```

#### 5.5.5 ClipStack::appendElement() 第二层过滤

```cpp
void ClipStack::appendElement(ClipElement toAdd) {
    auto& cur = current();
    size_t oldestInvalidIdx = _elements.size();    // 可复用槽位索引
    ClipElement* oldestInvalid = nullptr;          // 可复用槽位指针
    size_t oldestValidIdx = _elements.size();      // 最老有效索引
    // 最新有效索引，使用有符号类型避免下溢问题
    // 初始值为 startIndex - 1，当遍历没找到有效元素时 targetEndIdx = startIndex
    int64_t youngestValidIdx = static_cast<int64_t>(cur.startIndex) - 1;
    
    // 从新到老遍历 [oldestValidIndex, count-1] 范围
    for (size_t i = _elements.size(); i > cur.oldestValidIndex; ) {
        --i;
        auto& existing = _elements[i];
        
        UpdateElements(existing, toAdd, cur);
        
        if (!toAdd.isValid()) {
            if (!existing.isValid()) {
                // 两者都失效，整体变空
                cur.state = ClipState::Empty;
                return;
            } else {
                // toAdd 冗余，丢弃
                return;
            }
        } else if (!existing.isValid()) {
            // existing 被淘汰
            if (i >= cur.startIndex) {
                // Active 元素，记录可复用槽位
                oldestInvalid = &existing;
                oldestInvalidIdx = i;
            }
        } else {
            // 两者都有效
            oldestValidIdx = std::min(oldestValidIdx, i);
            youngestValidIdx = std::max(youngestValidIdx, static_cast<int64_t>(i));
        }
    }
    
    // 计算目标结束索引
    size_t targetEndIdx = static_cast<size_t>(youngestValidIdx + 1);
    if (!oldestInvalid || oldestInvalidIdx >= targetEndIdx) {
        // toAdd 需要新槽位（追加到末尾）
        targetEndIdx++;
        oldestInvalid = nullptr;
    }
    
    // 更新 ClipState（必须在 resize 之前，此时 size 还是原始值）
    cur.state = (oldestValidIdx == _elements.size() && toAdd.isRect)
        ? ClipState::Rect
        : ClipState::Complex;
    
    // 清理尾部无效元素
    _elements.resize(targetEndIdx);
    
    // 存储 toAdd
    if (oldestInvalid) {
        *oldestInvalid = toAdd;
    } else {
        _elements.back() = toAdd;  // resize 已分配空间，直接赋值
    }
    
    // 更新其他状态
    cur.oldestValidIndex = std::min(oldestValidIdx, oldestInvalidIdx);
    cur.bound = Rect::Intersect(cur.bound, toAdd.bound);
    cur.uniqueID = nextUniqueID();
}
```

**uniqueID 生成逻辑**（全局原子递增）：

```cpp
uint32_t nextUniqueID() {
    static std::atomic<uint32_t> nextID{1};
    return nextID.fetch_add(1, std::memory_order_relaxed);
}
```

**Active 槽位复用**：只有 `i >= clip.startIndex` 的 Active 元素槽位可以复用。Non-Active 元素属于之前的 save 层级，restore() 时需要恢复，不能复用其槽位。

#### 5.5.6 ResolveClipGeometry() 几何关系判断（全局函数）

```cpp
enum class ClipGeometry { Empty, AOnly, BOnly, Both };

static inline ClipGeometry ResolveClipGeometry(const ClipElement& a, const ClipElement& b) {
    auto boundsA = a.bound;
    auto boundsB = b.bound;
    
    // bounds 不相交 → 结果为空
    if (!boundsA.intersects(boundsB)) {
        return ClipGeometry::Empty;
    }
    
    // 双方都是矩形 → 快速 bounds 判断
    if (a.isRect && b.isRect) {
        if (boundsA.contains(boundsB)) {
            return ClipGeometry::AOnly;
        }
        if (boundsB.contains(boundsA)) {
            return ClipGeometry::BOnly;
        }
        return ClipGeometry::Both;
    }
    
    // 双方都是 Path → 无法精确判断
    if (!a.isRect && !b.isRect) {
        return ClipGeometry::Both;
    }
    
    // 一方是 Path，一方是 Rect → 用 Path.contains(Rect) 判断
    if (a.isRect) {
        // a 是 Rect，b 是 Path
        if (b.path.contains(boundsA)) {
            return ClipGeometry::BOnly;  // a 在 b 内部，Intersect = a，b 可淘汰
        }
    } else {
        // a 是 Path，b 是 Rect
        if (a.path.contains(boundsB)) {
            return ClipGeometry::AOnly;  // b 在 a 内部，Intersect = b，a 可淘汰
        }
    }
    
    return ClipGeometry::Both;
}
```

**设计原则**：保守处理，宁可多保留元素（影响性能），也不能错误淘汰（影响正确性）。利用 `Path::contains(Rect)` 可以在一方是 Path、一方是 Rect 时精确判断包含关系。

#### 5.5.7 ClipElement::tryCombine() 合并逻辑

```cpp
// 尝试将 other 合并到自身，成功则 other 可被淘汰
bool ClipElement::tryCombine(const ClipElement& other) {
    // 只合并矩形。Path 的合并需要在 CPU 进行布尔运算，成本较高，
    // 保留两个元素，延迟到 GPU 生成蒙版时处理。
    if (!isRect || !other.isRect) return false;
    if (antiAlias != other.antiAlias) return false;
    
    // 合并为矩形交集
    auto combined = Rect::Intersect(bound, other.bound);
    if (combined.isEmpty()) return false;
    
    bound = combined;
    path = Path::Rect(combined);
    return true;
}
```

### 5.6 ClipStack::save() 和 restore()

```cpp
void ClipStack::save() {
    auto cur = current();  // 拷贝当前状态
    cur.startIndex = _elements.size();  // 新层级从当前元素末尾开始
    records.push_back(cur);
}

void ClipStack::restore() {
    auto& cur = current();
    auto startIndex = static_cast<int>(cur.startIndex);
    // 先弹出 Active 元素
    _elements.resize(cur.startIndex);
    // 再恢复被当前层级标记失效的 Non-Active 元素
    for (auto& element : _elements) {
        if (element.invalidatedByIndex >= startIndex) {
            element.invalidatedByIndex = -1;
        }
    }
    // 恢复状态（pop 后 current() 自动指向之前保存的状态，uniqueID 也随之恢复）
    records.pop_back();
}
```

### 5.7 AppliedClip 结构

```cpp
enum class AppliedClipStatus { ClippedOut, Unclipped, Clipped };

struct AppliedClip {
    AppliedClipStatus  status = AppliedClipStatus::Unclipped;
    Rect               scissor = Rect::MakeLTRB(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);  // 初始无限大
    PlacementPtr<FragmentProcessor> coverageFP = nullptr;  // 覆盖率 FP（AA 或 Mask）
    
    void addScissor(const Rect& rect) {
        scissor = Rect::Intersect(scissor, rect);
    }
    
    // 判断是否有有效的 scissor 裁剪
    bool hasScissor() const {
        return scissor.left > -FLT_MAX || scissor.top > -FLT_MAX ||
               scissor.right < FLT_MAX || scissor.bottom < FLT_MAX;
    }
};
```

### 5.8 applyClip() 流程

绘制时在 OpsCompositor 中应用裁剪，接收 ClipStack 引用，返回裁剪结果：

```cpp
AppliedClip applyClip(BlockAllocator* allocator, const ClipStack& clipStack) {
    AppliedClip out;
    
    // 阶段 0: 快速判断
    if (clipStack.state() == ClipState::Empty) {
        out.status = AppliedClipStatus::ClippedOut;
        return out;
    }
    if (clipStack.state() == ClipState::WideOpen) {
        out.status = AppliedClipStatus::Unclipped;
        return out;
    }
    
    // 后续优化 - 整体边界判断
    // 传入 drawBounds 参数，判断 drawBounds 与 clipStack.bound() 是否相交，
    // 不相交则直接返回 ClippedOut，避免遍历 elements。
    
    // 阶段 1: 遍历有效 elements
    auto& elements = clipStack.elements();
    std::vector<const ClipElement*> elementsForMask;
    PlacementPtr<FragmentProcessor> clipFP = nullptr;
    
    for (size_t i = clipStack.oldestValidIndex(); i < elements.size(); ++i) {
        auto& element = elements[i];
        if (!element.isValid()) continue;
        
        // 后续优化 - 单元素边界判断
        // 判断 element.bound 与 drawBounds 的关系：
        // - 不相交：返回 ClippedOut
        // - element 完全包含 drawBounds：跳过该 element，不影响此次绘制
        
        // 尝试应用
        bool applied = false;
        
        // 尝试 1: Scissor（仅像素对齐矩形）
        // 像素对齐的矩形可以直接用硬件 Scissor 裁剪，无论 AA 设置如何
        if (element.isRect && element.isPixelAligned()) {
            out.addScissor(element.bound);
            applied = true;
        }
        
        // 尝试 2: Analytic FP（非像素对齐矩形）
        // 非像素对齐的矩形需要用 AARectEffect，根据 antiAlias 设置选择 AA 或硬边缘模式
        if (!applied && element.isRect) {
            clipFP = makeAnalyticFP(allocator, element, std::move(clipFP));
            applied = true;
        }
        
        // 尝试 3: 收集需要 Mask 的 element
        if (!applied) {
            elementsForMask.push_back(&element);
        }
    }
    
    // 阶段 2: 生成 Mask
    if (!elementsForMask.empty()) {
        clipFP = getClipMaskFP(allocator, elementsForMask, clipStack.uniqueID(), 
                               clipStack.bound(), std::move(clipFP));
    }
    
    out.coverageFP = std::move(clipFP);
    out.status = AppliedClipStatus::Clipped;
    return out;
}
```

### 5.9 AARectEffect 扩展

扩展现有的 `AARectEffect`，增加 `antiAlias` 参数支持非 AA 模式：

```cpp
class AARectEffect : public FragmentProcessor {
public:
    // 修改：新增 antiAlias 参数，默认 true 保持向后兼容
    static PlacementPtr<AARectEffect> Make(BlockAllocator* allocator,
                                            const Rect& rect,
                                            bool antiAlias = true);
    
protected:
    Rect rect;
    bool antiAlias;  // 新增
    
    AARectEffect(const Rect& rect, bool antiAlias)
        : FragmentProcessor(ClassID()), rect(rect), antiAlias(antiAlias) {}
};

class GLSLAARectEffect : public AARectEffect {
    void emitCode(EmitArgs& args) const override {
        auto fragBuilder = args.fragBuilder;
        auto uniformHandler = args.uniformHandler;
        
        auto rectName = uniformHandler->addUniform("Rect", UniformFormat::Float4,
                                                   ShaderStage::Fragment);
        
        if (antiAlias) {
            // AA: 计算到边缘的距离，平滑过渡（现有实现）
            fragBuilder->codeAppendf(
                "vec4 dists4 = clamp(vec4(1.0, 1.0, -1.0, -1.0) * "
                "vec4(gl_FragCoord.xyxy - %s), 0.0, 1.0);", rectName.c_str());
            fragBuilder->codeAppend("vec2 dists2 = dists4.xy + dists4.zw - 1.0;");
            fragBuilder->codeAppend("float coverage = dists2.x * dists2.y;");
        } else {
            // Non-AA: 硬边缘（新增）
            fragBuilder->codeAppendf(
                "float coverage = all(greaterThanEqual(gl_FragCoord.xy, %s.xy)) && "
                "all(lessThanEqual(gl_FragCoord.xy, %s.zw)) ? 1.0 : 0.0;",
                rectName.c_str(), rectName.c_str());
        }
        
        fragBuilder->codeAppendf("%s = %s * coverage;",
                                 args.outputColor.c_str(),
                                 args.inputColor.c_str());
    }
    
    void onSetData(UniformData*, UniformData* fragmentUniformData) const override {
        // AA 模式需要 outset 0.5 像素
        auto outRect = antiAlias ? rect.makeOutset(0.5f, 0.5f) : rect;
        fragmentUniformData->setData("Rect", outRect);
    }
    
    void onComputeProcessorKey(BytesKey* bytesKey) const override {
        bytesKey->write(static_cast<uint32_t>(antiAlias ? 1 : 0));
    }
};
```

### 5.10 makeAnalyticFP 函数

在 OpsCompositor 中统一处理 analytic clip FP 的生成和合并：

```cpp
PlacementPtr<FragmentProcessor> makeAnalyticFP(
    BlockAllocator* allocator,
    const ClipElement& element,
    PlacementPtr<FragmentProcessor> inputFP) {
    
    PlacementPtr<FragmentProcessor> clipFP = nullptr;
    
    if (element.isRect) {
        clipFP = AARectEffect::Make(allocator, element.bound, element.antiAlias);
    }
    // 后续可扩展：RRectEffect、ConvexPathEffect 等
    
    if (!clipFP) {
        return inputFP;  // 无法 analytic，返回原 FP
    }
    
    if (!inputFP) {
        return clipFP;
    }
    
    // 用 Compose 合并：clipFP(inputFP(x))
    return FragmentProcessor::Compose(allocator, std::move(clipFP), std::move(inputFP));
}
```

**设计说明**：
- AARectEffect 扩展后同时支持 AA 和非 AA 模式
- makeAnalyticFP 统一处理 FP 合并逻辑
- 使用 `Compose` 实现串行组合：`clipFP(inputFP(x))`

### 5.11 Mask 生成策略

```cpp
// OpsCompositor 已有成员变量，复用
uint32_t clipKey = 0;
std::shared_ptr<TextureProxy> clipTexture;

PlacementPtr<FragmentProcessor> getClipMaskFP(
    BlockAllocator* allocator,
    const std::vector<const ClipElement*>& elements,
    uint32_t uniqueID,
    const Rect& clipBound,
    PlacementPtr<FragmentProcessor> inputFP) {
    // 缓存命中
    if (uniqueID == clipKey && clipTexture) {
        return makeMaskFP(allocator, clipTexture, clipBound, std::move(inputFP));
    }
    
    // 缓存未命中，生成新 Mask
    auto bounds = clipBound;
    bounds.roundOut();
    
    // 所有 elements 渲染到同一张纹理，每个 element 使用自己的 antiAlias 设置
    clipTexture = makeClipTexture(elements, bounds);
    clipKey = uniqueID;
    
    return makeMaskFP(allocator, clipTexture, clipBound, std::move(inputFP));
}
```

**设计说明**：
- 复用已有的 `clipKey` 和 `clipTexture` 成员变量进行缓存
- 缓存基于 `record.uniqueID`，同一 clip 状态下多次绘制可复用
- 所有 elements 渲染到同一张纹理，每个 element 独立使用自己的 `antiAlias` 设置
- 不按 AA 分组，避免生成多张纹理的额外开销

#### 5.11.1 makeClipTexture 实现

```cpp
std::shared_ptr<TextureProxy> makeClipTexture(
    const std::vector<const ClipElement*>& elements,
    const Rect& bounds) {
    
    auto width = FloatSaturateToInt(bounds.width());
    auto height = FloatSaturateToInt(bounds.height());
    auto rasterizeMatrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
    
    // 创建目标 RenderTarget，初始清除为白色（alpha=1，表示全通过）
    auto clipRenderTarget = RenderTargetProxy::Make(context, width, height, true, 1, false,
                                                    ImageOrigin::TopLeft, BackingFit::Approx);
    DEBUG_ASSERT(clipRenderTarget != nullptr);
    
    // 收集所有 DrawOp
    std::vector<PlacementPtr<DrawOp>> drawOps;
    
    for (auto element : elements) {
        auto aaType = element->antiAlias ? AAType::Coverage : AAType::None;
        
        if (PathTriangulator::ShouldTriangulatePath(element->path)) {
            // GPU 渲染：复杂路径用 ShapeDrawOp
            auto clipBounds = Rect::MakeWH(width, height);
            auto shape = Shape::MakeFrom(element->path);
            shape = Shape::ApplyMatrix(std::move(shape), rasterizeMatrix);
            auto shapeProxy = proxyProvider()->createGPUShapeProxy(shape, aaType, 
                                                                    clipBounds, renderFlags);
            auto uvMatrix = Matrix::MakeTrans(bounds.left, bounds.top);
            auto drawOp = ShapeDrawOp::Make(std::move(shapeProxy), {}, uvMatrix, aaType);
            // 使用 Multiply 混合，实现多个 clip 的交集
            drawOp->setBlendMode(BlendMode::Multiply);
            drawOps.push_back(std::move(drawOp));
        } else {
            // CPU 光栅化：简单路径用 PathRasterizer
            auto rasterizer = PathRasterizer::MakeFrom(width, height, element->path,
                                                        element->antiAlias, &rasterizeMatrix);
            auto texture = proxyProvider()->createTextureProxy(rasterizer, false, renderFlags);
            // 创建 FillRectOp 绘制纹理，使用 Multiply 混合
            auto fillOp = FillRectOp::Make(..., BlendMode::Multiply);
            drawOps.push_back(std::move(fillOp));
        }
    }
    
    // 提交所有 drawOps 到 clipRenderTarget，初始清除为白色
    context->drawingManager()->addOpsRenderTask(std::move(clipRenderTarget), 
                                                 std::move(drawOps),
                                                 PMColor::White());  // 清除为白色
    
    return clipRenderTarget->asTextureProxy();
}
```

**设计说明**：
- 复用现有的 `ShouldTriangulatePath` 判断，决定走 GPU 还是 CPU 路径
- **初始清除为白色**（alpha=1），表示初始状态全部通过
- **使用 Multiply 混合**，实现多个 clip 的交集：`result = clip1 * clip2 * ...`
- GPU 路径：复杂路径使用 `ShapeDrawOp` 渲染，适合大面积或复杂形状
- CPU 路径：简单路径使用 `PathRasterizer` 光栅化，适合小面积或简单形状
- 每个 element 独立判断，独立使用自己的 `antiAlias` 设置

#### 5.11.2 makeMaskFP 实现

使用现有的 `DeviceSpaceTextureEffect` 和 `MulInputByChildAlpha` 包装：

```cpp
PlacementPtr<FragmentProcessor> makeMaskFP(
    BlockAllocator* allocator,
    std::shared_ptr<TextureProxy> maskTexture,
    const Rect& bounds,
    PlacementPtr<FragmentProcessor> inputFP) {
    // 计算纹理采样的 UV 矩阵
    auto uvMatrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
    if (renderTarget->origin() == ImageOrigin::BottomLeft) {
        uvMatrix.preConcat(renderTarget->getOriginTransform());
    }
    
    // 创建 DeviceSpaceTextureEffect 采样 mask 纹理
    auto maskFP = DeviceSpaceTextureEffect::Make(allocator, std::move(maskTexture), uvMatrix);
    // 用 MulInputByChildAlpha 包装，使 mask 的 alpha 值作为覆盖率
    maskFP = FragmentProcessor::MulInputByChildAlpha(allocator, std::move(maskFP));
    
    if (!inputFP) {
        return maskFP;
    }
    
    // 用 Compose 合并：maskFP(inputFP(x))
    return FragmentProcessor::Compose(allocator, std::move(maskFP), std::move(inputFP));
}
```

### 5.12 缓存策略

复用 OpsCompositor 已有的 `clipKey` 和 `clipTexture` 成员变量实现单纹理缓存：

```cpp
class OpsCompositor {
    // 已有成员变量，复用
    uint32_t clipKey = 0;
    std::shared_ptr<TextureProxy> clipTexture;
};
```

**缓存逻辑**：
- 命中条件：`record.uniqueID == clipKey && clipTexture != nullptr`
- 命中时直接复用 `clipTexture`，无需重新生成
- 未命中时生成新纹理并更新 `clipKey` 和 `clipTexture`

**缓存失效**：

| 操作                | 缓存处理                                 |
|---------------------|------------------------------------------|
| clip() 添加新元素   | uniqueID 变化，下次 applyClip 时自动重新生成 |
| save()              | uniqueID 不变，缓存保留                  |
| restore()           | uniqueID 变化，下次 applyClip 时自动重新生成 |

**uniqueID 更新时机**：

| 操作           | uniqueID 变化 | 说明                           |
|----------------|--------------|--------------------------------|
| clip() 添加    | 是           | 裁剪状态改变，生成新 ID        |
| save()         | 否           | 仅保存状态，裁剪区不变         |
| restore()      | 是（恢复）   | 恢复到之前保存的 uniqueID      |

### 5.13 与现有代码的集成

#### 5.13.1 DrawContext 接口改造

```cpp
// 改造前
class DrawContext {
    virtual void drawRect(const Rect& rect, const MCState& state, 
                          const Brush& brush, const Stroke* stroke) = 0;
    virtual void drawPath(const Path& path, const MCState& state, 
                          const Brush& brush) = 0;
    virtual void drawShape(std::shared_ptr<Shape> shape, const MCState& state, 
                           const Brush& brush, const Stroke* stroke) = 0;
    // ... 其他绘制方法
};

// 改造后
class DrawContext {
    virtual void drawRect(const Rect& rect, const Matrix& matrix,
                          const ClipStack& clip,
                          const Brush& brush, const Stroke* stroke) = 0;
    virtual void drawPath(const Path& path, const Matrix& matrix,
                          const ClipStack& clip,
                          const Brush& brush) = 0;
    virtual void drawShape(std::shared_ptr<Shape> shape, const Matrix& matrix,
                           const ClipStack& clip,
                           const Brush& brush, const Stroke* stroke) = 0;
    // ... 其他绘制方法同理
};
```

**说明**：
- `MCState` 拆分为独立的 `Matrix` 和 `ClipStack` 参数
- `ClipStack` 引用包含完整的裁剪信息：`elements`、`current()` 的 `state`/`bound`/`uniqueID` 等
- 接口更清晰，clip 相关信息从一个参数获取

#### 5.13.2 MCState 改造

```cpp
// 改造前
class MCState {
    Matrix matrix = {};
    Path   clip = {};
};

// 改造后：MCState 不再需要，Matrix 和 ClipStack 独立传递
// Canvas 内部分别维护 matrixStack_ 和 clipStack_
```

#### 5.13.3 Canvas 改造

Canvas 内部结构改为独立的 `matrixStack_` 和 `clipStack_`，详见 5.4.4 Canvas 结构。

主要改动：
- 移除 `MCState`，改为独立的矩阵栈和裁剪栈
- `save()`/`restore()` 分别调用两者的对应方法
- `clipRect`/`clipPath` 委托给 `clipStack_` 处理
- 绘制方法传递 `matrix()` 和 `clipStack_` 给 DrawContext
- 废弃 `setForceClipAntialias()` 和 `getForceClipAntialias()` API

#### 5.13.4 OpsCompositor 改造

```cpp
void OpsCompositor::addDrawOp(PlacementPtr<DrawOp> op, const ClipStack& clipStack) {
    auto allocator = context->drawingAllocator();
    
    // 应用裁剪
    auto appliedClip = applyClip(allocator, clipStack);
    
    if (appliedClip.status == AppliedClipStatus::ClippedOut) {
        return;  // 完全被裁剪，不绘制
    }
    
    if (appliedClip.status == AppliedClipStatus::Clipped) {
        // 设置 Scissor
        if (appliedClip.hasScissor()) {
            op->setScissorRect(appliedClip.scissor);
        }
        // 添加覆盖率 FP
        if (appliedClip.coverageFP) {
            op->addCoverageFP(std::move(appliedClip.coverageFP));
        }
    }
    
    // 继续原有的 maskFilter、blendMode 等处理...
    drawOps.emplace_back(std::move(op));
}
```

**说明**：原有的 `getClipMaskFP` 逻辑被 `applyClip` 替代，返回的 `AppliedClip` 包含 scissor 和 coverageFP 两部分。

### 5.14 PictureContext 录制回放

#### 5.14.1 数据结构

```cpp
class SetClip : public PictureRecord {
    ClipRecord record;                  // 裁剪状态快照
    std::vector<ClipElement> elements;  // 有效元素列表（深拷贝）
};

class PictureContext : public DrawContext {
    BlockAllocator blockAllocator;
    std::vector<PlacementPtr<PictureRecord>> records;
    uint32_t lastClipUniqueID = 0;
    // ...
};

class PlaybackContext {
    ClipStack clipStack_;
    Matrix matrix_;
    // ...
    
    const ClipStack& clipStack() const { return clipStack_; }
};
```

#### 5.14.2 录制阶段

```cpp
void PictureContext::recordClip(const ClipStack& clip) {
    auto& record = clip.current();
    if (lastClipUniqueID == record.uniqueID) return;
    
    // 只拷贝有效元素
    std::vector<ClipElement> validElements;
    auto& elements = clip.elements();
    for (size_t i = record.oldestValidIndex; i < elements.size(); ++i) {
        if (elements[i].isValid()) {
            validElements.push_back(elements[i]);
        }
    }
    
    // 修正 ClipRecord 索引
    ClipRecord snapshotRecord = record;
    snapshotRecord.startIndex = 0;
    snapshotRecord.oldestValidIndex = 0;
    
    auto setClip = blockAllocator.make<SetClip>(snapshotRecord, std::move(validElements));
    records.emplace_back(std::move(setClip));
    lastClipUniqueID = record.uniqueID;
}

void PictureContext::drawRect(const Rect& rect, const Matrix& matrix,
                              const ClipStack& clip,
                              const Brush& brush, const Stroke* stroke) {
    recordClip(clip);
    recordMatrix(matrix);
    recordBrush(brush);
    auto record = blockAllocator.make<DrawRectRecord>(rect, stroke);
    records.emplace_back(std::move(record));
}
```

#### 5.14.3 回放阶段

```cpp
// ClipStack 新增 reset 方法，用于回放时替换内部状态
void ClipStack::reset(const ClipRecord& record, const std::vector<ClipElement>& elements) {
    records.clear();
    records.push_back(record);
    _elements = elements;
}

void SetClip::playback(DrawContext* context, PlaybackContext* playback) {
    playback->clipStack_.reset(record, elements);
}

void DrawRectRecord::playback(DrawContext* context, PlaybackContext* playback) {
    context->drawRect(rect, playback->matrix(), playback->clipStack(),
                      playback->brush(), playback->stroke());
}
```

#### 5.14.4 性能特点

| 操作 | 频率 | 成本 |
|------|------|------|
| recordClip | 低频（clip 变化时） | 拷贝有效元素 |
| drawRect 录制 | 高频 | uniqueID 比较，无拷贝 |
| setClip 回放 | 低频 | 拷贝 elements 到 clipStack_ |
| drawRect 回放 | 高频 | 传递引用，零拷贝 |

### 5.15 其他 DrawContext 子类适配

| 子类 | 用途 | 适配方式 |
|------|------|----------|
| **RenderContext** | GPU 渲染 | 核心改造，接收 ClipStack 调用 applyClip() |
| **PictureContext** | 录制命令 | 记录 ClipRecord 快照和有效 elements |
| **PlaybackContext** | 回放命令 | 通过 ClipStack::reset() 恢复状态 |
| **MeasureContext** | 计算边界 | 使用 ClipStack.bound() 裁剪边界 |
| **LayerUnrollContext** | 代理转发 | 透传 ClipStack 给下层 DrawContext |
| **PDFExportContext** | 导出 PDF | 遍历 ClipStack.elements() 输出多个 clip path |
| **SVGExportContext** | 导出 SVG | 遍历 ClipStack.elements() 输出 `<clipPath>` 元素 |
| **OpaqueContext** | 图层样式 | 透传 ClipStack |
| **MaskContext** | 遮罩处理 | 透传 ClipStack |

**说明**：大多数子类只需修改接口签名以接收 `const ClipStack&` 参数，实际处理逻辑改动较小。

## 六、测试计划

基于现有 `TGFX_TEST(CanvasTest, clipAntiAlias)` 用例扩展，用少量测试用例覆盖核心逻辑。

### 6.1 截图测试

#### 6.1.1 ClipAntiAlias（主测试用例）

扩展现有用例，在一个 Surface 上展示所有关键场景：

```cpp
TGFX_TEST(CanvasTest, ClipAntiAlias) {
  // 创建 400x300 的 Surface，分为 4 列展示不同场景
  // 每列 100px 宽，展示不同的 clip + AA 组合
  
  // 列 1: 矩形 clip（像素对齐 + 非像素对齐）
  // - 上半部分：像素对齐矩形 + AA（走 Scissor，AA 无效果）
  // - 下半部分：非像素对齐矩形 + AA（走 ClipRectEffect，边缘平滑）
  
  // 列 2: 矩形 clip（AA vs 非 AA 对比）
  // - 上半部分：非像素对齐矩形 + AA（边缘平滑）
  // - 下半部分：非像素对齐矩形 + 非 AA（边缘硬边）
  
  // 列 3: 路径 clip（AA vs 非 AA 对比）
  // - 上半部分：三角形 + AA（边缘平滑）
  // - 下半部分：三角形 + 非 AA（边缘锯齿）
  
  // 列 4: 混合 clip（多个 clip 叠加）
  // - AA 矩形 + 非 AA 圆形叠加，验证各自边缘独立渲染
}
```

**覆盖的逻辑**：
- Scissor 路径（像素对齐矩形）
- AARectEffect 路径（非像素对齐矩形）
- Mask 路径（路径 clip）
- AA 独立性（同一绘制中不同 clip 的 AA 设置独立生效）

#### 6.1.2 ClipSaveRestore（save/restore 测试）

```cpp
TGFX_TEST(CanvasTest, ClipSaveRestore) {
  // 验证 save/restore 后裁剪状态正确恢复
  // - save() 后添加新 clip
  // - restore() 后新 clip 失效，旧 clip 恢复
  // - 多层嵌套 save/restore
}
```

**覆盖的逻辑**：
- save() 更新 startIndex
- restore() 恢复 invalidatedByIndex
- 多层嵌套状态正确恢复

### 6.2 单元测试

直接放在 CanvasTest 中，通过 Canvas API 间接测试 ClipStack 逻辑：

```cpp
TGFX_TEST(CanvasTest, ClipElementMerge) {
  // 测试元素合并和淘汰逻辑（通过绘制结果验证）
  // - 添加包含关系的矩形，验证合并生效
  // - 添加不相交的矩形，验证绘制被完全裁剪
}

TGFX_TEST(CanvasTest, ClipStateTransition) {
  // 测试 ClipState 状态转换（通过绘制结果验证）
  // - WideOpen -> Rect -> Complex -> Empty
}

TGFX_TEST(CanvasTest, ClipRecordPlayback) {
  // 测试录制回放中的 clip 状态
  // - 录制时 clip 元素被正确拷贝到 Picture
  // - 回放时 clip 状态正确恢复并生效
}
```

**覆盖的逻辑**：
- addElement 第一层过滤
- appendElement 第二层过滤和槽位复用
- ResolveClipGeometry 几何关系判断
- tryCombine 矩形合并
- save/restore 状态管理（由 6.1.2 覆盖）
- 录制回放 clip 状态拷贝与恢复
- 蒙版缓存命中与失效（通过 uniqueID 判断）

### 6.3 边界情况

在单元测试中覆盖：

| 场景 | 测试方式 |
|------|----------|
| Empty 状态 | 添加不相交的 clip，验证后续绘制跳过 |
| WideOpen 状态 | 初始状态验证，无 clip 时绘制不受影响 |
| 空矩形 clip | `clipRect(Rect::MakeEmpty())` 导致 Empty |
| 缓存命中 | 相同 uniqueID 下多次绘制，验证不重复生成 Mask |

## 七、实现步骤

### 阶段 1：核心数据结构

| 任务                          | 文件                     | 说明                                     |
|-------------------------------|--------------------------|------------------------------------------|
| 定义 ClipElement 结构         | ClipStack.h              | path, antiAlias, bound, isRect, invalidatedByIndex |
| 定义 ClipRecord 结构          | ClipStack.h              | startIndex, oldestValidIndex, bound, state, uniqueID |
| 定义 ClipState 枚举           | ClipStack.h              | Empty, WideOpen, Rect, Complex           |
| 定义 ClipStack 类             | ClipStack.h              | elements_, recordStack_, current_        |
| 定义 ClipGeometry 枚举        | ClipStack.cpp            | Empty, AOnly, BOnly, Both                |
| 定义 AppliedClip 结构         | OpsCompositor.h          | status, scissor, coverageFP              |
| 改造 DrawContext 接口         | DrawContext.h            | 所有绘制方法新增 ClipStack 参数          |

### 阶段 2：Element 合并逻辑

| 任务                          | 文件                     | 说明                                     |
|-------------------------------|--------------------------|------------------------------------------|
| 实现 ResolveClipGeometry()    | ClipStack.cpp            | 几何关系判断                             |
| 实现 ClipElement::tryCombine()| ClipElement.cpp          | 同 AA 矩形合并                           |
| 实现 ClipStack::addElement()  | ClipStack.cpp            | 第一层过滤                               |
| 实现 ClipStack::replaceWithElement() | ClipStack.cpp     | 替换整个栈                               |
| 实现 ClipStack::appendElement() | ClipStack.cpp          | 第二层过滤，Active 槽位复用，更新 oldestValidIndex |

### 阶段 3：save/restore 逻辑

| 任务                          | 文件                     | 说明                                     |
|-------------------------------|--------------------------|------------------------------------------|
| 实现 ClipStack::save()        | ClipStack.cpp            | 更新 startIndex，push recordStack        |
| 实现 ClipStack::restore()     | ClipStack.cpp            | 恢复 invalidatedByIndex 元素，pop recordStack |

### 阶段 4：Canvas 改造

| 任务                          | 文件                     | 说明                                     |
|-------------------------------|--------------------------|------------------------------------------|
| 添加 ClipStack 成员           | Canvas.h                 | 替代原 MCState 中的 clip                 |
| 添加 matrixStack_ 成员        | Canvas.h                 | 替代原 stateStack                        |
| 改造 Canvas::save()           | Canvas.cpp               | matrixStack_.push + clipStack_.save()    |
| 改造 Canvas::restore()        | Canvas.cpp               | matrixStack_.pop + clipStack_.restore()  |
| 实现 Canvas::clipRect()       | Canvas.cpp               | 变换后调用 clipStack_.clip()             |
| 实现 Canvas::clipPath()       | Canvas.cpp               | 变换后调用 clipStack_.clip()             |
| 废弃旧 API                    | Canvas.h/cpp             | setForceClipAntialias/getForceClipAntialias 标记 deprecated |

### 阶段 5：apply() 应用逻辑

| 任务                          | 文件                     | 说明                                     |
|-------------------------------|--------------------------|------------------------------------------|
| 实现 applyClip()              | OpsCompositor.cpp        | 接收 ClipStack，从 oldestValidIndex 遍历 |
| 实现 Scissor 应用             | OpsCompositor.cpp        | 像素对齐或非 AA 矩形                     |
| 扩展 AARectEffect             | AARectEffect.h, GLSLAARectEffect.cpp | 新增 antiAlias 参数，支持非 AA 模式 |
| 实现 makeAnalyticFP()         | OpsCompositor.cpp        | 创建并合并 Analytic FP                   |
| 实现 Mask 生成调度            | OpsCompositor.cpp        | 收集需要 Mask 的 elements                |

### 阶段 6：Mask 生成和缓存

| 任务                          | 文件                     | 说明                                     |
|-------------------------------|--------------------------|------------------------------------------|
| 实现 getClipMaskFP()          | OpsCompositor.cpp        | Mask 生成与缓存，复用 clipKey/clipTexture |
| 实现 makeClipTexture()        | OpsCompositor.cpp        | 渲染 elements 到纹理                     |
| 实现 makeMaskFP()             | OpsCompositor.cpp        | 使用 DeviceSpaceTextureEffect + MulInputByChildAlpha |

### 阶段 7：DrawContext 子类集成

| 任务                          | 文件                     | 说明                                     |
|-------------------------------|--------------------------|------------------------------------------|
| 改造 RenderContext            | RenderContext.cpp        | 接收 ClipStack，调用 applyClip()         |
| 实现 ClipStack::reset()       | ClipStack.cpp            | 用于回放时替换内部状态                   |
| 改造 SetClip 记录类型         | PictureRecords.h         | 存储 ClipRecord 快照和有效 elements 列表 |
| 改造 PictureContext           | PictureContext.cpp       | recordClip() 记录修正后的快照            |
| 改造 PlaybackContext          | PlaybackContext.cpp      | setClip() 调用 reset()，clipStack() 返回引用 |
| 改造其他 DrawContext 子类     | 各文件                   | MeasureContext、PDFExportContext 等      |

### 阶段 8：测试

详见第六章测试计划。

## 八、参考资料

- Skia ClipStack: `skia/src/gpu/ganesh/ClipStack.h/cpp`
- Skia SkCanvas: `skia/include/core/SkCanvas.h`
- TGFX Canvas: `include/tgfx/core/Canvas.h`
- TGFX OpsCompositor: `src/gpu/OpsCompositor.cpp`
