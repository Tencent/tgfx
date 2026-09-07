# 字体渲染：原理、算法与 GPU 实现

## 摘要

字体渲染是将数学化的字形轮廓描述转换为光栅显示器上离散像素表示的过程。本文档系统阐述字形轮廓表示、光栅化算法、抗锯齿技术、亚像素渲染、GPU 加速文本管线、有符号距离场（SDF）分辨率无关渲染方法，以及三维文本几何生成。讨论遵循从轮廓数据获取到最终合成的完整渲染管线。

---

## 1. 字形轮廓表示

### 1.1 TrueType 轮廓

TrueType 字体使用**二次 Bézier 样条**描述字形形状。每个轮廓（contour）是一条闭合的点序列，包含曲线上点（on-curve）和曲线外控制点（off-curve）：

- **曲线上点（on-curve point）**：曲线经过该点。
- **曲线外点（off-curve point）**：二次 Bézier 段的控制点。

二次 Bézier 曲线由三个点 $(P_0, P_1, P_2)$ 定义：

\[
B(t) = (1-t)^2 P_0 + 2(1-t)t\, P_1 + t^2 P_2, \quad t \in [0, 1]
\]

TrueType 允许**隐式曲线上点**：当两个连续 off-curve 点相邻时，其中点被视为隐含的 on-curve 点。此紧凑表示在不损失通用性的前提下减少了存储开销。

### 1.2 PostScript/CFF 轮廓

PostScript Type 1 和 CFF（Compact Font Format）字体使用**三次 Bézier 样条**，由四个控制点 $(P_0, P_1, P_2, P_3)$ 定义：

\[
B(t) = (1-t)^3 P_0 + 3(1-t)^2 t\, P_1 + 3(1-t)t^2 P_2 + t^3 P_3, \quad t \in [0, 1]
\]

三次曲线每段具有更高的自由度，通常以更少的段数达到等价逼近精度。

### 1.3 可变字体（Variable Fonts）

OpenType 1.8 引入的可变字体在单一文件中通过**设计轴（design axes）**参数化字形轮廓。每个轴（如 `wght`、`wdth`、`ital`）对应一个连续区间，字形轮廓点沿各轴进行**插值变形（variation）**：

\[
P_{\text{instance}} = P_{\text{default}} + \sum_{i=1}^{n} \Delta_i \cdot s_i
\]

其中 $\Delta_i$ 是第 $i$ 个轴对应的增量向量，$s_i$ 是该轴的归一化标量值。

### 1.4 字体度量（Font Metrics）

| 度量 | 定义 |
|------|------|
| **Em 方框（Em square）** | 设计坐标空间，通常 1000（CFF）或 2048（TrueType）单位 |
| **上升部（Ascender）** | 基线到最高字形顶部的距离 |
| **下降部（Descender）** | 基线到最低字形底部的距离（负值） |
| **步进宽度（Advance width）** | 到下一个字形原点的水平距离 |
| **左侧轴承（LSB）** | 字形原点到包围盒左边缘的距离 |
| **字距调整（Kerning）** | 特定字形对之间的步进修正量 |

从设计单位到设备像素的换算公式，设字号 $s$（单位：点），设备分辨率 $d$（单位：DPI）：

\[
\text{pixels} = \frac{\text{design units} \times s \times d}{\text{units per em} \times 72}
\]

---

## 2. 提示（Hinting）与网格对齐（Grid-Fitting）

### 2.1 动机

在小字号（< 20 ppem）下，字形特征可能小于一个像素宽度。若不干预，关键特征（笔画、衬线、对齐区域）将渲染不一致。提示通过受控的形变将关键特征对齐到像素网格。

### 2.2 TrueType 提示

TrueType 字体内嵌字节码程序，由虚拟机（TrueType 解释器）执行。指令相对网格操纵控制点：

- `MDAP[R]`：移动直接绝对点（四舍五入到网格）
- `MDRP[M>RBl]`：移动直接相对点（维持最小距离）
- `IP[]`：在参考点之间插值
- `DELTAP1`：在特定 ppem 尺寸进行逐像素例外调整

解释器维护**图形状态**，包括投影向量、自由向量、参考点、舍入状态和控制值表（CVT）。

### 2.3 PostScript 提示（Type 1 / CFF）

PostScript 提示是**声明式**而非过程式的：

- **笔画提示**（`hstem`、`vstem`）：声明水平/垂直笔画的位置与宽度。
- **反白提示（Counter hints）**：维持笔画间距。
- **提示替换**（`hintmask`）：字形不同区域可使用不同的提示集。

光栅化器解释这些声明，以最优方式将笔画定位在像素网格上。

### 2.4 自动提示（Auto-Hinting）

FreeType 的自动提示器（`autohinter`）通过分析轮廓拓扑自动推断提示：

1. **特征检测**：识别水平/垂直笔画、衬线、圆弧段。
2. **全局度量**：计算大写高度、x 高度、笔画宽度统计量。
3. **边缘对齐**：将检测到的边缘对齐到像素边界，保持笔画宽度一致性。
4. **平滑插值**：未被直接对齐的点通过 IUP（Interpolate Untouched Points）平滑过渡。

---

## 3. 二维光栅化

### 3.1 扫描线转换

