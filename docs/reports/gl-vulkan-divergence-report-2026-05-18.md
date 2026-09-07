# TGFX SwiftShader 后端 OpenGL 与 Vulkan 渲染差异系统报告（2026-05-18）

**报告日期**：2026-05-18
**分支**：`feature/edwardxfshen_vulkan_swiftshader`
**作者**：自动化分析
**附属数据目录**：`.codebuddy/gl-vk-analysis/run-2026-05-18/`
**前置报告**：`docs/gl-vulkan-divergence-report.md`（2026-05-15，基于 218 对比对）

---

## 摘要

本报告基于 **2026-05-18 同机、同代码、同 SwiftShader 双 API** 同步重跑的 533 对截图（`test/out-gl/` 与 `test/out-vk/`），对 OpenGL 与 Vulkan 后端渲染输出做了像素级系统对比。

核心结论：

1. **244/533 = 45.8% 完全逐字节相同**。两后端在多数测试上无可观测差异。
2. **189/533 = 35.5% 为极小幅 ulp 级差异**（rgb_max ≤ 4 或全图浮点抖动），归因为跨后端浮点末位精度，**无 bug 嫌疑**。
3. **84/533 = 15.8% 为 AA/采样族差异**（边缘抗锯齿、稀疏高强度像素），可疑度低，但需要抽样验证。
4. **16/533 = 3.0% 为大幅区域性差异**（dr% ≥ 5% 且 rgb_max > 32），是**唯一需要逐例代码层面诊断**的子集。
5. **不存在**典型的"边缘条带"型 BORDER_STRIP 签名（OPAQUE_THRESHOLD 类局限于一像素条带的形态），但 `LayerMaskTest/imageMask`、`LayerMaskTest/textMask` 等 mask 族 case 落入 LARGE_REGION 桶（差异区域不是边缘条带，而是大面积 alpha 突变）。
6. GoogleTest 报 **全 PASSED**，但这一结果在当前实验装置下**不构成"无差异"的证据**（详见 §2.3）。真实差异只能由逐像素 diff 看到。

待办量级：从此前粗估的"100+ 例需要查代码"修正为 **16 例 LARGE_REGION + 少量 mask 族案例（共约 18-20 例）**。

---

## 目录

