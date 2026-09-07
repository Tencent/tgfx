# 截图测试容差机制行业调研

**日期**：2026-05-19  
**目的**：调研业内图形渲染引擎/浏览器/UI 框架如何处理多后端、多平台下的像素级截图测试容差问题，为 TGFX 的截图测试系统升级提供参考。

---

## 1. 问题背景

TGFX 当前使用**逐像素精确比较**（`Baseline::Compare`）。在 GL/VK 两个后端共存后，由于 SwiftShader 内部精度差异，54% 的用例（289/533）产生不可避免的像素偏差。现有系统无法区分"正常精度差异"和"真实渲染 bug"。

核心需求：
1. 同后端回归检测（检测代码变更引入的劣化）
2. 跨后端差异监控（GL/VK 差异是否在合理范围内）
3. 避免误报（精度差异不应阻塞 CI）
4. 避免漏报（真正的 bug 必须被发现）

---

## 2. 行业方案综述

### 2.1 Chromium / Skia Gold

**产品**：Chromium 浏览器 GPU 像素测试  
**规模**：每个 commit 产生 >500,000 张截图  
**核心工具**：Skia Gold（Web 服务 + `goldctl` CLI）

#### 核心设计理念

**"多 baseline 优于 fuzzy matching"**——Gold 的哲学是：大多数测试在不同配置下只会产生 2~3 种视觉上不可区分的输出。与其配一个 fuzzy 阈值（可能误放过坏图），不如**把所有合法变体都 approve 为 baseline**。

#### 具体机制

| 机制 | 描述 |
|---|---|
| **多 baseline per test** | 同一个测试可以有 2~3 张被 approve 的 golden image，只要匹配任意一张就 PASS |
| **Per-configuration baseline** | 每个测试按 (OS, GPU 型号, 驱动版本) 维度独立管理 baseline |
| **Fuzzy matching（可选）** | 对噪声敏感的测试启用模糊比较 |
| **Sobel edge filter（可选）** | 只比较非边缘区域，忽略抗锯齿差异 |
| **Triage workflow** | 新图出现时 CI 不直接 FAIL，而是标记为 "untriaged"，人工 approve 后即时生效 |

#### Fuzzy Matching 参数（Skia Gold）

```go
type Matcher struct {
    MaxDifferentPixels            int  // 允许差异的最大像素数
    PixelDeltaThreshold           int  // 单像素 RGBA 差值总和上限（0~1020）
    PixelPerChannelDeltaThreshold int  // 单像素单通道差值上限（0~255）
    IgnoredBorderThickness        int  // 忽略图像边缘 N 像素
}
```

**两种比较策略**（互斥，只选其一）：
1. **Sum-based**：`|ΔR| + |ΔG| + |ΔB| + |ΔA| ≤ PixelDeltaThreshold`
2. **Max-per-channel**：`max(|ΔR|, |ΔG|, |ΔB|, |ΔA|) ≤ PixelPerChannelDeltaThreshold`

**判定逻辑**：
```
PASS 当且仅当:
  1. 两图尺寸相同
  2. 超出阈值的像素数 ≤ MaxDifferentPixels
  3. 任何像素的 delta 都不超过阈值
  4. 边缘 N 像素被排除在比较之外
```

#### Sobel Edge Filter 参数

```python
class SobelMatchingAlgorithm(FuzzyMatchingAlgorithm):
    edge_threshold: int  # 0~254，控制 Sobel 边缘检测灵敏度
    # 0 = 最激进（大部分像素被排除）
    # 254 = 最保守（几乎不排除）
    # 被 Sobel 标记为"边缘"的像素不参与比较
```

**用途**：对抗锯齿敏感的测试（如旋转文字、曲线边缘），只比较纯色/纯纹理区域，忽略所有边缘像素。

#### 优缺点

| ✅ 优势 | ❌ 劣势 |
|---|---|
| 多 baseline 处理同一测试的合法变体 | 需要独立 Web 服务（Gold server） |
| Per-config baseline 天然适配多后端 | Triage workflow 需要人工介入 |
| Fuzzy + Sobel 灵活组合 | 参数调优需要辅助脚本 |
| 新图不阻塞 CI（只标 untriaged） | 500k+ 图像量需要大量存储 |

#### 参数调优工具

Chromium 提供脚本自动寻找最优参数：
```
content/test/gpu/gold_inexact_matching/determine_gold_inexact_parameters.py
  --algorithm binary_search  # 二分搜索单参数
  --algorithm local_minima   # 多参数联合优化
```

---

### 2.2 WebRender（Mozilla/Servo）