经典的字形光栅化基于**扫描线填充算法**。对于每条水平扫描线 $y$：

1. 计算所有轮廓边与扫描线的交点。
2. 按 $x$ 坐标排序交点。
3. 使用**奇偶规则（even-odd rule）**或**非零绕数规则（non-zero winding rule）**判定内外：

**非零绕数规则**：维护绕数计数器 $w$，初始为 0。沿扫描线从左向右遍历：
- 遇到从下向上穿越的边：$w \leftarrow w + 1$
- 遇到从上向下穿越的边：$w \leftarrow w - 1$
- $w \neq 0$ 的区间为填充区域

TrueType 和 CFF 均约定外轮廓为顺时针方向、内轮廓（孔洞）为逆时针方向，非零绕数规则下可正确处理嵌套轮廓。

### 3.2 覆盖率累积法

现代光栅化器（如 FreeType 的 Smooth 渲染器、Skia 的 SkAAClip）采用**覆盖率累积（coverage accumulation）**方法，直接在扫描线级别计算每个像素的部分覆盖率：

对于像素 $(i, j)$，将其视为 $[i, i+1] \times [j, j+1]$ 的单元格。轮廓边穿过该像素时，根据边与像素列边界的交截计算两个值：

- **面积增量（area）**：边在像素内覆盖的面积变化量
- **覆盖增量（cover）**：边穿越像素的垂直跨度（用于后续像素的累积）

最终像素覆盖率 $\alpha$ 通过从左到右累积 cover 值并加上当前像素的 area 修正得到。此方法天然产生解析精度的抗锯齿，无需超采样。

### 3.3 超采样抗锯齿

替代方案是在更高分辨率下光栅化后下采样。设超采样因子为 $n \times m$（水平 $\times$ 垂直），则每个输出像素对应 $n \times m$ 个子采样点。覆盖率：

\[
\alpha = \frac{\text{位于轮廓内部的子采样点数}}{n \times m}
\]

FreeType 默认使用 1×4 垂直超采样（"light" 模式）或 8×8 灰度超采样。

---

## 4. 抗锯齿与亚像素渲染

### 4.1 灰度抗锯齿

灰度抗锯齿将每个像素的覆盖率映射为 $[0, 255]$ 的不透明度值。最终颜色：

\[
C_{\text{out}} = \alpha \cdot C_{\text{text}} + (1 - \alpha) \cdot C_{\text{bg}}
\]

其中 $C_{\text{text}}$ 为文字颜色，$C_{\text{bg}}$ 为背景颜色。

### 4.2 亚像素渲染（ClearType / FreeType LCD）

LCD 面板的每个像素由三个独立寻址的色条（通常 R、G、B 水平排列）组成。利用这一物理特性，可在水平方向获得三倍有效分辨率。

**步骤：**

1. 在三倍水平分辨率下光栅化字形（3×1 超采样）。
2. 对每三个相邻子像素分别计算覆盖率，赋予对应颜色通道。
3. 应用低通滤波器以抑制色纹（color fringing）。

常用滤波核（ClearType 风格，5-tap FIR）：

\[
[w_{-2}, w_{-1}, w_0, w_1, w_2] = \left[\frac{1}{9}, \frac{2}{9}, \frac{3}{9}, \frac{2}{9}, \frac{1}{9}\right]
\]

### 4.3 伽马校正

人眼对亮度的感知是非线性的。正确的文本合成需要在**线性光空间**中进行 alpha 混合：

\[
L = \text{sRGB\_to\_linear}(C), \quad L_{\text{out}} = \alpha \cdot L_{\text{text}} + (1-\alpha) \cdot L_{\text{bg}}, \quad C_{\text{out}} = \text{linear\_to\_sRGB}(L_{\text{out}})
\]

若在 sRGB 空间直接混合，小字号文本会出现"字形偏瘦"或"偏粗"的感知偏差。

---

## 5. GPU 加速二维文本渲染

### 5.1 纹理图集（Texture Atlas）法

**最广泛使用的方法**。预先将字形光栅化为位图，打包到 GPU 纹理图集中。渲染时每个字形对应一个带纹理坐标的四边形（quad）。

**管线：**

```
CPU: 字形光栅化 → 位图缓存 → 图集打包 → 上传纹理
GPU: 顶点着色器（定位四边形）→ 片段着色器（采样图集纹理，alpha 混合）
```

**优点：** 实现简单，批量绘制效率高（一次 draw call 渲染数百字形）。

**缺点：**
- 每个（字形, 字号, 提示参数）组合需要独立缓存条目。
- 缩放/旋转时边缘模糊（位图非分辨率无关）。
- 中日韩（CJK）字符集过大，图集容量成为瓶颈。

### 5.2 直接曲线光栅化

在 GPU 片段着色器中直接求解 Bézier 曲线的覆盖率，无需预光栅化。

**Loop-Blinn 方法（2005）：**

对于二次 Bézier 曲线，将三角形区域参数化，使得片段着色器可通过以下判别式判断片段是否在曲线内侧：

\[
f(u, v) = u^2 - v
\]

- $f < 0$：片段在曲线内侧（填充）
- $f > 0$：片段在曲线外侧（不填充）
- $f = 0$：恰好在曲线上

三次曲线需要更复杂的参数化和分类（serpentine、cusp、loop 三种情况）。

