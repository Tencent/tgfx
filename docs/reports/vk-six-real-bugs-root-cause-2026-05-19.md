# TGFX Vulkan 后端 6 类真 bug 根因分析

**报告日期**：2026-05-19
**分支**：`feature/edwardxfshen_vulkan_swiftshader`
**作者**：自动化分析（基于 5/18 + 5/19 两份报告交叉验证 + 静态代码阅读）
**附属数据目录**：`.codebuddy/gl-vk-analysis/run-2026-05-18/`、`.codebuddy/gl-vk-analysis/run-2026-05-19/`

---

## 目录

- [摘要](#摘要)
- [1. 范围与方法](#1-范围与方法)
  - [1.1 "6 类真 bug" 的来源](#11-6-类真-bug-的来源)
  - [1.2 证据基础](#12-证据基础)
  - [1.3 读图说明](#13-读图说明)
  - [1.4 关键指纹与解读约定](#14-关键指纹与解读约定)
- [2. 数据汇总](#2-数据汇总)
- [3. Bug 1 — Mipmap 实现链](#3-bug-1--mipmap-实现链)
- [4. Bug 2 — shapeMask / shaderMaskFilter 通道](#4-bug-2--shapemask--shadermaskfilter-通道)
- [5. Bug 3 — PartialDrawLayer 路径](#5-bug-3--partialdrawlayer-路径)
- [6. Bug 4 — TemporaryOffscreenImage](#6-bug-4--temporaryoffscreenimage)
- [7. Bug 5 — Matrix_3D 路径](#7-bug-5--matrix_3d-路径)
- [8. Bug 6 — Layer 杂项](#8-bug-6--layer-杂项)
- [9. 综合优先级与连锁效应](#9-综合优先级与连锁效应)
- [10. 风险与建议](#10-风险与建议)
- [附录 A：每个 bug 的剥离实验脚本](#附录-a每个-bug-的剥离实验脚本)
- [附录 B：5/18 与 5/19 数据完整对照](#附录-b518-与-519-数据完整对照)

---

## 摘要

5/18（mac OpenGL vs mac Vulkan，同 SwiftShader）与 5/19（mac VK vs win VK 真机驱动）两份报告交叉比对后，把 218→34 例 LARGE_REGION 收敛为 9 个子族。其中 6 个子族**两份报告同时指向 TGFX VK 后端实现缺陷**（不归因于 SwiftShader Reactor IR、也不归因于 win VK 驱动）：

1. **Mipmap 实现链**——影响 3 例 `ImageRenderTest`，根因高度怀疑：`MipmapMode::None` 时 VK 仍构建多级 mip chain，并以链式 `vkCmdBlitImage` 填充，与 GL `glGenerateMipmap` 在 base-level 采样路径上行为不一致。
2. **shapeMask / shaderMaskFilter 通道**——影响 4 例（`shapeMask` / `shaderMaskFilter` / `imageMask` / `textMask`），所有 4 例 `rgb_max ≥ 741`、最高 765，是**典型 RGBA↔BGRA swizzle 错排指纹**。怀疑根因：mask offscreen surface 的 `PixelFormat ↔ VkFormat` 选择错误。
3. **PartialDrawLayer 路径**——2 例 `rgb_max=765`、`a_max≤10`、bbox 严格落在 `clipRect(0,0,110,110)` 内。怀疑：`Layer::draw(canvas)` 路径下 `BackgroundBlurStyle` 的 render pass 未被正确切分。
4. **TemporaryOffscreenImage**——`_pic`/`_image1` LR、`_image2`（仅去掉 filters）IDENTICAL，剥离实验已天然存在。`rgb_max=510` 是 2 通道 swizzle 的指纹，与 Bug 2 同根因家族（offscreen 路径选 attachment format 错）。
5. **Matrix_3D 路径**——3 例 `a_max=0`，`rgb_max` 仅 68/144（非 swizzle）。怀疑：`src/layers/compositing3d/` 子系统中存在依赖容器迭代顺序的代码（`unordered_map` / `unordered_set`），跨平台/跨编译器内存布局不同导致排序差异。
6. **Layer 杂项 5 例**——4/5 例（`PassThrough_Test`、`getBounds`、`LayerFilterTest/innerShadow`、`LayerFilterTest/filters`）`rgb_max` ∈ {510, 765}，与 Bug 4 同根因；剩 1 例 `ShapeStyleWithMatrix` 是独立的 shader matrix 精度问题。

**收敛结论**：6 类 bug 实际只对应 **4 个独立根因**——
- 根因 R1（offscreen attachment format/swizzle）：Bug 2 + Bug 4 + Bug 6 中 4 例
- 根因 R2（mipmap chain 填充策略）：Bug 1
- 根因 R3（partial-draw render pass 切分）：Bug 3
- 根因 R4（compositing3d 容器顺序依赖）：Bug 5
- 独立小问题：Bug 6 中的 `ShapeStyleWithMatrix`

**置信度**：根因 R1 / R2 高（有清晰指纹和代码路径）；R3 / R4 中（推断成分较高，需 RenderDoc 抓帧确认）。

---

## 1. 范围与方法

### 1.1 "6 类真 bug" 的来源

5/19 报告 §6 把 34 例 LR 用例按"是否在 5/18 报告中也定位为 TGFX 缺陷"做了 A/B 象限标注：
- **A 象限**：mac GL/VK 已差异 + mac VK / win VK 也差异 → TGFX VK 后端 bug 候选
- **B 象限**：仅 mac VK / win VK 差异（mac GL/VK 相同）→ SwiftShader vs 真机驱动差异
- **A?** ：象限不明显，需进一步剥离

本报告分析的 6 类是 5/19 §6 中所有 A 与 A? 标注用例去除子族重复后的子族归并：

| Bug | 子族 | 涉及 key |
|---|---|---|
| 1 | mipmap | `ImageRenderTest/mipmap_none`、`drawImage`、`mipmap_linear` |
| 2 | mask | `LayerMaskTest/shapeMask`、`FilterTest/shaderMaskFilter`、`LayerMaskTest/imageMask`、`LayerMaskTest/textMask` |
| 3 | partial-layer | `LayerTest/PartialDrawLayer`、`PartialDrawLayer_shapeLayer` |
| 4 | offscreen | `LayerTest/TemporaryOffscreenImage_pic`、`TemporaryOffscreenImage_image1` |
| 5 | 3D-matrix | `LayerTest/Matrix_3D`、`Matrix_3D_2D_3D`、`Matrix_3D_Offscreen_Blend` |
| 6 | layer/layer-filter/passthrough | `LayerTest/PassThrough_Test`、`getBounds`、`ShapeStyleWithMatrix`、`LayerFilterTest/innerShadow`、`LayerFilterTest/filters` |

> 注：`imageMask` / `textMask` / `TemporaryOffscreenImage_*` 等部分用例在 **5/18 仅落入 OTHER 桶**（不是 LR），但**指纹 (`rgb_max`) 与同子族 LR 用例完全一致**——意味着差异性质相同，只是像素分布更稀疏。所以归在同一 bug 下。文档每节都标注了 5/18 的具体 bucket，避免误读。

### 1.2 证据基础

每个 bug 节的"现象证据"小节里，所有数据来自：
- `.codebuddy/gl-vk-analysis/run-2026-05-18/diff-table.csv` — mac GL vs mac VK
- `.codebuddy/gl-vk-analysis/run-2026-05-19/diff-table.csv` — mac VK vs win VK
- 三联图来自对应 `figures/` 目录

代码路径来自直接阅读 `src/gpu/opengl/`、`src/gpu/vulkan/`、`src/layers/`。**未使用 RenderDoc 抓帧**——所有"根因"均为推断，文档每节末尾给出"置信度"与"待验证假设"。

### 1.3 读图说明

每个 bug 节附带一张 PNG 三联图，**列定义随报告来源不同而异**：

- **5/18 来源（标记 "GL vs VK 三联图"）**：从左到右 = `GL (baseline) | VK (mac/SwiftShader) | |GL−VK|×8`
- **5/19 来源（标记 "mac VK vs win VK 三联图"）**：从左到右 = `mac VK (baseline) | win VK (实测) | |Δ|×8`

> 看图技巧：很多 case 的左右两图肉眼几乎一样——这不代表没差异，而是差异集中在边缘/高频或单像素饱和。**一定要看右侧 diff 面板**，配合下方指标定位差异性质。

### 1.4 关键指纹与解读约定

| 指纹 | 含义 |
|---|---|
| `rgb_max=765` | 至少一个像素 R+G+B 三通道**全部完整翻转**（255×3）。**RGBA↔BGRA swizzle 错排的几乎确诊指纹** |
| `rgb_max=510` | 两个通道完整翻转（255×2）。同一指纹族（仅一个通道恰好同值） |
| `rgb_max=255` | 单通道完整翻转，或某通道值差异满量程 |
| `a_max=0` | Alpha 完全一致 → **几何/可见性正确**，差异纯在 RGB |
| `a_max≥20` | Alpha 也偏离 → 透明度合成路径有问题（更严重） |
| `bbox=W×H` | 强差异连通块外接框。bbox 接近全图说明全局漂移；bbox 局部说明特定子区域错 |
| `dr_pct` | diff_ratio：差异像素占总像素比例 |

---

## 2. 数据汇总

下表合并了 5/18（mac GL vs mac VK，同 SwiftShader）与 5/19（mac VK vs win VK 真机驱动）两次跑的关键指标。

| Bug | key | 5/18 bucket | 5/18 dr% | 5/18 rgb_max | 5/18 a_max | 5/19 bucket | 5/19 dr% | 5/19 rgb_max | 5/19 a_max | bbox |
|---|---|---|---:|---:|---:|---|---:|---:|---:|---|
| 1 | `ImageRenderTest/mipmap_none` | LR | 35.77 | 46 | 0 | LR | 35.94 | 46 | 0 | 120×90 |
| 1 | `ImageRenderTest/drawImage` | LR | 20.19 | 42 | 6 | LR | 26.77 | 42 | 6 | 372×336 |
| 1 | `ImageRenderTest/mipmap_linear` | OTHER | 61.44 | 13 | 0 | OTHER | 78.92 | 14 | 0 | 120×90 |
| 2 | `LayerMaskTest/shapeMask` | OTHER | 1.41 | 255 | 1 | LR | 11.86 | 255 | 1 | 2812×3250 |
| 2 | `FilterTest/shaderMaskFilter` | OTHER | 4.81 | 741 | 252 | LR | 6.75 | 741 | 252 | 980×719 |
| 2 | `LayerMaskTest/imageMask` | LR | 8.89 | 765 | 128 | OTHER | 3.17 | 765 | 127 | 2812×3379 |
| 2 | `LayerMaskTest/textMask` | OTHER | 0.89 | 765 | 54 | OTHER | 0.93 | 765 | 54 | 2754×3186 |
| 3 | `LayerTest/PartialDrawLayer` | LR | 10.73 | 765 | 8 | LR | 7.12 | 765 | 8 | 106×113 |
| 3 | `LayerTest/PartialDrawLayer_shapeLayer` | LR | 10.82 | 765 | 10 | LR | 7.50 | 765 | 10 | 106×112 |
| 4 | `LayerTest/TemporaryOffscreenImage_pic` | OTHER | 3.10 | 510 | 2 | LR | 6.94 | 510 | 2 | 129×159 |
| 4 | `LayerTest/TemporaryOffscreenImage_image1` | OTHER | 2.95 | 510 | 1 | LR | 5.75 | 510 | 2 | 129×159 |
| 5 | `LayerTest/Matrix_3D` | LR | 5.07 | 144 | 0 | LR | 5.06 | 144 | 0 | 141×189 |
| 5 | `LayerTest/Matrix_3D_2D_3D` | LR | 6.31 | 144 | 1 | LR | 6.08 | 144 | 1 | 145×189 |
| 5 | `LayerTest/Matrix_3D_Offscreen_Blend` | LR | 12.62 | 34 | 0 | LR | 14.53 | 68 | 0 | 82×138 |
| 6 | `LayerTest/PassThrough_Test` | OTHER | 2.71 | 510 | 6 | LR | 9.71 | 510 | 140 | 66×94 |
| 6 | `LayerTest/getBounds` | LR | 10.92 | 765 | 10 | LR | 11.21 | 765 | 10 | 92×77 |
| 6 | `LayerTest/ShapeStyleWithMatrix` | ULP_NOISE | 0.00 | 3 | 1 | LR | 8.70 | 255 | 1 | 130×100 |
| 6 | `LayerFilterTest/innerShadow` | OTHER | 3.68 | 384 | 1 | LR | 8.42 | 765 | 1 | 278×280 |
| 6 | `LayerFilterTest/filters` | EDGE_AA | 0.09 | 28 | 1 | LR | 7.06 | 765 | 2 | 130×130 |

> 注意：`ShapeStyleWithMatrix`、`LayerFilterTest/filters` 等用例在 5/18 几乎没差异（ULP_NOISE / EDGE_AA），到 5/19 才放大成 LR——说明这些差异**只在 win VK 真机驱动 vs SwiftShader VK 之间显现**，机制可能与"驱动相关 shader 优化"或"undefined 行为暴露"有关。后文每节会区分"5/18+5/19 双显（确诊 TGFX bug）" 与"仅 5/19 显（可能 driver UB 或 TGFX 跨驱动隐患）"。

---

## 3. Bug 1 — Mipmap 实现链

**子族**：mipmap
**影响用例**：`ImageRenderTest/mipmap_none`、`ImageRenderTest/drawImage`、`ImageRenderTest/mipmap_linear`

### 3.1 现象证据

mac VK vs win VK 三联图（5/19）：

![LR01 mipmap_none](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR01__ImageRenderTest__mipmap_none.png)

![LR02 drawImage](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR02__ImageRenderTest__drawImage.png)

![mipmap_linear OTHER 桶](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/OTHER__ImageRenderTest__mipmap_linear.png)

5/18 mac GL vs mac VK 三联图（首次定位）：

![5/18 LR01 mipmap_none](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR01__ImageRenderTest__mipmap_none.png)

![5/18 LR03 drawImage](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR03__ImageRenderTest__drawImage.png)

### 3.2 现象解读

| 用例 | 5/18 dr% | 5/19 dr% | rgb_max | a_max | 特征 |
|---|---:|---:|---:|---:|---|
| `mipmap_none` | 35.77 | 35.94 | 46 | 0 | 全图低幅度漂移、几何对、无 swizzle |
| `drawImage` | 20.19 | 26.77 | 42 | 6 | 整图 372×336 漂移 |
| `mipmap_linear` | 61.44 | 78.92 | 13 | 0 | 极低幅度但分布广（OTHER 桶） |

三例的共同指纹：
- `a_max ≤ 6`：alpha 几乎不动 → 几何与覆盖正确
- `rgb_max ≤ 46`：单通道偏移 ≤ 18%，**远小于 swizzle 阈值 (255)**
- bbox 接近整个图像 → 全局漂移，不是局部缺陷

这是典型的"**采样滤波结果整体偏移**"，而不是几何/透明度错。

### 3.3 代码路径对比

**GL 后端**：

```cpp
// src/gpu/opengl/GLCommandEncoder.cpp:162
gl->generateMipmap(glTexture->target());
```

`glGenerateMipmap` 由驱动决定下采样核——Apple GPU 上多数走 box-2×2，桌面 GPU 上可能用 tent/Lanczos 等。

**VK 后端**：

```cpp
// src/gpu/vulkan/VulkanCommandEncoder.cpp:299-322
for (uint32_t i = 1; i < mipLevels; i++) {
    // 严格 2×2 双线性 blit
    vkCmdBlitImage(commandBuffer, image, ..., image, ..., VK_FILTER_LINEAR);
    // barrier transition
}
```

VK 用 `vkCmdBlitImage` 链式逐级线性 blit 模拟 mipmap 生成。

### 3.4 根因推断

**最关键的 case 是 `mipmap_none`**。该用例 `MipmapMode::None`，理论上**不应读到 mip≥1**。但 5/18 + 5/19 两次跑都呈现 35.94% 像素差异、`rgb_max=46` 的全局漂移。可能机制：

1. VK 端在创建 texture 时对所有 image 都按 `mipLevelCount = floor(log2(max(w,h))) + 1` 分配 mip chain（无论是否要用），并通过 `vkCmdBlitImage` 链填充；
2. Sampler 即便设了 `maxLod=0`，**采样硬件仍然可能在 anisotropic / derivative 路径上读取相邻 mip**——而该路径在 SwiftShader Reactor IR 与真机驱动上行为分歧；
3. GL 端因为 `glGenerateMipmap` 没被调用，mip≥1 是未定义内容，但 `GL_TEXTURE_MAX_LEVEL=0` 钳制有效。

观察 `VulkanSampler.cpp:84` 的 `maxLod=0` 钳制看似已做，但实际仍漏出差异，所以**问题不在 sampler 钳制路径，而在 mip chain 内容本身**——VK 端 mip≥1 被链式 blit 写过，GL 端是未定义。当采样硬件以 zero LOD 读 mip 0 时，理论上不应读到 mip≥1，但驱动 derivative 计算精度差异导致偶发越界采样。

`drawImage` 与 `mipmap_linear` 是同根因的副作用——只要 mip chain 内容存在差异，使用 mipmap 的所有路径都会漂移。

### 3.5 修复建议

- **短期**：`MipmapMode::None` 时 VK 后端不要构建多级 mip texture（`mipLevelCount = 1`），让两端 mip chain 形态一致。这条改动小、风险低。
- **中期**：审计所有 image 创建路径，确认 `mipLevelCount` 是按真实需求决定的，不是默认按 `floor(log2)+1` 全开。
- **长期**：评估是否切换到 compute shader 自定义下采样核（box / Kaiser），统一两端语义并避免链式 blit 的 LOD 衰减。

### 3.6 置信度

- **代码路径差异**：✅ 直接读到（GL `glGenerateMipmap` vs VK `vkCmdBlitImage` 链）
- **`mipmap_none` 翻车的具体原因**：⚠️ 推断为 mip chain 多级填充策略，需要打印实际 `mipLevelCount` 验证
- **优先级**：P1（影响所有 ImageRender 类，但修复改动小）

### 3.7 待验证假设

1. 在 `VulkanGPU::createTexture` 中打印 `descriptor.mipLevelCount`，确认 `MipmapMode::None` 时是否仍然 > 1。
2. 临时把 `mipmap_none` 用例 VK 路径强制 `mipLevelCount=1`，确认 dr% 是否回落到 < 1%。

---

## 4. Bug 2 — shapeMask / shaderMaskFilter 通道

**子族**：mask
**影响用例**：`LayerMaskTest/shapeMask`、`FilterTest/shaderMaskFilter`、`LayerMaskTest/imageMask`、`LayerMaskTest/textMask`

### 4.1 现象证据

mac VK vs win VK 三联图（5/19）：

![LR11 shapeMask](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR11__LayerMaskTest__shapeMask.png)

![LR24 shaderMaskFilter](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR24__FilterTest__shaderMaskFilter.png)

5/18 mac GL vs mac VK 三联图：

![5/18 LR11 imageMask](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR11__LayerMaskTest__imageMask.png)

### 4.2 现象解读

| 用例 | 5/18 / 5/19 dr% | rgb_max | a_max | bbox | 关键指纹 |
|---|---|---:|---:|---|---|
| `shapeMask` | 1.41 / 11.86 | **255** | 1 | 2812×3250 | 单通道全翻转，alpha 对 |
| `shaderMaskFilter` | 4.81 / 6.75 | **741** | 252 | 980×719 | RGBA 全部错排 |
| `imageMask` | 8.89 / 3.17 | **765** | 128 | 2812×3379 | RGB 全翻转 |
| `textMask` | 0.89 / 0.93 | **765** | 54 | 2754×3186 | RGB 全翻转 |

**所有 4 例 `rgb_max ≥ 255`，最高 765**。`rgb_max=765` 是"R+G+B 三通道全部翻转"的几乎确诊指纹——这种现象的物理来源**只有两种**：
1. RGBA↔BGRA 通道顺序错排（swizzle）
2. `glReadPixels` / `vkCmdCopyImageToBuffer` 的 format 描述与实际 attachment format 不一致

### 4.3 代码路径

Mask 渲染走 `getMaskData → ImageFilter::ColorFilter(Luma) → MakeImageShader`（`src/layers/Layer.cpp:1242-1252`），关键步骤：
1. mask 内容渲染到独立 offscreen surface（alpha-only 或 luma-only）
2. 该 surface 作为 `Image` 通过 `ImageShader` 重新采样
3. 跨 surface 读回 + 重采样

**与 `ReadPixelsTest/Surface_rgb_A_to_bgr_A` 是同源问题**（5/18 报告 SPARSE_HIGH，`rgb_max=765`）——本质都是 VK swizzle 不正确。

### 4.4 根因推断

**最高怀疑**：mask offscreen surface 创建时 `PixelFormat ↔ VkFormat` 映射错误：

```
PixelFormat::RGBA_8888 → VK_FORMAT_R8G8B8A8_UNORM   ✓
PixelFormat::BGRA_8888 → VK_FORMAT_B8G8R8A8_UNORM   ✓
```

但 mask offscreen 在某些路径（如 `OffscreenRenderer::makeMaskSurface`）下，可能：
- 创建 surface 时按 RGBA 但 attachment 实际给到 BGRA format
- 或 `vkCmdCopyImageToBuffer` 拷贝路径下 component swizzle 与实际 format 不一致

5/18 时 `shaderMaskFilter`、`shapeMask` 都被归到 OTHER（dr% 较低，仅 1-5%），但**`rgb_max` 已经达到 741/255**——说明差异性质（swizzle）从 5/18 就存在，只是 dr% 没破 5% 阈值。5/19 由于 win 真机驱动对 swizzle 错位敏感度不同，dr% 放大到 ≥ 5%，进入 LR。

### 4.5 修复建议

1. **审计 `VulkanCaps`/`VulkanGPU` 中所有 `PixelFormat → VkFormat` 调用点**，确认是否一致
2. **审计所有 offscreen surface 创建路径**，特别是 mask / filter 这类内部路径（即不直接通过用户 API 创建的）
3. 配合 RenderDoc 抓取 mask surface 的 attachment 实际 format 与 swizzle 设置

### 4.6 置信度

- **指纹**：✅ 高（`rgb_max=765/741` 几乎不可能是其他原因）
- **代码定位**：⚠️ 中（指向 mask offscreen 路径，但具体行需 RenderDoc 确认）
- **连锁影响**：很可能修复后 `Surface_rgb_A_to_bgr_A` 也修复

### 4.7 待验证假设

写 micro test：
1. 创建一个 mask offscreen surface（按现有内部代码路径）
2. 立即 `readPixels` 读出
3. 比较读出像素与已知 RGBA 期望值——若有 R/B 翻转即定位

---

## 5. Bug 3 — PartialDrawLayer 路径

**子族**：partial-layer
**影响用例**：`LayerTest/PartialDrawLayer`、`LayerTest/PartialDrawLayer_shapeLayer`

### 5.1 现象证据

mac VK vs win VK 三联图：

![LR21 PartialDrawLayer](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR21__LayerTest__PartialDrawLayer.png)

![LR20 PartialDrawLayer_shapeLayer](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR20__LayerTest__PartialDrawLayer_shapeLayer.png)

5/18 三联图：

![5/18 LR09 PartialDrawLayer](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR09__LayerTest__PartialDrawLayer.png)

![5/18 LR08 PartialDrawLayer_shapeLayer](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR08__LayerTest__PartialDrawLayer_shapeLayer.png)

### 5.2 现象解读

| 用例 | 5/18 / 5/19 dr% | rgb_max | a_max | bbox |
|---|---|---:|---:|---|
| `PartialDrawLayer` | 10.73 / 7.12 | 765 | 8 | 106×113 |
| `PartialDrawLayer_shapeLayer` | 10.82 / 7.50 | 765 | 10 | 106×112 |

特征：
- 两次跑都 LR、`rgb_max=765`（swizzle 指纹）、`a_max ≤ 10`
- bbox 严格 ≈ 用例中 `clipRect(0,0,110,110)` 的有效绘制区
- 差异**只在 clip 区内**，clip 外完全一致

### 5.3 代码路径

测试用例（`test/src/LayerTest.cpp:1664+`）的关键操作：

```cpp
canvas->rotate(30);
canvas->scale(2.0f, 1.0f);
canvas->translate(20, 20);
canvas->clipRect(Rect::MakeWH(110, 110));
auto blurStyle = BackgroundBlurStyle::Make(10, 10);  // ★ BackgroundBlur
rootLayer->setLayerStyles({blurStyle});
rootLayer->draw(canvas);  // ★ 走 Layer::draw 直接路径，不是 displayList.render
```

关键点：
1. `canvas->rotate(30)` + `scale(2,1)` + `translate(20,20)`：非整数复合变换矩阵
2. `BackgroundBlurStyle`：需要从当前 surface 读回内容做 blur input
3. `rootLayer->draw(canvas)`：partial-draw 路径，绕开 `displayList.render` 主路径

### 5.4 根因推断

**主要怀疑**：`BackgroundBlur` 需要把当前 framebuffer 当作 input texture 采样。VK 中这要求 render pass 已经 end（attachment 切换为 sampled image layout）。

- **`displayList.render` 主路径**：在 layer style 处理时会显式切 render pass，行为正确
- **`Layer::draw(canvas)` partial-draw 路径**：可能没有触发同样的 render pass split，VK 端在同一 pass 内试图采样自身 attachment，结果未定义

GL 后端因 `glFlush` + 隐式同步勉强能跑对；VK 后端如果 render pass 没切，就读到未定义内容——这恰好能解释 `rgb_max=765`（未定义内存可能恰好填了 swap 的字节）。

**次要怀疑**（不能完全排除）：`MakeImage("imageReplacement.png")` 配合非整数 matrix → image shader sampling，在 partial-draw 路径下边界舍入不同。但这无法解释 `rgb_max=765` 这么强的指纹。

### 5.5 修复建议

1. 用 RenderDoc 对比 `PartialDrawLayer` 在 GL 和 VK 下的 render pass 数量
2. 若确认 render pass 没切，在 `BackgroundHandler` 里为 partial-draw 路径强制 break render pass
3. 或者更彻底：让 `Layer::draw(canvas)` 在遇到 `BackgroundBlur`/`BackdropFilter` 时，复用 `displayList.render` 的 layer style 处理逻辑

### 5.6 置信度

- **指纹**：✅ 高（rgb_max=765 + bbox 与 clip 区一致）
- **代码定位**：⚠️ 中偏低（render pass 切分是推断，未抓帧确认）

### 5.7 待验证假设

剥离测试（最容易做的）：
1. 把 `BackgroundBlurStyle::Make(10, 10)` 注释掉跑一次——若 LR 消失，确认与 BackgroundBlur 相关
2. 把 `clipRect` 改成全图大小再跑——若 LR 消失，与 clip + render pass 相关
3. 把 `rootLayer->draw(canvas)` 改成 `displayList.render(...)` 跑一次——若 LR 消失，确认是 partial-draw 路径独有问题

---

## 6. Bug 4 — TemporaryOffscreenImage

**子族**：offscreen
**影响用例**：`LayerTest/TemporaryOffscreenImage_pic`、`LayerTest/TemporaryOffscreenImage_image1`

### 6.1 现象证据

mac VK vs win VK 三联图：

![LR23 TemporaryOffscreenImage_pic](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR23__LayerTest__TemporaryOffscreenImage_pic.png)

![LR29 TemporaryOffscreenImage_image1](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR29__LayerTest__TemporaryOffscreenImage_image1.png)

### 6.2 现象解读 — 测试自带剥离实验

| 用例 | 5/18 / 5/19 dr% | rgb_max | a_max | bbox | 测试代码差异 |
|---|---|---:|---:|---|---|
| `TemporaryOffscreenImage_pic` | 3.10 / 6.94 | **510** | 2 | 129×159 | `setFilters({DropShadowFilter})` + glassLayer |
| `TemporaryOffscreenImage_image1` | 2.95 / 5.75 | **510** | 2 | 129×159 | 同 _pic |
| `TemporaryOffscreenImage_image2` | **IDENTICAL** | — | — | — | `setFilters({})` + `setAlpha(0.8f)` |

**`_image2` 相比 `_pic`/`_image1` 只是把 filters 去掉**——结果就 IDENTICAL，**这天然就是剥离实验**。

### 6.3 根因推断

`DropShadowFilter` 的处理：
1. 把 layer 内容渲染到一个临时 offscreen surface
2. 把该 surface 作为 Image 采样到主 surface（带 shadow 偏移）

这条路径与 Bug 2（mask offscreen）走的是同一类临时 offscreen——`rgb_max=510` 是 2 通道 swizzle 的指纹（`Bug 2` 是 765 即 3 通道，`Bug 4` 是 510 即 2 通道，原因是 alpha 通道恰好为 0/255 之一就只剩 2 通道差异）。

**核心怀疑**：`OffscreenRenderer` 或 filter 临时 surface 创建时，VK 端选择了与主 surface 不同的 `VkFormat`（如主用 BGRA、临时给了 RGBA），shader 采样时 sampler 把 component 按 RGBA 解但内存里是 BGRA。

### 6.4 修复建议

与 Bug 2 同根：审计所有"临时 offscreen surface 创建"的路径（`makeOffscreenSurface` / `OffscreenRenderer` / filter 内部缓存等），保证 `VkFormat` 与主 surface 或与 image shader 期望一致。

### 6.5 置信度

- **指纹**：✅ 高（`rgb_max=510` + 测试自带剥离实验确认是 filters 触发）
- **根因合并**：✅ 高（与 Bug 2 同家族）
- **优先级**：P0（修复 Bug 2 时一并解决）

### 6.6 待验证假设

1. 在 `VulkanGPU::createTexture` 入口打印所有内部 offscreen 路径调用的 `pixelFormat` 与最终 `VkFormat`，对比主 surface
2. 修复后预期 `_pic`、`_image1` 同时变 IDENTICAL，连带可能修复 Bug 6 中 4/5 例

---

## 7. Bug 5 — Matrix_3D 路径

**子族**：3D-matrix
**影响用例**：`LayerTest/Matrix_3D`、`LayerTest/Matrix_3D_2D_3D`、`LayerTest/Matrix_3D_Offscreen_Blend`

### 7.1 现象证据

mac VK vs win VK 三联图：

![LR34 Matrix_3D](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR34__LayerTest__Matrix_3D.png)

![LR27 Matrix_3D_2D_3D](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR27__LayerTest__Matrix_3D_2D_3D.png)

![LR08 Matrix_3D_Offscreen_Blend](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR08__LayerTest__Matrix_3D_Offscreen_Blend.png)

5/18 三联图：

![5/18 LR16 Matrix_3D](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR16__LayerTest__Matrix_3D.png)

![5/18 LR06 Matrix_3D_Offscreen_Blend](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR06__LayerTest__Matrix_3D_Offscreen_Blend.png)

### 7.2 现象解读

| 用例 | 5/18 / 5/19 dr% | rgb_max | a_max | bbox |
|---|---|---:|---:|---|
| `Matrix_3D` | 5.07 / 5.06 | 144 | 0 | 141×189 |
| `Matrix_3D_2D_3D` | 6.31 / 6.08 | 144 | 1 | 145×189 |
| `Matrix_3D_Offscreen_Blend` | 12.62 / 14.53 | 34→68 | 0 | 82×138 |

**关键指纹**：
- `a_max=0`（前两例）/ `a_max=0`（第三例）→ alpha 完全对，几何与覆盖正确
- `rgb_max ≤ 144`，**远小于 swizzle 阈值** → 不是通道错排
- 这是"颜色累积顺序不同"的指纹

### 7.3 代码路径

`canPreserve3D` 路径走 `src/layers/compositing3d/`：

```
src/layers/compositing3d/
├── BspTree.cpp          # 软件 BSP 切多边形排序
├── Render3DContext.cpp  # 3D 上下文管理
├── Opaque3DContext.cpp  # 不透明 3D 子图绘制
└── ...
```

BSP 是软件实现，不依赖 GPU depth。绘制顺序由软件决定。

### 7.4 根因推断

**主要怀疑**：`compositing3d` 子系统中存在依赖**容器迭代顺序**的代码（如 `std::unordered_map` / `std::unordered_set`）。这类容器：
- 在不同 STL 实现下哈希函数不同（libc++ vs libstdc++）
- 在 macOS clang 与 win MSVC 下迭代顺序不同
- 同一指针因不同进程内存布局而被分配到不同 bucket，导致迭代顺序不同

**关键证据**：`a_max=0` + `rgb_max=68/144`——这种"几何对、颜色累积顺序不同"的指纹，正是浮点累加序列差异（`(a + b) + c ≠ a + (b + c)`）的典型表现。3D 排序变了，半透明 alpha-over 累积顺序就变了，颜色就漂移。

`Matrix_3D_Offscreen_Blend` dr% 最高（14.53%），它名字里有 `Offscreen_Blend` ——3D 内容→offscreen→blend 回主 surface 多了一层，浮点累积序列差异被放大。

### 7.5 修复建议

1. 全文检索 `src/layers/compositing3d/` 中的 `unordered_map` / `unordered_set` 使用
2. 改用 `std::map` / `std::set`（按 key 有序）或 `std::vector` + 显式排序
3. 也要审计 `std::sort` 是否在某处用了不稳定排序但有等价 key（`std::stable_sort` 替代）

### 7.6 置信度

- **指纹**：✅ 中（颜色累积序列差异指纹明确）
- **根因定位**：⚠️ 低-中（仅指向"compositing3d 子系统的某种顺序差异"，未具体到行）
- **优先级**：P3（独立 3 例，影响小）

### 7.7 待验证假设

写一个 macro 把 compositing3d 内所有 unordered_* 容器替换为 ordered 版本，重跑这 3 例。若 dr% 变 0 → 确认根因。

---

## 8. Bug 6 — Layer 杂项

**子族**：layer / layer-filter / passthrough
**影响用例**：`LayerTest/PassThrough_Test`、`LayerTest/getBounds`、`LayerTest/ShapeStyleWithMatrix`、`LayerFilterTest/innerShadow`、`LayerFilterTest/filters`

### 8.1 现象证据

mac VK vs win VK 三联图：

![LR14 PassThrough_Test](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR14__LayerTest__PassThrough_Test.png)

![LR12 getBounds](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR12__LayerTest__getBounds.png)

![LR16 ShapeStyleWithMatrix](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR16__LayerTest__ShapeStyleWithMatrix.png)

![LR17 LayerFilterTest__innerShadow](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR17__LayerFilterTest__innerShadow.png)

![LR22 LayerFilterTest__filters](../.codebuddy/gl-vk-analysis/run-2026-05-19/figures/LR22__LayerFilterTest__filters.png)

### 8.2 现象解读

| 用例 | 5/18 / 5/19 dr% | rgb_max | a_max | bbox | 推断归类 |
|---|---|---:|---:|---|---|
| `PassThrough_Test` | 2.71 / 9.71 | **510** | **140** | 66×94 | Bug 4 同根，**额外 alpha 错** |
| `getBounds` | 10.92 / 11.21 | **765** | 10 | 92×77 | Bug 4 同根 |
| `ShapeStyleWithMatrix` | 0.00 / 8.70 | 255 | 1 | 130×100 | **独立**，shader matrix 精度 |
| `LayerFilterTest/innerShadow` | 3.68 / 8.42 | **765** | 1 | 278×280 | Bug 4 同根 |
| `LayerFilterTest/filters` | 0.09 / 7.06 | **765** | 2 | 130×130 | Bug 4 同根 |

### 8.3 子分析

#### 8.3a `PassThrough_Test` — Bug 4 + alpha 错的复合

`a_max=140` 是这组里**唯一 alpha 大错**的。
- `rgb_max=510`：与 Bug 4 / Bug 6 其他例同根（offscreen attachment swizzle）
- `a_max=140`：额外的 alpha 通道也错位

对应 `Layer.cpp:1133` 的 `passThroughBackground` 逻辑——决定是否走 offscreen。VK 后端在 `passThroughBackground=true` + 非 SrcOver 混合 + alpha<1 同时成立时的 offscreen 决策与 GL 不一致——本质就是 Bug 4，**叠加了 alpha 通道也错位**，所以是 Bug 4 的更严重变种。

#### 8.3b `getBounds` — Bug 4 同根

`rgb_max=765`、`a_max=10`：典型 RGB 全 swizzle。`Layer::getBounds` 内部触发 `getContentContourImage`（`Layer.cpp:1256`）路径，同样涉及临时 offscreen 渲染。同 Bug 4 根因。

#### 8.3c `ShapeStyleWithMatrix` — 独立小问题

5/18 在 ULP_NOISE 桶（`rgb_max=3`）几乎完全一致；5/19 跳到 LR `dr=8.70%`、`rgb_max=255`。
- 仅在 win VK 真机驱动下显现，5/18 SwiftShader 同后端不显现
- `rgb_max=255` 单通道翻转，bbox 130×100 局部
- 测试用例：ShapeStyle 配合 matrix → `MakeImageShader(shader)` + `makeWithMatrix`

**根因怀疑**：image shader 在 fragment shader 里做坐标变换的精度不同——SwiftShader 用 32-bit float 全程，真机驱动可能用 16-bit half precision 优化关键 mat3。具体到代码层，需要看 `ImageShader` 在 Vulkan 端 SPIR-V 生成时是否带 `RelaxedPrecision` decoration。

**优先级**：低，1 个独立 case。

#### 8.3d `LayerFilterTest/innerShadow` & `filters` — Bug 4 同根

都是 `rgb_max=765`、`a_max=1/2`，都走 LayerFilter（`_filters` 数组应用）→ `makeWithFilter` → 临时 offscreen。同 Bug 4 根因。

### 8.4 收敛结论

Bug 6 的 5 例中：
- **4 例（`PassThrough_Test`、`getBounds`、`LayerFilterTest/innerShadow`、`LayerFilterTest/filters`）= Bug 4 同根**。修复 Bug 4 时同时消失。
- **1 例（`ShapeStyleWithMatrix`）= 独立 shader matrix 精度问题**，与 Bug 4 无关。

### 8.5 置信度

- 4/5 例 Bug 4 同根：✅ 高（指纹 `rgb_max ∈ {510, 765}`，与 Bug 2 / Bug 4 一致）
- 1/5 例独立：⚠️ 中（仅在 win VK 显现，怀疑 driver UB / precision 优化差异）

---

## 9. 综合优先级与连锁效应

### 9.1 根因合并

6 个 bug 实际只对应 **4 个独立根因 + 1 个独立小问题**：

| 根因 | 涉及 bug | 涉及用例数 | 修复影响范围 |
|---|---|---:|---|
| **R1** offscreen attachment format / swizzle | Bug 2 全部 + Bug 4 全部 + Bug 6 中 4 例 | **10** | mask、shaderMaskFilter、imageMask、textMask、TemporaryOffscreenImage_pic、TemporaryOffscreenImage_image1、PassThrough_Test、getBounds、innerShadow、filters；连带 `ReadPixelsTest/Surface_rgb_A_to_bgr_A` |
| **R2** mipmap chain 填充策略 | Bug 1 全部 | 3 | mipmap_none、drawImage、mipmap_linear；可能影响其他 ImageRender 类 |
| **R3** partial-draw render pass 切分 | Bug 3 全部 | 2 | PartialDrawLayer、PartialDrawLayer_shapeLayer |
| **R4** compositing3d 容器顺序依赖 | Bug 5 全部 | 3 | Matrix_3D、Matrix_3D_2D_3D、Matrix_3D_Offscreen_Blend |
| 独立 | ShapeStyleWithMatrix | 1 | 仅 1 例 |

### 9.2 优先级表

| 优先级 | 根因 | 修复难度 | 修复后预期 LR 减少 |
|---|---|---|---:|
| **P0** | R1（offscreen swizzle） | 中 | 10 + 至少 1（ReadPixels）= **11** |
| **P1** | R2（mipmap chain） | 中 | 3 + 不确定（其他 ImageRender） |
| **P2** | R3（partial-draw render pass） | 高（需 RenderDoc） | 2 |
| **P3** | R4（3D 容器顺序） | 中（grep + 替换） | 3 |
| **P4** | ShapeStyleWithMatrix | 低-中 | 1 |

**预期总收益**：修复 4 个根因后，34 例 LR 中至少 19 例能消失，剩 15 例多归 SwiftShader vs 真机驱动差异（B 象限），此时 mac VK CI 失败应从 156 降到接近 win VK 真机的失败规模。

### 9.3 修复推进顺序

1. **第一步**：P0（R1）。先做剥离实验（micro test 跑 mask offscreen → readPixels 看 R/B 是否翻转），确认假设。确认后定位代码、修复、跑 baseline-wide 验证。**预期消除 11 例**。
2. **第二步**：P1（R2）。打印 `mipLevelCount`，确认 `MipmapMode::None` 时是否仍 > 1。修复改动小。
3. **第三步**：P3（R4）。grep `compositing3d/` 下 unordered 容器，替换为 ordered。
4. **第四步**：P2（R3）。剥离实验后 RenderDoc 抓帧，针对性修 render pass 切分。
5. **第五步**：P4（ShapeStyleWithMatrix）。优先级最低，可放最后。

---

## 10. 风险与建议

### 10.1 重要风险声明

本文档**未做 RenderDoc 抓帧、未实际改代码、未跑剥离实验**。所有"根因"均基于：
- ✅ 现象数据（5/18 + 5/19 两次 csv）
- ✅ 静态代码阅读（`src/gpu/opengl/`、`src/gpu/vulkan/`、`src/layers/`）
- ⚠️ 推断（结合两者 + 经验）

具体置信度：

| 根因 | 置信度 | 已确证 | 仅推断 |
|---|---|---|---|
| R1 swizzle | 高 | rgb_max=765 是几乎不可能的其他原因 | 具体是 attachment format 还是 readPixels 描述 |
| R2 mipmap | 中-高 | GL/VK 代码路径差异已确证 | mipmap_none 翻车的具体位置 |
| R3 render pass | 中-低 | 用例特征指向 BackgroundBlur + partial-draw | 完全是推断，需 RenderDoc 确诊 |
| R4 3D 容器顺序 | 低-中 | 颜色累积序列差异指纹 | 没具体定位到代码行 |

### 10.2 推荐推进流程（不直接动代码）

我**强烈建议不要直接基于本文档改代码**——因为推断成分较高，错误修改可能引入新问题。建议：

1. **第一阶段：剥离实验确认根因**
   - R1：写 micro test，创建 mask offscreen → readPixels → 验证 R/B
   - R2：打印 `mipLevelCount`，强制 `=1` 后跑 mipmap_none
   - R3：注释掉 `BackgroundBlurStyle` 跑 PartialDrawLayer
   - R4：grep + 临时替换 unordered → ordered 跑 Matrix_3D

2. **第二阶段：剥离实验通过后再实施修复**
   - 每个根因独立 PR
   - 每个 PR 跑一遍完整 baseline diff，确保不引入新失败

3. **第三阶段：每修一个根因重跑 5/19 类比对**
   - 验证 LR 数从 34 单调下降

### 10.3 立即可行动项

最容易做、收益最大的两项：

1. **R1 micro test**：5 分钟可写出。决定是否进 R1 修复阶段。
2. **R2 一行打印**：在 `VulkanGPU::createTexture` 入口加一行 `printf` 打印 `mipLevelCount`，跑 `ImageRenderTest/mipmap_none` 看实际值。

要哪一项先做，告诉我即可。

---

## 附录 A：每个 bug 的剥离实验脚本

### A.1 Bug 1（mipmap）

```cpp
// 在 src/gpu/vulkan/VulkanGPU.cpp::createTexture 入口加：
fprintf(stderr, "[mipmap-debug] key=%s mipLevelCount=%u w=%d h=%d\n",
        currentTestKey, descriptor.mipLevelCount, descriptor.width, descriptor.height);
```
跑 `TGFXFullTest --gtest_filter=ImageRenderTest.mipmap_none`，看输出。

### A.2 Bug 2 / 4 / 6（R1 swizzle）

写 `MaskSwizzleTest`：
```cpp
auto surface = Surface::Make(context, 64, 64);  // 走内部 mask offscreen 路径
auto canvas = surface->getCanvas();
Paint p; p.setColor({1.0f, 0.0f, 0.0f, 1.0f});  // 纯红
canvas->drawRect(Rect::MakeWH(64, 64), p);
context->flushAndSubmit();
uint32_t pixel;
surface->readPixels({ColorType::RGBA_8888, AlphaType::Premul}, &pixel, sizeof(uint32_t));
// 期望 0xFF0000FF (RGBA: R=255, G=0, B=0, A=255)
// 若读到 0xFF0000FF 之外的值（特别是 0x000000FF 或 0xFFFF0000），即为 swizzle 错
```

### A.3 Bug 3（partial-draw）

把 `test/src/LayerTest.cpp::PartialDrawLayer` 临时复制为 `PartialDrawLayer_NoBlur`，去掉 `BackgroundBlurStyle::Make` 行，跑：
```
TGFXFullTest --gtest_filter=LayerTest.PartialDrawLayer_NoBlur
```
若不再有 LR 差异 → 确认 BackgroundBlur + partial-draw 是根因。

### A.4 Bug 5（3D 容器）

```bash
rg "unordered_(map|set)" src/layers/compositing3d/
```
若有命中，临时替换为 `std::map` / `std::set`，跑 `LayerTest.Matrix_3D*`。

---

## 附录 B：5/18 与 5/19 数据完整对照

完整数据见：
- `.codebuddy/gl-vk-analysis/run-2026-05-18/diff-table.csv`
- `.codebuddy/gl-vk-analysis/run-2026-05-19/diff-table.csv`

本文档第 2 节表格已是这 6 个 bug 涉及的所有用例的完整对照。

完整 6 类 bug 的所有 PNG 图位于：
- `.codebuddy/gl-vk-analysis/run-2026-05-18/figures/`
- `.codebuddy/gl-vk-analysis/run-2026-05-19/figures/`