**产品**：Firefox 渲染引擎 WebRender  
**规模**：数百个 reftest  
**核心工具**：Wrench reftest harness

#### 核心设计

使用 **fuzzy annotation** 直接在测试清单中标注每个测试的容差：

```
fuzzy(max_diff, num_pixels) == test.yaml reference.png
```

| 参数 | 含义 |
|---|---|
| `max_diff` | 单像素单通道允许的最大差值（0~255） |
| `num_pixels` | 允许差异的最大像素数 |

#### 示例

```bash
# 精确比较
== basic-rect.yaml basic-rect-ref.png

# 允许最多 10 个像素差 1 级
fuzzy(1,10) == blur-test.yaml blur-ref.png

# 允许最多 50 个像素差 3 级（模糊效果）
fuzzy(3,50) == box-shadow.yaml box-shadow-ref.png

# 平台限定
platform(linux,mac) fuzzy(2,20) == text-test.yaml text-ref.png

# 跳过特定平台
skip_on(android) == complex-blend.yaml complex-blend-ref.png
```

#### 多后端处理

| 机制 | 描述 |
|---|---|
| **Platform filtering** | `platform(linux,mac)` 限定测试只在特定平台运行 |
| **Per-platform fuzzy** | 同一测试可以在不同平台有不同的 fuzzy 参数 |
| **Rendering options** | `options(disable-subpixel)` 禁用子像素 AA 消除平台差异源 |
| **Backend selection** | Wrench 支持 hardware GL vs software SWGL 两个后端运行对比 |

#### 优缺点

| ✅ 优势 | ❌ 劣势 |
|---|---|
| 极简——不需要外部服务 | 参数需要人工为每个测试选定 |
| 容差与测试代码同源管理 | 无法自动学习"合理"阈值 |
| 平台过滤灵活 | 只支持 per-channel max，不支持 sum/percentile |
| 透明——一看清单就知道容差 | 大量测试时手写 fuzzy annotation 工作量大 |

---

### 2.3 Slint UI

**产品**：Slint 跨平台 UI 框架  
**规模**：数百个 widget 截图测试  
**核心工具**：自定义 Rust 测试框架

#### 核心设计

**Per-backend reference image + 欧几里得色彩距离 + per-test 可配阈值**

```
tests/screenshots/references/software/   ← 软件渲染器 baseline
tests/screenshots/references/skia/       ← Skia(GL/VK) 渲染器 baseline
```

#### 比较算法

```rust
fn color_difference(a: Rgba8Pixel, b: Rgba8Pixel) -> f64 {
    let dr = (a.r as f64 - b.r as f64);
    let dg = (a.g as f64 - b.g as f64);
    let db = (a.b as f64 - b.b as f64);
    let da = (a.a as f64 - b.a as f64);
    (dr*dr + dg*dg + db*db + da*da).sqrt()  // 欧几里得 RGBA 距离
}
```

**判定**：任何像素的 `color_difference` 超过阈值 → FAIL

#### 阈值配置

```rust
struct TestCaseOptions {
    base_threshold: f64,       // 普通渲染的每像素最大距离
    rotation_threshold: f64,   // 旋转渲染的阈值（通常更高）
}
```

在 `.slint` 源文件中通过注释设置：
```
// BASE_THRESHOLD=5.0
// ROTATION_THRESHOLD=10.0
```

#### 多后端处理

1. **每个后端独立 baseline**——不做跨后端对比
2. **测试只对比自己后端的 reference**
3. **新 reference 自动生成**：`SLINT_CREATE_SCREENSHOTS=1` 环境变量

#### 优缺点

| ✅ 优势 | ❌ 劣势 |
|---|---|
| 后端隔离——不纠缠跨后端差异 | 需要为每个后端维护一套 reference |
| 欧几里得距离比单通道 max 更稳定 | 欧几里得距离对极端单通道错误不敏感 |
| Per-test 阈值灵活 | 阈值需要人工调 |
| Rotation-aware | 实现复杂度中等 |

---

### 2.4 Flutter Golden Tests

**产品**：Flutter UI 框架  
**规模**：thousands of golden files  
**核心工具**：`flutter_test` + `GoldenFileComparator`

#### 核心设计

**默认精确比较 + 可选 `TolerantGoldenFileComparator`**

```dart
// 默认：精确比较
await expectLater(find.byType(MyWidget), matchesGoldenFile('my_widget.png'));

// 带容差：
class TolerantGoldenFileComparator extends LocalFileComparator {
  @override
  Future<bool> compare(Uint8List imageBytes, Uri golden) async {
    // 计算像素差异比例
    double diffPercent = computeDiff(imageBytes, goldenBytes);
    return diffPercent <= threshold;  // threshold 通常 0.5%~2%
  }
}
```