**Slug 方法（Eric Lengyel, 2017）：**

对每条扫描线，在片段着色器中计算所有 Bézier 曲线段与当前像素行的绕数贡献，累积得到精确的解析覆盖率。该方法支持任意变换且精度无损。

### 5.3 计算着色器方法

现代 GPU 管线支持使用 Compute Shader 进行字形光栅化：

1. 每个工作组处理一个 tile（如 16×16 像素）。
2. 将 tile 内相关的 Bézier 段从全局内存加载到共享内存。
3. 对每个像素，累积所有段的覆盖率贡献。
4. 写入输出纹理。

代表实现：Pathfinder（Mozilla）、piet-gpu（Google Fuchsia）、Vello。

---

## 6. 有符号距离场（Signed Distance Field）方法

### 6.1 原理

SDF 将字形轮廓编码为一张标量纹理，每个纹素存储该点到最近轮廓边界的**有符号距离**：

\[
d(x, y) = \begin{cases}
-\min_{\mathbf{p} \in \partial\Omega} \|\mathbf{x} - \mathbf{p}\| & \text{if } \mathbf{x} \in \Omega \text{ (内部)} \\
+\min_{\mathbf{p} \in \partial\Omega} \|\mathbf{x} - \mathbf{p}\| & \text{if } \mathbf{x} \notin \Omega \text{ (外部)}
\end{cases}
\]

其中 $\Omega$ 为字形填充区域，$\partial\Omega$ 为轮廓边界。

### 6.2 渲染

在片段着色器中采样 SDF 纹理，通过阈值化重建边缘：

```glsl
float distance = texture(sdfTexture, uv).r;
float edge = 0.5; // 轮廓对应的归一化距离值
float alpha = smoothstep(edge - smoothing, edge + smoothing, distance);
```

`smoothing` 参数控制抗锯齿宽度，通常取决于屏幕空间的像素密度（可由 `fwidth()` 计算）。

### 6.3 优势与局限

**优势：**
- **分辨率无关**：单张低分辨率（如 32×32）SDF 纹理可在任意缩放下保持清晰边缘。
- **变换友好**：旋转、缩放、透视投影下均有效。
- **效果丰富**：描边、阴影、发光等效果仅需修改着色器中的阈值和混合逻辑。
- **内存高效**：一张 SDF 纹理覆盖所有字号。

**局限：**
- **尖角退化**：标准 SDF 在尖锐拐角处丢失信息（距离场是各向同性的）。
- **小字号模糊**：SDF 分辨率不足时，细节丢失。

### 6.4 多通道 SDF（MSDF）

Chlumský（2015）提出 Multi-channel Signed Distance Field 方法。使用 RGB 三通道分别编码不同边缘方向的距离信息，在片段着色器中取中值：

```glsl
vec3 msd = texture(msdfTexture, uv).rgb;
float sd = median(msd.r, msd.g, msd.b);
float alpha = smoothstep(0.5 - smoothing, 0.5 + smoothing, sd);
```

MSDF 解决了标准 SDF 的尖角退化问题，可在极低分辨率（如 16×16）下保持文字的锐利尖角。

### 6.5 SDF 生成算法

| 算法 | 时间复杂度 | 特点 |
|------|-----------|------|
| 暴力搜索 | $O(n \cdot w \cdot h)$ | 对每个纹素遍历所有轮廓段求最近距离 |
| 8SSEDT（Danielsson） | $O(w \cdot h)$ | 两趟扫描的距离变换近似 |
| 精确解析法 | $O(n \cdot w \cdot h)$ | 对每段 Bézier 求解析最近点（解三次/四次方程） |
| Chlumský MSDF 生成器 | $O(n \cdot w \cdot h)$ | 在精确解析法基础上分配边缘着色 |

---

## 7. 三维文本渲染

### 7.1 轮廓三角化（Triangulation）

将 2D 字形轮廓转化为三角网格是 3D 文本的第一步。

**步骤：**

1. **曲线线性化**：将 Bézier 曲线按给定公差 $\epsilon$ 递归细分为折线段。细分终止条件通常为曲线与弦的最大偏差小于 $\epsilon$：

\[
\text{flatness}(P_0, P_1, P_2) = \frac{\|P_1 - \frac{P_0 + P_2}{2}\|}{2} \leq \epsilon
\]

2. **多边形三角化**：对线性化后的闭合多边形进行三角化。常用算法：

   - **耳切法（Ear Clipping）**：$O(n^2)$，实现简单，处理孔洞需先桥接。
   - **约束 Delaunay 三角化（CDT）**：$O(n \log n)$，生成质量更高的三角形。
   - **单调多边形分解**：先将多边形分解为 $y$-单调子多边形，再线性时间三角化。

3. **孔洞处理**：通过搜索内外轮廓间的最近点对，插入桥接边将孔洞合并到外轮廓中，使整体成为简单多边形。

### 7.2 拉伸（Extrusion）

沿法线方向（通常为 $z$ 轴）拉伸前表面，生成具有厚度的三维实体：

1. **前表面**：三角化的 2D 轮廓，$z = 0$。
2. **后表面**：前表面的副本，$z = d$（$d$ 为拉伸深度），三角形缠绕方向取反。
3. **侧面**：连接前后表面对应轮廓边的四边形条带，每个四边形由两个三角形组成。

