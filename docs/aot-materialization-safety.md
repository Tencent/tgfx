# AOT 物化安全边界

依据：代码 `file:line` + 全量实测（Metal / OpenGL 双后端）。本文记录物化（把 FP 子树渲进离屏纹理再采样）的**真实约束**、**两个已修根因**、**九个已排除假设**，以及**正确的差异度量方法**。

---

## 0. 为什么需要这份文档

物化是 L2 分解执行器的地基：它让「不可匹配的复杂子树」拆成「可匹配的简单 kernel」，从而在**零变体增量**下提升 AOT 命中率。实测收益：

```
precompiled  1460 → 1673   (+213)
冷 AOT 命中率 82.86% → 88.99%
编译变体      735 → 735    (零增量)
```

但物化**不是语义中性**的。九轮假设-验证才定位到真正的两个根因，中间反复撞墙。这份文档的目的是让后续接手的人不重走这些弯路。

---

## 1. 度量方法：必须用 α 加权，不能用 maxDiff

**这是最重要的一条。** 对 premultiplied 图像，`maxDiff`（逐通道最大差）在低 α 区域会被 `1/α` 放大成无意义的大数。

实测例子（`LayerFilterTest/Filters`）按 α 分档：

| α 档位 | 像素数 | maxRGB差 | **合成后可见差**（差 × α/255） |
|---|---|---|---|
| α<16 | 559 | **255** | **1.0** ← 最吓人的，实际影响最小 |
| α16-64 | 807 | 31 | 3.0 |
| α64-128 | 1289 | 12 | 4.1 |
| α≥128 | 3997 | 11 | **8.3** ← 最大实际影响 |

**正确度量**：
```
visibleDiff = max over pixels of
    max( |A.rgb × A.a/255 − B.rgb × B.a/255| , |A.a − B.a| )
```

**判定阈值**：≤8 为亚视觉（人眼阈值约 2-5/255，PSNR 通常 >50dB）；>20 需逐个排查。

用 `maxDiff` 会把可接受的差异误判为功能失效，从而放弃整个可行方案。

**另两个度量陷阱**：
- **不要用测试名猜截图文件名** —— 从日志解析 `Baseline::Compare(surface, "KEY")` 的实际 key（`ScalePictureImage` 的 key 是 `pic_scaled_*`）
- **必须确认测的是本次输出** —— 检查文件 mtime。曾把 `ComplexSVG` 测成 1.6（实际 237），因为读了过期文件

---

## 2. 两个已修根因

### 根因 A：采样越界（已修，apron=1）

**机制**：`FPArgs::drawRect` 表达「这次绘制覆盖哪些像素」，**不是**「下游会采样哪些像素」。物化用 `drawRect` 尺寸建 Exact 纹理（`FPFlattenHelper.h` 的 `RenderTargetProxy::Make` + `roundOut()`），下游若采到界外，`TextureEffect` 会 clamp 到边缘纹素 —— **透明边界变不透明**。

**实测证据**：`drawRect = (0,0,110,110)` 恰好等于图像尺寸、零余量；而坏点精确落在图像边界外 1 像素（x=29/169，图像在 [30,140]/[170,280]），`base=(0,0,0,0)` → `new=(159,151,153,128)`。

**修法**：`MaterializationDecision::apronRadius = 1.0f`，`FlattenToTexture` 按此 `outset` 后再 `roundOut`。

**修掉**：`FilterTest.InnerShadow`(128)、`LayerFilterTest.InnerShadowStyle`(64)、`LayerTest.StrokeOnTop`、`FilterTest.ClipInnerShadowImageFilter`(43→2)。

**apron=1/2/4 实测结果完全相同** → 1 是精确需求，不是安全余量。更大的 apron 无额外收益。

### 根因 B：嵌套光栅化的双重量化（已修，NestedRasterization 标志）

**机制**：`PictureImage` 会把整个 picture 光栅化进一张 RGBA8 纹理（`PictureImage.cpp` 的 `RenderTargetProxy::Make`）。在这个 pass **内部**再物化子树，产物立刻被外层吞掉 —— 对外层那次 draw 毫无 AOT 收益，只多一次 8-bit 量化。而 `makeScaled` 会在每个分辨率重新光栅化，量化误差被缩放插值放大 30~90 倍。

**实测证据**：`ScalePictureImage` 一次运行触发 **12 次物化**（9 次同尺寸 366×517 + 3 次 276×427），耗时 74ms → 522ms（**7 倍**）。

**修法**：`InternalRenderFlags::NestedRasterization`（`AOTMaterializationPolicy.h`，`1u << 16`，不占用公开 `RenderFlags` 位）。`PictureImage::drawPicture` 置位，`EnsureSimpleBlendChild` 见此标志**两支物化全部跳过**。

**关键**：必须两支都跳过。只跳过「AOT 可匹配性」那一支时误差仅降 27~75%（仍 23~46 可见）；两支都跳过后 `ScalePictureImage` **完全 PASS（78ms）**。