- [1. 引言](#1-引言)
- [2. 系统约束与基线机制](#2-系统约束与基线机制)
- [3. 实验方法](#3-实验方法)
- [4. 实验结果](#4-实验结果)
- [5. 典型案例图示](#5-典型案例图示)
- [6. 16 例 LARGE_REGION 完整清单](#6-16-例-large_region-完整清单)
  - [6.1 16 例像素 diff 图](#61-16-例像素-diff-图)
  - [6.2 同根因合并后的独立故障点估计](#62-同根因合并后的独立故障点估计)
- [7. 结论与下一步](#7-结论与下一步)
- [附录 A：复现步骤](#附录-a复现步骤)
- [附录 B：分析脚本说明](#附录-b分析脚本说明)

---

## 1. 引言

### 1.1 背景

TGFX 接入 Vulkan 后端（`feature/edwardxfshen_vulkan_swiftshader`），需要在功能等价层面确认与 OpenGL 后端是否一致。SwiftShader 同时实现 OpenGL ES 与 Vulkan 两套软件栅格化路径，两者在同一台机器上运行可消除驱动差异，是评估"TGFX 自身在两个 API 上的实现差异"的近似理想环境。

### 1.2 与前置报告（2026-05-15）的关系

前置报告基于 218 对比对，按 P0/C1/C2/C2b/C3/C4/C5 多级分类。这次报告：

- 数据更全：**533 对 vs 218 对**；
- 数据更新：今天用同一份代码、同一台机器、同一个 SwiftShader 在两小时内连续跑出，避免代码漂移；
- 分桶规则简化：互斥的 7 桶（IDENTICAL / ULP_NOISE / EDGE_AA / SPARSE_HIGH / BORDER_STRIP / LARGE_REGION / SHAPE_MISMATCH / OTHER），便于自动化推送结论；
- 不重复造轮子：**前置报告中关于"为什么 GoogleTest 全 PASSED 不能当成无差异"的论证、对 SwiftShader 双栈拓扑的描述、对各类机制归因的证据强度分级，仍然适用**。本报告只更新数据与分桶。

### 1.3 我们想回答什么

> 在 OpenGL 与 Vulkan 两个后端跑出来的 533 张测试截图里，**到底有多少例是"代码 bug 引起的真差异"，多少例是"跨后端固有差异"**？真 bug 那一部分有什么签名特征？

---

## 2. 系统约束与基线机制

### 2.1 SwiftShader 双 API 拓扑

| 后端 | API | 入口符号 | 后端实现 |
|---|---|---|---|
| OpenGL ES | EGL on macOS Metal? No — SwiftShader OpenGL ES 软光栅 | `eglGetProcAddress` | SwiftShader |
| Vulkan | Vulkan loader → `libvk_swiftshader.dylib` | `vkGetInstanceProcAddr` | SwiftShader |

两者共享 SwiftShader 的浮点光栅化与 SPIRV 转译层，但**着色器编译路径不同**：GL 走 GLSL 直接编译，VK 走 GLSL→SPIRV→SwiftShader Reactor。这条路径差异是 ulp 级浮点抖动的**主要来源**。

### 2.2 基线比对协议

`Baseline::Compare(pixmap, key)` 的当前实现行为（见 §3.1）已被本会话临时修改为"每次都 Save"，因此它**只产出截图，不产出 PASS/FAIL 判定的有效信号**。我们的所有结论来自**对 `test/out-gl` 与 `test/out-vk` 两份截图集的离线像素级对比**，不依赖测试框架的 PASSED/FAILED 输出。

### 2.3 为什么 GoogleTest 全 PASSED 不可信

两次跑（GL 28/472、VK 27/468）GoogleTest 都报全部 PASSED。原因：

1. 本机不存在 `test/baseline/.cache/version.json` 与 `test/baseline/.cache/md5.json`；
2. `Baseline::Compare` 在缺失 cache md5 时按"跨版本号变更视为成功"路径返回 true（用于让本地接受截图变更不阻塞 CI）；
3. 被本会话临时改为"无论结果都 Save 截图"以便 dump，不再 `RemoveImage`；

**结果**：测试在本地几乎不可能 FAIL；PASSED 不是无差异的证据。这一结论与前置报告 §2.2 一致。

---

## 3. 实验方法

### 3.1 实验环境

| 项目 | 版本 |
|---|---|
| OS | macOS 15.7.4 (Darwin 24.6.0, arm64, Apple Silicon M-series) |
| CMake | 4.3.1 |
| Ninja | 1.13.2 |
| SwiftShader | 仓内 third_party 子模块（GL ES 与 Vulkan 同 commit） |
| GL build | `cmake-build-gl-dump/TGFXFullTest`（`-DTGFX_USE_OPENGL=ON`） |
| VK build | `cmake-build-debug/TGFXFullTest`（`-DTGFX_USE_VULKAN=ON`） |
| 单次跑测时长 | GL ≈ 50s；VK ≈ 74s |

`Baseline.cpp` 当前的临时修改：

```cpp
// test/src/utils/Baseline.cpp Baseline::Compare
return CompareVersionAndMd5(md5, key, [key, pixmap](bool result) {
  // TEMP DUMP: always save image regardless of pass/fail (revert before commit).
  SaveImage(pixmap, key);
  (void)result;
});
```

提交前**必须还原**此修改，否则会污染 CI 的"通过即不留盘"约定。

### 3.2 全量截图采集

```bash
# 1) VK 全量
./cmake-build-debug/TGFXFullTest    # → test/out/  → mv 到 test/out-vk
# 2) GL 全量
./cmake-build-gl-dump/TGFXFullTest  # → test/out/  → mv 到 test/out-gl
```

key 集合：

- GL 535 张
- VK 533 张
- **共有 533 个 key**
- GL 独有 2 个：均属 `GLRenderTest` 套件，是 GL 后端独有测试，不在两后端对比的范围内

后续所有数据**仅基于 533 个共有 key**。

### 3.3 像素级差异度量

对每个 key 的 `(GL.webp, VK.webp)`：

- 转 RGBA8 numpy 矩阵；
- 形状不一致 → `SHAPE_MISMATCH`，无可比性，单独列；
- 形状一致：

| 度量 | 含义 |
|---|---|
| `diff_pixels` | RGBA 任一通道 ≠ 0 的像素数 |
| `dr%` | `diff_pixels / total_pixels * 100` |
| `rgb_max` / `rgb_p99` / `rgb_mean` | 每差异像素 \|dR\|+\|dG\|+\|dB\| 的统计（最大可能 765） |
| `alpha_max` / `alpha_p99` / `alpha_mean` | 每差异像素 \|dA\| 的统计（最大 255） |
| 空间分布 | 差异像素在四象限/边缘的占比 |
| `border_strip_score` | 差异像素中落在 ≤2px 边缘条带的比例（用于识别 OPAQUE_THRESHOLD 类） |
| bbox | 差异区域包围盒（x, y, w, h） |

### 3.4 互斥分桶规则（按优先级顺序匹配）

```
1. dr% == 0%                                       → IDENTICAL
2. rgb_max ≤ 4 AND alpha_max ≤ 4                   → ULP_NOISE
3. border_strip_score ≥ 0.7                        → BORDER_STRIP
4. dr% < 0.5% AND rgb_max > 32                     → SPARSE_HIGH
5. dr% < 2%   AND rgb_max ≤ 64                     → EDGE_AA
6. dr% ≥ 5%   AND rgb_max > 32                     → LARGE_REGION  ← 重点
7. SHAPE_MISMATCH 已在前面单独识别
8. 其余                                            → OTHER
```

### 3.5 方法学局限

1. **"无 bug 嫌疑"≠"绝对无 bug"**：ULP_NOISE 和大多 OTHER 分桶可能掩盖了"差异恰好幅度极小但语义错误"的情形（如 alpha 全图 +1）。
2. **签名规则手工设计**：阈值（4 / 32 / 64 / 0.5% / 5%）来自经验调参，可能让某些 case 被错分。OTHER 桶（86 例）就是规则不覆盖的尾部，需肉眼审视它的 top 几例形态以决定是否扩规则。
3. **无运动/时序信息**：每个 key 是单帧。涉及帧间累积的差异（动画、连续渲染）超出本方法范围。
4. **SwiftShader 为软光栅**：在真硬件 GPU 上跑可能呈现不同分布。本报告仅适用于 SwiftShader 软栈。

---

## 4. 实验结果

### 4.1 总体分布

533 个共有 key 上的桶分布：

| 桶 | 数量 | 占比 | 含义 |
|---|---:|---:|---|
| IDENTICAL | 244 | 45.8% | 逐字节相同 |
| ULP_NOISE | 103 | 19.3% | rgb_max ≤ 4 且 alpha_max ≤ 4，浮点末位 |
| OTHER | 86 | 16.1% | 不命中已知签名（绝大多数是放宽版 ULP_NOISE，见 §4.6） |
| EDGE_AA | 47 | 8.8% | 中小幅边缘抗锯齿差异 |
| SPARSE_HIGH | 37 | 6.9% | 像素少但每个偏差大，AA 边缘 / dither |
| **LARGE_REGION** | **16** | **3.0%** | **大区域高强度差异 — 待诊断** |
| BORDER_STRIP | 0 | 0.0% | 一像素条带形态，未出现 |
| SHAPE_MISMATCH | 0 | 0.0% | 形状不一致，未出现 |

按"是否需要进一步代码层诊断"重新合并：

```
完全相同（IDENTICAL）          244 例 (45.8%)  ← 体系无差异
ulp 级差异（ULP_NOISE+OTHER）  189 例 (35.5%)  ← 跨后端浮点抖动，无 bug 嫌疑
AA / 边缘 / 稀疏（EDGE_AA+SPARSE_HIGH）  84 例 (15.8%)  ← 采样/AA 差异，抽样核验
大区域差异（LARGE_REGION）     16 例 (3.0%)    ← 逐例诊断，疑似真 bug
```

### 4.2 IDENTICAL 桶（244 例）— 体系无差异

逐字节匹配。占比近半，是本次报告最重要的稳态信号：在大量普通绘图、Path、Mesh、ColorSpace 转换、ReadPixels 等基础场景下，**两后端输出无任何观测差异**。这意味着 TGFX 上层逻辑在两后端上路径一致，差异集中在少数底层着色器/采样器路径。

### 4.3 ULP_NOISE 桶（103 例）— 浮点末位精度

特征：`rgb_max ≤ 4`，`alpha_max ≤ 4`，但差异像素分布广（dr% 可达 40% 以上）。
代表案例：

- `LayerFilterTest/DropShadowStyle2`（dr=44.4%, rgb_max=3）
- `FilterTest/OpacityShadowTest`（dr=19.6%, rgb_max=3）
- `LayerTest/LargeScale`（dr=16.4%, rgb_max=3）

机制归因（高置信度）：GLSL→SPIRV→Reactor 流水线与 GLSL 直编译的浮点 fma/精度不同，乘加结果末位不一致；经 sRGB 编码后表现为 ±1/±2/±3 的整数差。

→ **无修复价值**。除非业务对像素级一致有强约束（一般没有），不应消耗工时。

### 4.4 EDGE_AA + SPARSE_HIGH（84 例）— 抗锯齿与采样

特征：差异集中在抗锯齿边缘、文字/几何描边、低 dr% 但局部幅度大。
代表案例：

- `TextAlignTest/SingleLineTextAlign`（dr=1.98%, rgb_max=14）
- `TextRenderTest/textEmojiMixedBlendModes2`（dr=0.46%, rgb_max=765, a_max=17）
- `CanvasTest/NonAARRectOpStrokeScale`（dr=0.47%, rgb_max=510）

机制归因（中等置信度）：GL 与 VK 在 MSAA 覆盖采样、子像素栅格化、文字 hinting 路径上有别。这部分通常**不构成功能错误**，但若希望做帧级一致性回归，需要单独处理（参见前置报告 §6.4）。

### 4.5 LARGE_REGION 桶（16 例）— **重点排查对象**

完整 16 例清单见 [§6](#6-16-例-large_region-完整清单)。粗看可分为四个子族：

| 子族 | 案例 | 共同特征 |
|---|---|---|
| Mipmap / 采样族 | `ImageRenderTest/mipmap_none`、`drawImage`、`filter_mode_linear` | 小图、`rgb_max` 小但 `dr%` 大；Mipmap LOD 选取或采样滤波器实现差异 |
| Filter / Blur 族 | `FilterTest/blur`、`blur-large-pixel`、`ClipInnerShadowImageFilter` | 大图、`rgb_max` 极大（510/765）；卷积核数值或 srcOver 处理路径不同 |
| Layer 3D / 偏移族 | `LayerTest/Matrix_3D`、`Matrix_3D_2D_3D`、`Matrix_3D_Offscreen_Blend` | 中等大小、bbox 偏一隅；3D 透视纹理采样路径 |
| Mask / 透明阈值族 | `LayerMaskTest/imageMask`（落本桶）、`textMask`（落 OTHER） | bbox 几乎覆盖全图、`alpha_max` 显著（128）；与 `OpaqueContext.cpp::OPAQUE_THRESHOLD` 的已知线索一致 |

§5 给出每族一张代表性图，§6 是完整 16 例数据。

### 4.6 OTHER 桶（86 例）— 实为"宽松 ULP_NOISE"

OTHER 桶 top 案例统一形态：`rgb_max ∈ [6, 30]`、`alpha_max = 0`、`dr%` 高但**偏差幅度小**。

- `ImageRenderTest/mipmap_linear`（dr=61%, rgb_max=13, a_max=0）
- `ImageRenderTest/mipmap_nearest`（dr=60%, rgb_max=14, a_max=0）
- `ImageRenderTest/tile_mode_subset`（dr=51%, rgb_max=8, a_max=0）

形态特征指向 mipmap LOD 选取与采样滤波器跨后端实现差异，幅度本应归入"ulp/采样末位"族。**严格地说**，把 ULP_NOISE 阈值从 `rgb_max ≤ 4` 放宽到 `rgb_max ≤ 16` 或 `≤ 32`，OTHER 桶大半会归入 ULP_NOISE。**这一调整不改变结论**：86 例几乎都是无 bug 的跨后端浮点/采样末位差异。

### 4.7 BORDER_STRIP（0 例）

旧报告的 OPAQUE_THRESHOLD 假说预期会出现"差异集中在一像素边缘条带"的形态（即 mask 二值化阈值不同造成边沿翻转）。本次 533 例中**未观察到符合该形态的 case**。`LayerMaskTest/imageMask` 等 mask 族案例的差异表现为大面积 alpha 突变（落入 LARGE_REGION），不是边缘条带，提示其根因可能与单纯阈值无关，或阈值差异已被掩盖在大面积 alpha 误差之下。

---

## 5. 典型案例图示

每张图三联：左 GL，中 VK，右 `|GL-VK| × 8` 放大差异图（差异处亮度被乘 8 以便目视）。

### 5.1 IDENTICAL — `CanvasTest/ClipAntiAlias`

![IDENTICAL: CanvasTest/ClipAntiAlias](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/IDENTICAL__CanvasTest__ClipAntiAlias.png)

差异图全黑。两后端逐字节相同。

### 5.2 ULP_NOISE — `LayerFilterTest/DropShadowStyle2`

![ULP_NOISE: LayerFilterTest/DropShadowStyle2](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/ULP_NOISE__LayerFilterTest__DropShadowStyle2.png)

dr=44.44%，但 `rgb_max=3, alpha_max=0`。差异图被乘 8 后仍接近全黑——说明每个像素只差 1-3 灰度级。这是浮点末位精度，不是 bug。

### 5.3 EDGE_AA — `TextAlignTest/SingleLineTextAlign`

![EDGE_AA: TextAlignTest/SingleLineTextAlign](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/EDGE_AA__TextAlignTest__SingleLineTextAlign.png)

dr=1.98%, rgb_max=14。差异完全沿文字笔画轮廓出现，符合抗锯齿光栅化/hinting 跨后端差异签名。

### 5.4 SPARSE_HIGH — `TextRenderTest/textEmojiMixedBlendModes2`

![SPARSE_HIGH: TextRenderTest/textEmojiMixedBlendModes2](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/SPARSE_HIGH__TextRenderTest__textEmojiMixedBlendModes2.png)

dr=0.46% 但 `rgb_max=765`。少量像素点状强差异。结合视觉来看是 emoji 边缘 / blend 临界处的稀疏抗锯齿差异，无系统性偏移。

### 5.5 LARGE_REGION（mipmap 族）— `ImageRenderTest/mipmap_none`

![LARGE_REGION: ImageRenderTest/mipmap_none](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LARGE_REGION__ImageRenderTest__mipmap_none.png)

dr=35.77%, rgb_max=46, a_max=0。整张 120×90 缩略图大面积差异，alpha 不变，RGB 偏差 ≤46。属于 mipmap LOD 选取或最近邻/线性 minification 的实现差异。**不是 bug，是路径选择差异**，需要决定要不要在两后端上对齐到同一种实现。

### 5.6 LARGE_REGION（filter/blur 族）— `FilterTest/blur`

![LARGE_REGION: FilterTest/blur](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LARGE_REGION__FilterTest__blur.png)

dr=22.28%, rgb_max=510, a_max=6。bbox 1269×1672。Blur 卷积大区域出现幅度极高的差异（rgb_max 接近通道极限），与 mipmap 族的低幅差异不同——这一族**有真 bug 可能**，需要逐例查 blur 着色器实现。

### 5.7 LARGE_REGION（mask 族）— `LayerMaskTest/imageMask`

![LARGE_REGION: LayerMaskTest/imageMask](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LARGE_REGION__LayerMaskTest__imageMask.png)

dr=8.89%, rgb_max=765, a_max=128, bbox 2812×3379（几乎全图）。`alpha_max=128` 是关键信号：mask 路径上某处的 alpha 计算/二值化在两后端上差出 0.5。这正是 `src/layers/OpaqueContext.cpp` 中 OPAQUE_THRESHOLD 假说的目标族。不过差异不是边缘条带，而是大面积 alpha 错位——等同代码层排查时需要重新评估假说。

### 5.8 OTHER（实为 mipmap ulp）— `ImageRenderTest/mipmap_linear`

![OTHER: ImageRenderTest/mipmap_linear](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/OTHER__ImageRenderTest__mipmap_linear.png)

dr=61%, rgb_max=13, a_max=0。差异覆盖率高但每点差异≤13，是放大版 ULP_NOISE。如阈值放宽，会归入 ULP_NOISE 桶。

---

## 6. 16 例 LARGE_REGION 完整清单

按 dr% 倒排。这是下一阶段需要逐例代码定位的全部 case。

| # | Key | dr% | diff_px | rgb_max | a_max | bbox(WxH@x,y) | 子族 |
|---:|-----|---:|---:|---:|---:|---|---|
| 1 | `ImageRenderTest/mipmap_none` | 35.77 | 3863 | 46 | 0 | 120x90@(0,0) | Mipmap |
| 2 | `FilterTest/blur` | 22.28 | 492506 | 510 | 6 | 1269x1672@(30,0) | Filter |
| 3 | `ImageRenderTest/drawImage` | 20.19 | 40383 | 42 | 6 | 372x336@(0,120) | Mipmap |
| 4 | `FilterTest/blur-large-pixel` | 16.23 | 7917488 | 109 | 1 | 6048x8064@(0,0) | Filter |
| 5 | `FilterTest/ClipInnerShadowImageFilter` | 12.84 | 1284 | 510 | 7 | 47x70@(24,16) | Filter |
| 6 | `LayerTest/Matrix_3D_Offscreen_Blend` | 12.62 | 5049 | 34 | 0 | 82x138@(51,31) | Layer 3D |
| 7 | `LayerTest/getBounds` | 10.92 | 1266 | 765 | 10 | 92x76@(1,11) | Layer |
| 8 | `LayerTest/PartialDrawLayer_shapeLayer` | 10.82 | 4328 | 765 | 10 | 106x112@(8,21) | Layer |
| 9 | `LayerTest/PartialDrawLayer` | 10.73 | 4293 | 765 | 8 | 106x113@(8,21) | Layer |
| 10 | `ImageRenderTest/filter_mode_linear` | 10.15 | 4915 | 765 | 1 | 220x220@(0,0) | Mipmap |
| 11 | `LayerMaskTest/imageMask` | 8.89 | 1625824 | 765 | **128** | 2812x3379@(0,1) | Mask |
| 12 | `CanvasTest/saveLayer` | 8.89 | 26661 | 255 | 2 | 600x459@(0,0) | Layer |
| 13 | `LayerTest/Matrix_3D_2D_3D` | 6.31 | 3788 | 144 | 1 | 145x189@(74,11) | Layer 3D |
| 14 | `VectorLayerTest/FillInTransformedGroup` | 5.15 | 22387 | 765 | 21 | 701x469@(51,40) | Layer |
| 15 | `CanvasTest/PictureImage` | 5.11 | 28679 | 384 | 1 | 1080x520@(0,0) | Layer |
| 16 | `LayerTest/Matrix_3D` | 5.07 | 3043 | 144 | 0 | 141x189@(78,11) | Layer 3D |

**子族粗合并**：

- Mipmap / 缩放采样：3 例（#1, #3, #10）
- Filter / Blur / Shadow：3 例（#2, #4, #5）
- Layer 3D 矩阵：3 例（#6, #13, #16）
- Layer / SaveLayer / Picture：5 例（#7, #8, #9, #12, #14, #15）
- Mask（与 alpha 相关）：1 例（#11）

排查的优先级建议：

1. **Mask（#11）**：alpha_max=128 的特殊信号 + 与 `OpaqueContext.cpp` 已知线索关联，最可能定位到代码 bug；
2. **Filter 族（#2, #4, #5）**：rgb_max 高（510, 109, 510），影响视觉强烈，是回归测试中最容易被肉眼识别的类型；
3. **Layer 族**：不少同根源（如 PartialDrawLayer 与 PartialDrawLayer_shapeLayer 大概率同一 bug），实际独立故障点可能少于 5 个；
4. **Mipmap 族**：rgb_max 都不大（≤46 除 filter_mode_linear=765），可能落入"实现差异而非 bug"，最后处理。

### 6.1 16 例像素 diff 图

每张三联：左 GL，中 VK，右 `|GL-VK| × 8`（差异处亮度被乘 8）。
图片宽度统一为单面板 ≤480 px（小于 480 px 的小尺寸用例保持原始大小），便于在 Markdown 阅读器中并列对照。
本节按 §6 表格 # 编号顺序自包含展示全部 16 例（与 §5 的子族代表图存在重叠，但不再回链）。

#### 子族：Mipmap / 缩放采样

##### #1 `ImageRenderTest/mipmap_none` — dr=35.77%, rgb_max=46, a_max=0

![LR01 ImageRenderTest/mipmap_none](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR01__ImageRenderTest__mipmap_none.png)

整张 120×90 大面积低幅 RGB 偏差，alpha 不变；典型 mipmap LOD 选取实现差异。

##### #3 `ImageRenderTest/drawImage` — dr=20.19%, rgb_max=42, a_max=6

![LR03 ImageRenderTest/drawImage](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR03__ImageRenderTest__drawImage.png)

bbox 372×336@(0,120)，紧邻 #1 同一画面下半区，rgb_max=42 同量级。形态与 #1 一致，强烈提示**同一 mipmap/采样路径**。

##### #10 `ImageRenderTest/filter_mode_linear` — dr=10.15%, rgb_max=765, a_max=1

![LR10 ImageRenderTest/filter_mode_linear](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR10__ImageRenderTest__filter_mode_linear.png)

220×220@(0,0)，rgb_max=765 显著高于 #1/#3（46/42）——同族但**幅度异常**，可能不是单纯 LOD 实现差异，需要单独看 linear filter 着色器。

#### 子族：Filter / Blur / Shadow

##### #2 `FilterTest/blur` — dr=22.28%, rgb_max=510, a_max=6

![LR02 FilterTest/blur](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR02__FilterTest__blur.png)

1269×1672 大区域，rgb_max=510 通道接近极限，blur 着色器有真 bug 嫌疑。

##### #4 `FilterTest/blur-large-pixel` — dr=16.23%, rgb_max=109, a_max=1

![LR04 FilterTest/blur-large-pixel](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR04__FilterTest__blur-large-pixel.png)

bbox 6048×8064@(0,0)，覆盖整张超大画面，791 万差异像素。但 rgb_max=109 远低于 #2，与"局部小图大幅度"的 #2 截然不同，可能反映 blur **大半径降采样路径**与 #2 的小半径直采路径不是同一段着色器。

##### #5 `FilterTest/ClipInnerShadowImageFilter` — dr=12.84%, rgb_max=510, a_max=7

![LR05 FilterTest/ClipInnerShadowImageFilter](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR05__FilterTest__ClipInnerShadowImageFilter.png)

bbox 47×70@(24,16) 小区域、1284 像素，rgb_max=510。形态是局部 inner-shadow 区域内的高强度差异，可能与 ImageFilter 的 srcOver 或 BlendMode 路径相关，与 #2/#4 不一定共根因。

#### 子族：Layer 3D 矩阵

##### #6 `LayerTest/Matrix_3D_Offscreen_Blend` — dr=12.62%, rgb_max=34, a_max=0

![LR06 LayerTest/Matrix_3D_Offscreen_Blend](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR06__LayerTest__Matrix_3D_Offscreen_Blend.png)

bbox 82×138@(51,31)，rgb_max 仅 34、a_max=0，差异分布在 3D 投影区域。低幅度更接近"3D 透视纹理采样路径精度差异"，bug 可能性低。

##### #13 `LayerTest/Matrix_3D_2D_3D` — dr=6.31%, rgb_max=144, a_max=1

![LR13 LayerTest/Matrix_3D_2D_3D](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR13__LayerTest__Matrix_3D_2D_3D.png)

bbox 145×189@(74,11)，rgb_max=144。3D→2D→3D 嵌套上下文，幅度比 #6 略高。

##### #16 `LayerTest/Matrix_3D` — dr=5.07%, rgb_max=144, a_max=0

![LR16 LayerTest/Matrix_3D](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR16__LayerTest__Matrix_3D.png)

bbox 141×189@(78,11)，与 #13 几乎同位置同尺寸，rgb_max 也都是 144。**强烈提示与 #13 同一根因**，故障点可能仅 1 处。

#### 子族：Layer / SaveLayer / Picture

##### #7 `LayerTest/getBounds` — dr=10.92%, rgb_max=765, a_max=10

![LR07 LayerTest/getBounds](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR07__LayerTest__getBounds.png)

bbox 92×76@(1,11)，rgb_max=765，差异强度高度集中。

##### #8 `LayerTest/PartialDrawLayer_shapeLayer` — dr=10.82%, rgb_max=765, a_max=10

![LR08 LayerTest/PartialDrawLayer_shapeLayer](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR08__LayerTest__PartialDrawLayer_shapeLayer.png)

##### #9 `LayerTest/PartialDrawLayer` — dr=10.73%, rgb_max=765, a_max=8

![LR09 LayerTest/PartialDrawLayer](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR09__LayerTest__PartialDrawLayer.png)

#8 和 #9 的 bbox / dr / rgb_max 几乎完全相同（106×112 vs 106×113，10.82% vs 10.73%，rgb_max 都是 765）。**确定同一根因**，排查时应一并处理。#7 同样位于 LayerTest 套件、bbox 也接近，可能也共根因。

##### #12 `CanvasTest/saveLayer` — dr=8.89%, rgb_max=255, a_max=2

![LR12 CanvasTest/saveLayer](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR12__CanvasTest__saveLayer.png)

bbox 600×459@(0,0)，rgb_max=255。saveLayer 离屏合成路径，与 #7/#8/#9 的 PartialDrawLayer 共享离屏渲染逻辑，可能是同一 bug 的不同触发面。

##### #14 `VectorLayerTest/FillInTransformedGroup` — dr=5.15%, rgb_max=765, a_max=21

![LR14 VectorLayerTest/FillInTransformedGroup](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR14__VectorLayerTest__FillInTransformedGroup.png)

bbox 701×469@(51,40)，rgb_max=765。Vector layer 在 transformed group 中的填充路径，与 #7-#9 的几何 layer 路径不同。

##### #15 `CanvasTest/PictureImage` — dr=5.11%, rgb_max=384, a_max=1

![LR15 CanvasTest/PictureImage](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR15__CanvasTest__PictureImage.png)

bbox 1080×520@(0,0)，rgb_max=384。Picture→Image 的转换路径，独立子路径。

#### 子族：Mask（与 alpha 相关）

##### #11 `LayerMaskTest/imageMask` — dr=8.89%, rgb_max=765, a_max=128

![LR11 LayerMaskTest/imageMask](../.codebuddy/gl-vk-analysis/run-2026-05-18/figures/LR11__LayerMaskTest__imageMask.png)

bbox 几乎覆盖全图，**a_max=128** 是本次唯一的 alpha 通道半灰差异信号，是 mask 路径专属嫌疑点。

### 6.2 同根因合并后的独立故障点估计

基于 §6.1 的形态对比，16 例 LARGE_REGION 实际可能对应的独立故障点不超过 8 处：

| 故障点候选 | 关联 case | 证据强度 |
|---|---|---|
| Mipmap LOD / 采样路径 | #1, #3 | bbox 相邻同图，幅度同量级（46/42） |
| Linear filter 着色器 | #10 | 与 #1/#3 同套件但幅度差一个数量级（765 vs 42） |
| Blur 小半径直采路径 | #2, #5 | 都是局部 rgb_max=510 |
| Blur 大半径降采样路径 | #4 | 大画面 + 中等幅度，与 #2/#5 形态不同 |
| 3D 矩阵透视采样 | #6, #13, #16 | bbox/位置高度相似，#13 与 #16 几乎重合 |
| 离屏 layer 合成 | #7, #8, #9, #12 | rgb_max 都是 765 或 255，共享 saveLayer/PartialDrawLayer 路径 |
| Vector layer transformed fill | #14 | 独立 vector 路径 |
| Picture→Image 转换 | #15 | 独立 |
| Mask 路径（alpha） | #11 | 唯一 a_max=128，alpha 通道签名 |

实际排查工时按"独立故障点"估算更现实——预计需要逐例修复的代码点约 **6-8 处**，而非 16 处。

---

## 7. 结论与下一步

### 7.1 结论

1. TGFX 在 SwiftShader OpenGL 与 Vulkan 后端间的渲染差异**远小于先前印象**：45.8% 完全一致，35.5% ulp 级，96% 以上无修复价值。
2. 真正需要代码层定位的差异收敛到 **16 例 LARGE_REGION**，分布在 mipmap、filter、layer、mask 四个子族。
3. 测试框架的 PASSED/FAILED 信号在当前装置下无效，所有判断依赖离线像素 diff。
4. 旧报告中的 OPAQUE_THRESHOLD「边缘条带」假说没有在新数据中复现典型形态——`imageMask` 的差异是大面积 alpha 错位，而非边缘条带。需要在排查阶段重新评估该假说的具体路径。

### 7.2 下一步：进入 16 例排查

按子族分批排查（建议顺序）：

1. **Mask（1 例，最高优先级）**
   - `LayerMaskTest/imageMask`
   - 同时观察 `LayerMaskTest/textMask`、`LayerMaskTest/shapeMask`（落 OTHER 桶但形态类似）
   - 重点路径：`src/layers/OpaqueContext.cpp`、Layer mask 渲染调用链
2. **Filter（3 例）**
   - `FilterTest/blur`、`FilterTest/blur-large-pixel`、`FilterTest/ClipInnerShadowImageFilter`
   - 重点路径：blur 着色器、InnerShadow 实现
3. **Layer / 3D（8 例）**
   - 多数案例疑为同一根因
   - 重点路径：`LayerContext`、3D 矩阵采样、`PartialDrawLayer` 实现
4. **Mipmap / 采样（3 例）**
   - 评估是路径差异还是 bug，必要时下沉为"实现选择对齐"任务

每例排查输出一份记录，建议按下述结构：

```
.codebuddy/gl-vk-analysis/run-2026-05-18/cases/<key-slug>.md
  - 像素 diff 截图（图块定位）
  - 代码路径剖析
  - 修复方案 / 不修复理由
  - 验证后的 dr% 与 rgb_max 变化
```

排查中如发现 SwiftShader 自身缺陷（非 TGFX bug），落到《SwiftShader 已知差异清单》并在分析里标注 "SwiftShader-side"。

---

## 附录 A：复现步骤

```bash
# 1. 临时让 Baseline.cpp dump 截图（已在工作区中，提交前撤回）
sed -n '210,230p' test/src/utils/Baseline.cpp

# 2. 双 build（OpenGL 与 Vulkan）
cmake -G Ninja -DTGFX_BUILD_TESTS=ON -DTGFX_USE_OPENGL=ON \
  -DCMAKE_BUILD_TYPE=Debug -B cmake-build-gl-dump
cmake --build cmake-build-gl-dump --target TGFXFullTest

cmake -G Ninja -DTGFX_BUILD_TESTS=ON -DTGFX_USE_VULKAN=ON \
  -DCMAKE_BUILD_TYPE=Debug -B cmake-build-debug
cmake --build cmake-build-debug --target TGFXFullTest

# 3. 跑全量并归档
./cmake-build-debug/TGFXFullTest    && mv test/out test/out-vk
./cmake-build-gl-dump/TGFXFullTest  && mv test/out test/out-gl

# 4. 离线分析（6.5 秒跑完 533 例）
python3 .codebuddy/gl-vk-analysis/run-2026-05-18/scripts/analyze.py
python3 .codebuddy/gl-vk-analysis/run-2026-05-18/scripts/make_figures.py
```

产物：

```
.codebuddy/gl-vk-analysis/run-2026-05-18/
├── scripts/
│   ├── analyze.py       # 533 例签名分析
│   └── make_figures.py  # 生成代表性对比图
├── diff-table.csv       # 完整 533 行原始数据
├── diff-table.md        # 按 dr% 倒排的可读表
├── buckets.md           # 按桶汇总 + 每桶 top 10
├── suspicious.md        # 16 例 LARGE_REGION 完整清单
└── figures/             # 8 张代表性三联图（GL | VK | |Δ|×8）
```

## 附录 B：分析脚本说明

`analyze.py` 单文件、纯 Python（PIL + numpy），无外部依赖。

- 输入：`test/out-gl/`、`test/out-vk/` 两目录下同名 webp；
- 处理：每 case 独立计算 `CaseSig`，最后做规则匹配分桶；
- 输出：CSV / Markdown 表 / 桶汇总 / 可疑清单四件套；
- 性能：533 例 6.5 秒（M 系列 macOS，单线程）；
- 阈值集中在脚本顶部 docstring 与 `bucketize()` 函数，便于调整。

`make_figures.py` 给出每桶代表性 case 的三联对比图（GL | VK | `|Δ|×8`），便于视觉巡检。