侧面法线计算：对于轮廓边 $\mathbf{e} = P_{i+1} - P_i$，外法线为：

\[
\mathbf{n} = \text{normalize}(\mathbf{e} \times \mathbf{z}) = \text{normalize}(e_y, -e_x, 0)
\]

### 7.3 倒角与圆角（Bevel and Round）

为使三维文字边缘更具立体感，常在前/后表面边缘添加倒角或圆角：

**直线倒角（Bevel）：**

在轮廓边缘处添加 45° 斜切面，引入额外的三角形条带。法线为边缘法线与面法线的平均。

**圆角（Round / Chamfer）：**

沿边缘生成弧形截面，细分为 $n$ 段。每段的法线通过对面法线和边缘法线进行球面插值（slerp）得到：

\[
\mathbf{n}(\theta) = \cos\theta \cdot \mathbf{n}_{\text{face}} + \sin\theta \cdot \mathbf{n}_{\text{edge}}, \quad \theta \in \left[0, \frac{\pi}{2}\right]
\]

顶点位置沿法线方向偏移，形成圆弧截面。

### 7.4 三维 SDF 文本（体积渲染）

将 2D SDF 沿 $z$ 轴拉伸为三维标量场，通过 Ray Marching 渲染：

```glsl
float map(vec3 p) {
    float d2d = sdfText(p.xy);         // 2D 字形 SDF
    float dz = abs(p.z) - thickness;   // z 方向的平板 SDF
    return max(d2d, dz);               // 交集 = 拉伸体
}
```

此方法天然支持：
- **软阴影**：沿光线方向步进，累积遮蔽比。
- **环境光遮蔽（AO）**：在表面法线方向采样 SDF 场。
- **反射/折射**：基于 SDF 梯度法线实现。
- **布尔运算**：`max(a, b)` = 交集，`min(a, b)` = 并集，`max(a, -b)` = 差集。

### 7.5 法线贴图与位移贴图

对于不需要真实几何厚度的场景，可将 SDF 用作**高度图（height map）**，通过法线贴图（normal mapping）或视差贴图（parallax mapping）模拟三维深度：

法线从 SDF 梯度计算：

\[
\mathbf{n} = \text{normalize}\left(-\frac{\partial d}{\partial x}, -\frac{\partial d}{\partial y}, 1\right)
\]

梯度可通过中心差分近似：

\[
\frac{\partial d}{\partial x} \approx \frac{d(x+\epsilon, y) - d(x-\epsilon, y)}{2\epsilon}
\]

---

## 8. 文本排版（Text Layout）

### 8.1 简单文本排版

对于单行水平文本（拉丁字母、CJK 等从左到右的书写系统）：

1. 从字体的 `cmap` 表将 Unicode 码位映射为字形 ID（glyph index）。
2. 查询每个字形的 advance width，累积水平偏移。
3. 应用字距调整（kerning，来自 `kern` 表或 GPOS 表）。
4. 放置每个字形的原点。

### 8.2 复杂文本排版（Complex Text Layout, CTL）

阿拉伯文、天城文、泰文等书写系统需要**文本整形（shaping）**：

1. **字符重排序**：逻辑顺序与视觉顺序不同（如阿拉伯文从右到左）。
2. **连字（Ligature）**：多个字符合并为单一字形（如阿拉伯文连写形式）。
3. **上下文替换**：同一字符根据位置（词首、词中、词尾、独立）使用不同字形。
4. **附加符号定位**：元音标记、声调符号相对基字符精确定位。

**HarfBuzz** 是目前最广泛使用的开源文本整形引擎，完整实现了 OpenType Layout（GSUB/GPOS）和 Universal Shaping Engine（USE）规范。

### 8.3 双向文本（BiDi）

混合从左到右（LTR）和从右到左（RTL）文本时，需遵循 Unicode Bidirectional Algorithm（UBA, UAX #9）：

1. 确定每个字符的 BiDi 类别（L、R、AN、EN、…）。
2. 解析嵌入/覆盖/隔离级别。
3. 解析弱类型和中性类型。
4. 为每个字符分配嵌入级别。
5. 按嵌入级别对行内 runs 进行视觉重排序。

---

## 9. 性能优化策略

### 9.1 字形缓存分层

```
L1: GPU 纹理图集（热字形，直接渲染）
L2: CPU 位图缓存（温字形，需上传到 GPU）
L3: 轮廓缓存（冷字形，需光栅化 → 位图 → 上传）
L4: 字体文件（未缓存字形，需解析轮廓 → 提示 → 光栅化）
```

### 9.2 批量渲染

将相同纹理图集的字形合并为单次 draw call：

- **实例化渲染（Instancing）**：每实例传递字形位置和纹理坐标。
- **间接绘制（Indirect Draw）**：GPU 端生成绘制参数，减少 CPU-GPU 同步。
- **动态顶点缓冲**：每帧填充一个大顶点缓冲，单次提交。

### 9.3 增量更新

文本内容变化时，仅更新变化部分：

- **脏矩形追踪**：仅重绘文本变化区域。
- **图集增量上传**：新字形仅 `glTexSubImage2D` 局部更新。
- **LRU 淘汰**：图集满时淘汰最近最少使用的字形。