**这也证明「正确性物化」的必要性注释是过时的** —— 至少在嵌套光栅化场景下不成立，跳过后渲染依然正确。

---

## 3. 九个已排除的假设

以下方向**全部被数据否决**，不要重试：

| # | 假设 | 否决依据 |
|---|---|---|
| 1 | 透明区颜色反转（判据 A） | α 加权后仅 1~8，非主因 |
| 2 | 顶层 Decal 语义丢失 | 部分改善但未根除 |
| 3 | 嵌套 Decal 未递归检测 | 同上（max 128→64） |
| 4 | premultiplied 8-bit 量化 | α 加权仅 1~8；`PixelFormat` 只有 8-bit 格式（5 种），加浮点需改 49 文件 × 四后端，且非主因 |
| 5 | `roundOut()` 亚像素偏移 | 插桩证明 drawRect 本来就是整数，零偏移 |
| 6 | 半像素 inset 缺失 | 加了**更差**（116→120 像素，max 128→255）；JIT 的 `clampedCoord` 已用含 0.5 inset 的 `shaderClamp` |
| 7 | sampleArea 未传递 | 插桩证明 `drawRect` 与 `sampleArea` **完全相同**（都是 0,0,110,110），无信息可传 |
| 8 | ClampToBorderLinear 的 1 纹素渐变 | 该机制真实存在（`GLSLTiledTextureEffect.cpp` 的 `mix(color, vec4(0), min(abs(err),1.0))`），但 trace 显示所有物化点 `shaderMode=0`（硬件路径），不含此数学 |
| 9 | perspective 变换 | 加检查后 `Matrix`/`ComplexSVG` 仍失败 |

**共同教训**：前八个都假设「物化是对的，只是参数没调好」，实际两个真因都是**范围**问题（采样范围、光栅化层级），不是**精度**问题。

---

## 4. 剩余代价（实测，双后端）

| 后端 | 基线失败 | 物化后 | 净新增 | 其中真实可见 |
|---|---|---|---|---|
| Metal | 28 | 34 | +6 | **1** |
| OpenGL | 4 | 22 | +18 | **3** |

**OpenGL 新增 18 个的精确分解**（不是「OpenGL 代价更大」，主要是基线更干净导致既有问题显形）：

| 类别 | 数量 | 说明 |
|---|---|---|
| Metal 基线里就有 | **3** | `LayerTest.Matrix`(224)、`SVGRenderTest.ComplexSVG`(237)、`FilterTest.OpacityShadowTest` —— **两后端都坏**，Metal 基线已吸收 |
| Metal 也新增 | 5 | 两后端共有的物化代价 |
| 仅 OpenGL 新增 | 10 | 见下 |

**仅 OpenGL 的 10 个**：2 个亚视觉（1.0）、2 个可见（`ComposeImageFilter` 10.8/11.8）、**4 个 PDFExport**（走 `Baseline::Compare(data, key)` **二进制比较**，1 bit 不同即失败，不适用可见性判断）、2 个未测。

**OpenGL 命中率无提升（0.43%）不是物化的问题** —— 被 `PermutationMatcher.cpp` 的 stage-1 白名单挡在匹配之前（`IsOpenGLStage1Candidate`）。白名单放开后同样能拿 +213。

---

## 5. 准入判据（白名单式）

物化只可能在三处丢信息，每处对应一条判据：

| 判据 | 环节 | 现状 |
|---|---|---|
| **A 值域** | RGBA8 量化、premultiplied 透明区 | `AOTEffectDecomposer` 已拒绝 alpha bias≠0 的 ColorMatrix |
| **B 空间** | `roundOut()` + Exact 尺寸 → 采样越界 | **已修**（apron=1） |
| **C 采样语义** | 原子树的 tile/边界行为被 `TextureEffect` 默认采样接管 | 实测未成为主因（物化点均为硬件路径） |
| **D 层级** | 嵌套光栅化中的冗余物化 | **已修**（NestedRasterization） |

**设计要求**：判据必须**白名单式**（默认拒绝、显式放行），落点是 `AOTMaterializationPolicy::Evaluate`。黑名单式（遇到问题加 guard）补不完 —— 九个被否决的假设就是证据。

**载体缺口**：`EffectTraits` 的 `isSelfContained` / `preservesAlphaRepresentation` / `preservesColorSpace`（`AOTEffect.h`）被定义、被各 `lowerToAOT` 填值，但**全库零读取**。安全判断有载体没逻辑。

---

## 6. 验收门

1. `AOTRenderConsistencyTest` 双渲染 **byte-exact**
2. **净新增失败 = 0**，或每个新增都经 §1 的 α 加权方法确认为亚视觉
3. 截图差异出现时分析**空间分布**：集中在边界行 → 判据 B/C；均匀散布 → 判据 A；集中在特定色值 → 可能是可视化叠加层（如脏区框）
4. 命中率上升不能作为接受回归的理由
5. 变体总数**不增**（物化方案应为 0 增量）
6. 截图基准变更：**仅提示用户执行 `/accept-baseline`，绝不自行 accept**