#### 阈值策略

| 参数 | 含义 | 典型值 |
|---|---|---|
| `threshold` | 差异像素占总像素的百分比上限 | 0.5%~2% |

**特点**：Flutter 的容差是一个单一的 `diffPercent` ≤ threshold，**不区分每像素差异大小**。这意味着 1 个像素差 255 和 100 个像素差 1 被同等对待——比较粗糙。

#### 多后端/多平台处理

- Flutter 的 golden test 主要面对 **字体渲染差异**（不同平台的字体引擎）
- 解法：**在 CI 容器中固定字体 + 固定渲染器版本**（环境确定性优先于容差放宽）
- 如需跨平台：为每个平台维护独立 golden 文件

#### 优缺点

| ✅ 优势 | ❌ 劣势 |
|---|---|
| API 简洁（一个 threshold） | 不区分像素差异程度 |
| 环境确定性理念正确 | 阈值太粗——可能漏过真 bug |
| 社区生态好（golden_toolkit 等） | 无 per-test 精细配置 |

---

### 2.5 dEQP（Khronos OpenGL/Vulkan 一致性测试）

**产品**：GPU 驱动一致性测试套件  
**规模**：200,000+ 测试用例  
**核心工具**：`deqp-vk` / `deqp-gles3`

#### 核心设计

**fuzzyCompare + surface threshold + per-test tolerance**

```cpp
// framework/common/tcuImageCompare.cpp
bool fuzzyCompare(const tcu::Surface& reference, const tcu::Surface& result,
                  float threshold, CompareLogMode logMode) {
    // 对每个像素：计算 per-channel 差值
    // 用 5x5 邻域做局部比较（抗锯齿容忍）
    // 如果邻域内有匹配的像素，则该像素算通过
    // 最终统计超阈值比例 < threshold → PASS
}
```

#### 关键特性

| 特性 | 描述 |
|---|---|
| **邻域比较** | 不是逐像素精确对位，而是在 3×3 或 5×5 邻域内寻找匹配——容忍 ±1~2 像素的位移（AA 差异） |
| **浮点阈值** | `threshold` 是 0.0~1.0 的浮点数，代表"允许超标像素的比例"而非"单像素差值" |
| **Per-test 配置** | 每个测试用例可以 override 默认阈值 |
| **多比较方法** | `pixelThresholdCompare`（绝对阈值）、`fuzzyCompare`（邻域+比例）、`bilinearCompare`（带插值容忍） |

#### 多后端处理

- dEQP 的设计前提是"**同一 API spec 的不同实现应该产出相同结果**"
- 允许误差来源：浮点精度、AA 实现差异、纹理过滤实现
- **不做跨 API 比较**（GLES 和 VK 有各自的测试集和 baseline）

---

### 2.6 Playwright / Cypress（前端 UI 测试）

**产品**：Web 端截图测试工具  
**规模**：面向 Web UI 的回归测试

#### Playwright 的参数模型

```typescript
await expect(page).toHaveScreenshot('name.png', {
  threshold: 0.2,           // per-pixel YIQ 色彩距离容差（0~1）
  maxDiffPixels: 100,       // 允许差异的最大像素数（绝对值）
  maxDiffPixelRatio: 0.01,  // 允许差异的最大像素比例
});
```

| 参数 | 含义 | 关键注意 |
|---|---|---|
| `threshold` | **per-pixel** YIQ 颜色空间感知差异（NOT 差异像素比例） | 常被误解为全局比例 |
| `maxDiffPixels` | 超出 threshold 的像素数上限 | 与 maxDiffPixelRatio 二选一 |
| `maxDiffPixelRatio` | 超出 threshold 的像素占比上限 | 与 maxDiffPixels 二选一 |

#### 底层算法：pixelmatch

```
1. 将像素转为 YIQ NTSC 色彩空间
2. 计算感知色彩差异（luminance-weighted）
3. 检测抗锯齿像素（Vyšniauskas 2009 算法）
4. AA 像素默认排除在 diff 计数之外
5. 非 AA 像素：若 YIQ 差异 > threshold → 标记为 diff
6. 若 diff 像素数 > maxDiffPixels → FAIL
```

**性能**：~28ms/1440×900 图

#### 优缺点

| ✅ 优势 | ❌ 劣势 |
|---|---|
| YIQ 感知色彩空间比原始 RGB 更符合人眼 | 对图形渲染不够专业（面向 Web UI） |
| AA 像素自动检测排除 | 不支持 per-test 阈值（只能全局） |
| maxDiffPixels 绝对值控制 | 无邻域搜索能力 |