### 9.4 多线程管线

```
线程 1（排版）：文本整形 → 布局计算 → 产生字形序列
线程 2（光栅化）：CPU 光栅化新字形 → 写入暂存缓冲
线程 3（GPU）：上传纹理 → 提交绘制命令
```

三阶段流水线并行，最大化吞吐量。

---

## 10. 质量评估指标

### 10.1 笔画宽度一致性

同一字体、同一字号下，相同权重的笔画应渲染为相同的像素宽度。量化方法：测量多个目标笔画的渲染宽度标准差 $\sigma_w$，$\sigma_w < 0.3\text{px}$ 为优秀。

### 10.2 对齐精度

基线、x 高度、大写高度应精确对齐到像素行。偏差通过统计同一行内多个字形的垂直对齐误差分布来评估。

### 10.3 色纹评估（亚像素渲染）

在黑白边缘过渡区测量 $\Delta E_{ab}^*$（CIELAB 色差），$\Delta E_{ab}^* < 2.0$ 为不可感知的色纹水平。

### 10.4 渲染保真度

对于 SDF 方法，通过与参考光栅化器（如 FreeType 精确模式）的输出逐像素比较：

\[
\text{PSNR} = 10 \cdot \log_{10}\left(\frac{255^2}{\text{MSE}}\right)
\]

PSNR > 40 dB 表示视觉无损。

---

## 11. 参考文献

1. Knuth, D.E. (1986). *The METAFONTbook*. Addison-Wesley.
2. Green, C. (2007). "Improved Alpha-Tested Magnification for Vector Textures and Special Effects." *ACM SIGGRAPH 2007 Courses*.
3. Loop, C., Blinn, J. (2005). "Resolution Independent Curve Rendering using Programmable Graphics Hardware." *ACM SIGGRAPH 2005 Papers*.
4. Chlumský, V. (2015). "Shape Decomposition for Multi-channel Distance Fields." Master's thesis, Czech Technical University in Prague.
5. Lengyel, E. (2017). "GPU-Centered Font Rendering Directly from Glyph Outlines." *Journal of Computer Graphics Techniques*, 6(2).
6. Behdad Esfahbod et al. HarfBuzz: An OpenType text shaping engine. https://harfbuzz.github.io/
7. FreeType Project. https://freetype.org/
8. Levien, R. (2020). "piet-gpu: A GPU compute-centric 2D renderer." https://raphlinus.github.io/
9. Microsoft Typography. "TrueType Fundamentals." OpenType Specification.
10. Adobe Systems. "The Type 2 Charstring Format." Technical Note #5177.

---

## 附录 A：二次 Bézier 曲线到像素覆盖率的解析解

设二次 Bézier 曲线 $B(t) = (x(t), y(t))$ 经过像素单元 $[x_0, x_0+1] \times [y_0, y_0+1]$。该曲线对像素覆盖率的贡献等于轮廓在该像素列内围成的有符号面积：

\[
A = \int_{t_0}^{t_1} (x(t) - x_0) \cdot y'(t) \, dt
\]

其中 $[t_0, t_1]$ 是曲线在像素列 $[x_0, x_0+1]$ 内的参数区间。对于二次曲线，此积分是关于 $t$ 的四次多项式，可解析求解。

## 附录 B：SDF 中二次 Bézier 曲线最近点求解

求点 $\mathbf{p}$ 到二次 Bézier 曲线 $B(t)$ 的最近点，等价于最小化：

\[
f(t) = \|B(t) - \mathbf{p}\|^2
\]

令 $f'(t) = 0$：

\[
f'(t) = 2(B(t) - \mathbf{p}) \cdot B'(t) = 0
\]

展开后得到关于 $t$ 的**三次方程**，可用 Cardano 公式或数值方法求解。需额外检查端点 $t=0$ 和 $t=1$ 处的距离。

对于三次 Bézier 曲线，最小化问题产生**五次方程**，无解析通解，通常使用 Newton-Raphson 迭代或细分法求解。

---

## 附录 C：字体库实践与高级效果——以 tgfx 为例

本附录以 tgfx 图形引擎为载体，分析主流字体库的集成方式、API 设计模式，以及高级文本渲染效果的实现策略。

### C.1 字体库生态概览与选型

| 库 | 职责 | 适用场景 |
|---|---|---|
| **FreeType** | 字形轮廓解析、光栅化、度量计算、位图/矢量颜色字形加载 | 跨平台核心后端 |
| **HarfBuzz** | OpenType 文本整形（GSUB/GPOS）、连字、上下文替换、复杂文字 | 阿拉伯/天城/emoji 序列 |
| **Core Text** (Apple) | 系统级字体匹配、光栅化、高级排版 | macOS/iOS 原生渲染 |
| **DirectWrite** (Windows) | 系统字体枚举、ClearType 光栅化 | Windows 原生渲染 |
| **ICU** | Unicode 分段（断词/断行）、BiDi | 复杂文本布局（tgfx 未使用） |
| **libpng / skcms** | 颜色 emoji 位图解码、色彩空间转换 | 配合 FreeType 使用 |

**tgfx 的选型策略**：平台自适应 + 最小依赖。

- Apple 平台：直接使用 Core Text/Core Graphics（`CGScalerContext`），无需编译 FreeType，利用系统字体缓存和 LCD 渲染。
- Android/Linux/Windows：FreeType + HarfBuzz（`FTScalerContext`），HarfBuzz 以 `-DHB_MINI` 编译减小体积，不依赖 ICU。
- Web（Emscripten）：专用 `WebScalerContext`，利用浏览器 Canvas2D 光栅化。

### C.2 tgfx 字体 API 层次结构

```
┌────────────────── 应用层 ──────────────────────┐
│  TextLayer / TextPath / TextModifier            │
│  (自动换行、对齐、截断、沿路径、逐字符动画)       │
└───────────────────────┬────────────────────────┘
                        │
┌───────────────────────┴────────────────────────┐
│              核心文本 API                        │
│  Typeface → Font → TextBlobBuilder → TextBlob  │
│  Canvas.drawTextBlob() / drawGlyphs()          │
└───────────────────────┬────────────────────────┘
                        │
┌───────────────────────┴────────────────────────┐
│           ScalerContext（平台抽象层）             │
│  FTScalerContext / CGScalerContext / Web...     │
│  职责：getAdvance / getBounds / getPath /       │
│        getImage / readPixels                   │
└───────────────────────┬────────────────────────┘
                        │
┌───────────────────────┴────────────────────────┐
│             GPU 渲染管线                         │
│  GlyphRasterizer → Atlas → AtlasTextOp → GPU  │
│  回退：GlyphShape → Shape → 通用 DrawOp        │
└────────────────────────────────────────────────┘
```

### C.3 FreeType 的使用模式

#### C.3.1 初始化与字体加载

```cpp
// 典型的 FreeType 初始化流程（tgfx 内部封装）
FT_Library library;
FT_Init_FreeType(&library);

FT_Face face;
FT_New_Memory_Face(library, fontData, fontDataSize, faceIndex, &face);

// 设置字号（26.6 固定点数格式，像素大小 × 64）
FT_Set_Char_Size(face, 0, fontSize * 64, dpiX, dpiY);
```

#### C.3.2 字形获取与光栅化

```cpp
// Unicode → GlyphID
FT_UInt glyphIndex = FT_Get_Char_Index(face, unicodeCodepoint);

// 加载字形（不立即光栅化）
FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT);

// 获取轮廓路径（用于矢量渲染 / SDF 生成）
FT_Outline* outline = &face->glyph->outline;

// 光栅化为位图（灰度抗锯齿）
FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
FT_Bitmap* bitmap = &face->glyph->bitmap;
```

#### C.3.3 tgfx 中的关键加载标志组合

```cpp
// 普通字形：加载轮廓，不光栅化
FT_LOAD_NO_BITMAP

// 颜色 emoji：加载彩色位图
FT_LOAD_COLOR

// 描边字形：加载后应用 FT_Stroker
FT_Stroker_Set(stroker, strokeWidth * 64, FT_STROKER_LINECAP_ROUND,
               FT_STROKER_LINEJOIN_ROUND, 0);
FT_Glyph_Stroke(&glyph, stroker, false);
```

### C.4 HarfBuzz 文本整形集成

#### C.4.1 整形流程

```cpp
// 创建 buffer
hb_buffer_t* buf = hb_buffer_create();
hb_buffer_add_utf8(buf, text, -1, 0, -1);
hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
hb_buffer_set_language(buf, hb_language_from_string("en", -1));

// 从 FreeType face 创建 hb_font
hb_font_t* font = hb_ft_font_create(ftFace, nullptr);

// 执行整形
hb_shape(font, buf, nullptr, 0);

// 获取结果
unsigned int glyphCount;
hb_glyph_info_t* glyphInfo = hb_buffer_get_glyph_infos(buf, &glyphCount);
hb_glyph_position_t* glyphPos = hb_buffer_get_glyph_positions(buf, &glyphCount);

// glyphInfo[i].codepoint → GlyphID
// glyphPos[i].x_advance / y_advance → 步进（26.6 格式）
// glyphPos[i].x_offset / y_offset → 偏移（用于组合标记定位）
```

#### C.4.2 tgfx 中的 TextShaper 抽象

tgfx 通过 `TextShaper` 类封装 HarfBuzz，支持：
- **回退字体链**：主字体缺失的字形自动切换到 fallback 字体
- **emoji 序列识别**：ZWJ 序列（👨‍👩‍👧‍👦）、肤色修饰符（👨🏼）、国旗（🇨🇳）
- **可扩展设计**：用户可继承 `TextShaper` 实现自定义整形逻辑

### C.5 GPU 文本渲染管线详解

#### C.5.1 Atlas 纹理图集策略

tgfx 使用分层 Atlas 管理方案：

| 参数 | 值 | 说明 |
|------|---|------|
| Atlas 纹理尺寸 | 2048 × 2048 | 单张 Atlas 最大尺寸 |
| Plot 分块大小 | 512 × 512 | Atlas 内部划分为 4×4 = 16 个 plot |
| 字形尺寸上限 | 256 × 256 px | 超过此尺寸回退到路径渲染 |
| 缓存上限 | 4MB / 2048 条目 | LRU 淘汰最近未使用的字形 |
| 格式区分 | A8 / RGBA / BGRA | 灰度字形 / 颜色 emoji / Apple 颜色 |