---

### 2.7 ODiff（高性能替代）

**产品**：pixelmatch 的 SIMD 加速替代品  
**语言**：Zig (原 OCaml)

| 指标 | pixelmatch | ODiff |
|---|---|---|
| 速度 (1440×900) | ~28ms | ~3-4ms (8× faster) |
| 算法 | YIQ + AA detection | 同 pixelmatch |
| SIMD | ❌ | ✅ SSE2/AVX2/AVX512/NEON |
| 适用场景 | 小中规模 | 高并发 CI (1000+ screenshots/run) |

---

## 3. 方案对比矩阵

| 维度 | Skia Gold | WebRender | Slint | Flutter | dEQP | Playwright |
|---|---|---|---|---|---|---|
| **比较单位** | per-pixel | per-pixel | per-pixel | per-image | per-pixel + 邻域 | per-pixel |
| **色彩空间** | RGB/RGBA | RGB/RGBA | RGBA 欧几里得 | 未知 | RGB/RGBA | YIQ |
| **阈值粒度** | per-test | per-test | per-test | 全局 | per-test | 全局 |
| **阈值维度** | max_pixels + per_channel_delta | max_diff + num_pixels | 欧几里得距离 | diff_percent | 超标比例 | threshold + max_pixels |
| **多 baseline** | ✅ 2~3 张/test | ❌ 单 ref | ❌ 单 ref | ❌ 单 ref | ❌ 单 ref | ✅ 支持 |
| **多后端** | per-config baseline | platform filter | per-backend ref | 固定环境 | per-API 测试集 | 不涉及 |
| **AA 处理** | Sobel filter | fuzzy(1,N) | 无特殊 | 无 | 邻域搜索 | Vyšniauskas |
| **边缘排除** | IgnoredBorderThickness | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Triage** | Web UI + 即时生效 | 手动改 reftest.list | 环境变量重生成 | flutter update-goldens | 无 | --update-snapshots |
| **外部依赖** | Gold server + GCS | 无 | 无 | 无 | 无 | 无 |
| **实现复杂度** | 高 | 低 | 中 | 低 | 中 | 中 |

---

## 4. 关键设计模式总结

### 模式 1：Per-Backend Baseline（Slint、Skia Gold per-config）

```
test/baseline/opengl/version.json
test/baseline/vulkan/version.json
```

**思路**：每个后端维护独立的 golden image。测试时只与自己后端的 baseline 对比。跨后端对比作为独立的"审计模式"而非 CI 阻塞项。

**适合 TGFX**：✅ 直接解决"GL/VK 不能共用 baseline"问题。

### 模式 2：参数化容差（WebRender fuzzy annotation）

```
fuzzy(max_per_channel_diff, max_num_pixels)
```

**思路**：每个测试的容差与测试定义写在一起。简单透明，一看就知道。

**适合 TGFX**：✅ 轻量级，不需要外部服务。

### 模式 3：多判定维度（Skia Gold fuzzy）

```
MaxDifferentPixels + PixelPerChannelDeltaThreshold + IgnoredBorderThickness
```

**思路**：同时控制"多少像素可以差"和"每个像素可以差多少"，避免单一维度导致漏报或误报。

**适合 TGFX**：✅ 核心需要——rgb_max 和 dr% 必须同时约束。

### 模式 4：边缘/AA 特殊处理（Skia Gold Sobel、Playwright AA detection、dEQP 邻域搜索）

```
1. 检测边缘像素（Sobel / gradient-based）
2. 边缘像素使用更宽容的阈值或直接排除
3. 非边缘像素使用严格阈值
```

**适合 TGFX**：⚠️ 可选。当前 TGFX 的 HIGH 级别差异主要来自边缘像素，如果启用 Sobel 排除，45 例 HIGH 中大部分会降为 LOW。

### 模式 5：环境确定性优先（Flutter、Playwright 最佳实践）

**核心理念**：与其放宽阈值容忍差异，不如**消除差异源**——固定字体、固定渲染器版本、固定 OS 环境。

**对 TGFX 的启示**：
- 当前已用 SwiftShader（固定的软件渲染器）
- 但 GL/VK 走不同代码路径是架构设计，不可消除
- 所以环境确定性**必要但不充分**——仍需容差机制补充

---

## 5. 推荐方案

基于 TGFX 的实际情况（C++ 代码库、不想引入外部服务、多后端、约 500 个用例），推荐组合：

### 核心方案：**WebRender 模式 + Skia Gold 参数**

即：**per-backend baseline + per-test fuzzy annotation + 双维度阈值**