**缓存键 (Cache Key)**：`TypefaceID + BackingSize + IsBold + StrokeWidth`

这意味着同一字体在不同字号下会产生不同的缓存条目（非 SDF 方案的固有代价）。

#### C.5.2 渲染路径选择逻辑

```
输入：GlyphRun + 变换矩阵 + Paint
  │
  ├─── 判断是否需要路径渲染？
  │    条件：RSXform/Matrix 定位 + 非轴对齐旋转 + 有描边
  │    是 → drawGlyphAsPath()（矢量路径渲染，精度无限）
  │
  └─── 否 → drawGlyphsAsDirectMask()（Atlas 纹理渲染）
       │
       ├── 字形 ≤ 256px → 光栅化到 Atlas → AtlasTextOp 批量绘制
       │
       └── 字形 > 256px → drawGlyphAsTransformedMask()
            └── 每帧单独光栅化（不缓存到 Atlas）
```

#### C.5.3 Gamma 校正

tgfx 可选启用文本 gamma 校正（通过 `TGFX_USE_TEXT_GAMMA_CORRECTION` 编译选项）：

```cpp
// 在光栅化阶段应用 gamma 表（PathRasterizer）
// 目的：补偿 LCD 显示器非线性响应，使小字号文本粗细更均匀
static const uint8_t GammaTable[256]; // 预计算的 gamma 映射
alpha = GammaTable[alpha];
```

### C.6 高级文本效果实现

#### C.6.1 渐变填充文本

通过 `Paint.setShader()` 将着色器应用于文本：

```cpp
auto shader = Shader::MakeLinearGradient(
    Point{0, 0}, Point{textWidth, 0},
    {Color::Red(), Color::Blue()},
    {0.0f, 1.0f},
    TileMode::Clamp);

Paint paint;
paint.setShader(shader);
canvas->drawTextBlob(textBlob, x, y, paint);
```

**GPU 实现原理**：Atlas 纹理提供覆盖率（alpha），着色器提供颜色，两者在片段着色器中相乘：

```glsl
vec4 color = evaluateShader(fragCoord);  // 渐变/图案颜色
float alpha = texture(atlasTexture, atlasUV).a;  // 字形覆盖率
fragColor = color * alpha;
```

#### C.6.2 描边文本

```cpp
// 先绘制描边（底层）
Paint strokePaint;
strokePaint.setStyle(PaintStyle::Stroke);
strokePaint.setStrokeWidth(3.0f);
strokePaint.setColor(Color::Black());
canvas->drawTextBlob(textBlob, x, y, strokePaint);

// 再绘制填充（上层）
Paint fillPaint;
fillPaint.setColor(Color::White());
canvas->drawTextBlob(textBlob, x, y, fillPaint);
```

**内部实现**：描边模式下，光栅化器使用 FreeType Stroker 对字形轮廓做描边扩展后再光栅化到 Atlas。描边字形和填充字形使用不同的缓存键（因为 StrokeWidth 是键的一部分）。

#### C.6.3 文本沿路径排列（TextPath）

```cpp
// layers 层 API
auto textPath = TextPath::Make();
textPath->setText(textBlob);
textPath->setPath(curvePath);           // 贝塞尔路径
textPath->setPerpendicularToPath(true); // 字符垂直于切线
textPath->setReversedPath(false);       // 正向
textPath->setFirstMargin(10.0f);        // 起始偏移
```

**算法**：
1. 对路径进行弧长参数化（arc-length parameterization）。
2. 对每个字形中心位置，通过弧长反查路径上的点和切线方向。
3. 以切线为 x 轴方向，对字形施加局部旋转矩阵。

#### C.6.4 逐字符动画（TextModifier + TextSelector）

这是 tgfx 的特色功能，类似 After Effects 的文本动画器：

```cpp
// 定义选择器：选中前 50% 的字符
auto selector = TextSelector::Make();
selector->setRange({0.0f, 0.5f});
selector->setMode(TextSelectorMode::Percentage);
selector->setShape(TextSelectorShape::RampUp);  // 渐入曲线

// 定义修改器：被选中的字符向上位移 + 透明度渐变
auto modifier = TextModifier::Make();
modifier->setPosition({0.0f, -50.0f});  // y 方向上移 50px
modifier->setOpacity(0.3f);             // 透明度 30%
modifier->setScale({1.2f, 1.2f});       // 放大 120%
modifier->setRotation(15.0f);           // 旋转 15°
modifier->setFillColor(Color::Red());   // 变色

// 组合
modifier->addSelector(selector);
textElement->addModifier(modifier);
```

**实现原理**：
1. TextSelector 对每个字符计算一个 `[0, 1]` 的影响权重（由 shape 曲线决定过渡形状）。
2. TextModifier 将各属性（位移/旋转/缩放/透明度/颜色）乘以权重。
3. 渲染时对每个字形应用独立的变换矩阵和颜色修改。
4. 通过动态修改 selector 的 range 参数可实现逐字入场动画。

#### C.6.5 颜色 Emoji 与分层矢量字形

tgfx 完整支持 OpenType 颜色字体的四种格式：

| 格式 | 类型 | 支持情况 |
|------|------|----------|
| **CBDT/CBLC** | 位图 emoji（Google Noto Color Emoji） | ✅ FreeType `FT_LOAD_COLOR` |
| **sbix** | 位图 emoji（Apple Color Emoji） | ✅ FreeType 加载 |
| **COLRv0** | 分层矢量（多个单色路径层叠加） | ✅ `FT_Get_Color_Glyph_Layer()` 逐层渲染 |
| **COLRv1** | 树状颜料图（渐变、变换、混合） | ✅ 递归遍历 `FT_COLR_PAINTFORMAT_*` |
| **SVG** | SVG 嵌入（Firefox emoji） | ❌ 未实现 |

**COLRv0 渲染流程**：
```
1. 枚举字形所有颜色层（每层 = GlyphID + PaletteColorIndex）
2. 对每层获取轮廓路径
3. 从 CPAL 调色板获取颜色
4. 按层序从底到顶绘制填充路径
```

#### C.6.6 自定义图形字体

tgfx 支持从矢量路径或图片构建自定义字体，适用于图标字体、特殊符号：

```cpp
// 矢量路径字体
PathTypefaceBuilder builder;
builder.setGlyphName('A', "customA");

Path starPath;
starPath.moveTo(50, 0);
starPath.lineTo(65, 35);
// ... 构建五角星
builder.setGlyphPath('A', starPath, 100.0f);  // advance = 100

auto typeface = builder.build();
Font font(typeface, 48);
canvas->drawSimpleText("AAA", font, paint);  // 绘制三个五角星
```

```cpp
// 图片字体（如 SVG icon 预渲染）
ImageTypefaceBuilder imgBuilder;
imgBuilder.setGlyphImage('X', imageCodec, Point{0, -40}, 50.0f);
auto imgTypeface = imgBuilder.build();
```

### C.7 性能最佳实践

#### C.7.1 减少 Atlas 压力

| 策略 | 适用场景 |
|------|----------|
| 限制字号种类 | UI 文本统一使用 12/14/16/20/24 等离散字号 |
| 预热字形缓存 | 应用启动时预光栅化常用字符（ASCII + 常见汉字） |
| 合理设置描边宽度 | 描边宽度是缓存键的一部分，避免浮点精度产生多余条目 |
| 大字号用路径渲染 | 标题/数字/logo 等大尺寸文本使用矢量渲染，不占 Atlas |

#### C.7.2 批量绘制

```cpp
// 推荐：一次 drawTextBlob 包含完整段落
auto blob = TextBlob::MakeFrom(text, font);
canvas->drawTextBlob(blob, x, y, paint);

// 避免：逐字符单独绘制（产生大量 draw call）
for (auto ch : text) {
    canvas->drawGlyphs(&glyph, &pos, 1, font, paint);  // ❌ 低效
}
```

#### C.7.3 TextBlob 复用

```cpp
// TextBlob 是不可变对象，可在多帧间复用
// 仅文本内容变化时才重新创建
if (textChanged) {
    cachedBlob = TextBlob::MakeFrom(newText, font);
}
canvas->drawTextBlob(cachedBlob, x, y, paint);
```

### C.8 对比：tgfx 与其他引擎的文本方案

| 特性 | tgfx | Skia | Dear ImGui | Unity TextMesh Pro |
|------|------|------|------------|-------------------|
| 光栅化方案 | Atlas（直接光栅化） | Atlas（直接光栅化） | Atlas（直接光栅化） | SDF / MSDF |
| 分辨率无关 | ❌（需按字号缓存） | ❌ | ❌ | ✅ |
| 颜色 emoji | ✅ COLRv0/v1 + 位图 | ✅ | ❌ | ❌ |
| 复杂文字整形 | ✅ HarfBuzz | ✅ HarfBuzz / ICU | ❌ | 有限 |
| 沿路径排列 | ✅ TextPath | ✅ | ❌ | ✅ |
| 逐字符动画 | ✅ TextModifier | 需手动实现 | ❌ | ✅ (Rich Text Tags) |
| 描边 | ✅ FT_Stroker | ✅ | ❌ | ✅ (SDF 阈值) |
| 渐变填充 | ✅ Shader | ✅ Shader | ❌ | ✅ (顶点色) |
| 3D 文本 | ❌ | ❌ | ❌ | ✅ (mesh extrusion) |
| 文本阴影 | ✅ ImageFilter | ✅ MaskFilter | ❌ | ✅ (SDF 偏移) |

### C.9 未来演进方向

基于 tgfx 当前架构，可能的增强方向：

1. **SDF/MSDF 渲染模式**：为大字号和变换场景提供分辨率无关方案，减少 Atlas 内存占用。适合 CJK 文本（字符集大，Atlas 压力高）。

2. **可变字体 API**：暴露 `fvar` 轴的设置接口（`Font::setVariation(tag, value)`），支持动态权重/宽度动画。

3. **GPU 直接曲线光栅化**：对于大字号字形，在 Compute Shader 中直接求解 Bézier 覆盖率，避免 CPU 光栅化瓶颈。

4. **3D 文本几何**：集成轮廓三角化 + 拉伸管线（参见本文第 7 章），支持 3D 场景中的文字渲染。

5. **异步字形光栅化**：将 CPU 光栅化移至后台线程，Atlas 上传延迟到下一帧，消除主线程阻塞。