#### 5.1 数据模型

```cpp
struct CompareOptions {
    int maxDifferentPixels = 0;           // 允许差异的最大像素数（0=精确）
    int pixelPerChannelDeltaThreshold = 0; // 单通道差值上限（0~255）
    int ignoredBorderThickness = 0;        // 忽略边缘 N 像素
};
```

**判定逻辑**：
```
PASS 当且仅当:
  1. 尺寸匹配
  2. 排除边缘 N 像素后
  3. per-channel delta > threshold 的像素数 ≤ maxDifferentPixels
```

#### 5.2 Per-Backend Baseline

```
test/baseline/version.json          ← 当前（GL baseline）
test/baseline/vulkan/version.json   ← 新增（VK baseline）
```

- 切换后端时使用对应 baseline 文件
- 跨后端对比作为独立工具（不阻塞 CI）

#### 5.3 Per-Test 容差配置

两种方式（可共存）：

**方式 A：代码内 annotation**
```cpp
Baseline::Compare(surface, "LayerTest/PartialDrawLayer",
                  {.maxDifferentPixels = 5000,
                   .pixelPerChannelDeltaThreshold = 5,
                   .ignoredBorderThickness = 1});
```

**方式 B：配置文件**
```json
// test/baseline/tolerance.json
{
  "LayerTest/PartialDrawLayer": {
    "maxDifferentPixels": 5000,
    "pixelPerChannelDeltaThreshold": 5,
    "ignoredBorderThickness": 1
  },
  "__default__": {
    "maxDifferentPixels": 0,
    "pixelPerChannelDeltaThreshold": 0,
    "ignoredBorderThickness": 0
  }
}
```

#### 5.4 阈值自动推导工具

参考 Chromium 的 `determine_gold_inexact_parameters.py`，编写脚本：

```bash
# 输入：GL 和 VK 的截图目录
# 输出：每个用例建议的 maxDifferentPixels 和 threshold
python3 tools/determine_tolerance.py \
  --gl-dir test/out-gl/ \
  --vk-dir test/out-vk/ \
  --safety-margin 1.5   # 在实测值基础上乘 1.5 作为阈值
```

#### 5.5 回归检测策略

| 场景 | 策略 |
|---|---|
| 同后端 baseline 匹配 | 精确比较（当前行为不变） |
| 同后端 baseline 不匹配但在 tolerance 内 | PASS，但输出 WARNING log |
| 同后端 baseline 不匹配且超出 tolerance | **FAIL** |
| 跨后端审计模式 | 只输出 report，不阻塞 |

---

## 6. 实施路径建议

| 阶段 | 工作 | 收益 |
|---|---|---|
| **Phase 1** | `Baseline::Compare` 增加 `CompareOptions` 参数，保持默认精确比较 | 向后兼容，零风险 |
| **Phase 2** | 为 VK 后端生成独立 baseline | VK CI 可跑起来 |
| **Phase 3** | 编写阈值推导脚本，为已知差异用例配置 tolerance | CI 误报消除 |
| **Phase 4** | （可选）增加 Sobel edge filter 支持 | 进一步降低边缘 AA 误报 |

---

## 附录 A：各方案实际参数参考

### Chromium GPU 测试典型 fuzzy 参数

```python
# 来自 Chromium 源码
PixelTestPage(
    url='css3d_blue_box.html',
    matching_algorithm=FuzzyMatchingAlgorithm(
        max_different_pixels=15,
        pixel_per_channel_delta_threshold=8,
    )
)
```

### WebRender 典型 fuzzy annotation

```bash
# text rendering — 字体 AA 差异
fuzzy(1,326) == text-decorations.yaml text-decorations-ref.png
# box shadow — blur 精度差异
fuzzy(3,10) == box-shadow-inset-large.yaml box-shadow-inset-large-ref.png
# complex blend modes
fuzzy(2,50) == blend-multiply.yaml blend-multiply-ref.png
```

### 对照 TGFX 当前数据的建议阈值

| 级别 | rgb_max | dr% | 建议 pixelPerChannelDelta | 建议 maxDifferentPixels |
|---|---|---|---|---|
| LOW (191 例) | <50 | <62% | 20 | total×0.7 |
| MEDIUM (53 例) | 50~499 | <17% | 对需支持的用例: 视情况 | 视情况 |
| HIGH (45 例) | ≥500 | <11% | 不建议容忍（应 per-backend baseline） | — |

**核心建议**：对 HIGH 级别不放宽阈值，而是使用 **per-backend baseline**——每个后端有自己的 golden image，只检测**同后端回归**。跨后端差异用独立报告监控。
