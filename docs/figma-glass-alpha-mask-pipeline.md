# Figma Glass Effect — 高度图生成与使用完整流程

## 1. 概述

Figma 玻璃效果的 v1 变体不需要为每种形状写专门的 SDF，而是利用一个通用原理：**将形状的 Alpha 通过高斯模糊近似为无符号距离场（UDF），再通过梯度计算获得法线方向**。这种方式适用于任意形状（五角星、文本、不规则 Path），且内部也能产生折射。

### 核心原理

```
形状 Alpha mask (二值: 0/1)
       ↓ 高斯模糊 (σ 较大)
模糊后的 Alpha (近似 UDF: 边缘→0, 中心→1)
       ↓ 中心差分
梯度向量 (处处非零，指向远离最近边的方向)
       ↓ splay 混合 + 非线性缩放
折射偏移 → 采样模糊背景
```

---

## 2. 整体架构

```
阶段 1: Shape Alpha 渲染
  图层形状（五角星/文本/任意 Path）
       ↓ 光栅化（白色填充，仅 Alpha 通道有效）
  二值 Alpha 纹理 (内部=1, 外部=0, 边缘抗锯齿)

阶段 2: 高斯模糊 → 高度图 (texture1_)
  二值 Alpha
       ↓ 水平高斯 Pass (σ 由 refractionDistance 决定)
       ↓ 垂直高斯 Pass
  模糊 Alpha → RGBA8 打包
       = texture1_ (高度图)

阶段 3: Bevel 纹理生成 (texture2_)
  二值 Alpha / 模糊 Alpha
       ↓ SDF 计算 + 法线检测
  RGBA8: RGB=法线方向, A=归一化SDF
       = texture2_ (bevel 纹理)

阶段 4: 背景捕获 + 模糊
  玻璃层下方的全部内容
       ↓ 离屏渲染 + 可选 frost 模糊
  texture0_ = 模糊后的背景

阶段 5: 玻璃着色器合成
  texture1_(高度图) + texture0_(背景) + texture2_(bevel)
       ↓ GlassRefractionEffect (se())
  最终输出
```

---

## 3. 三张纹理的详细说明

### 3.1 texture0_ — 模糊背景

| 属性 | 值 |
|------|-----|
| 格式 | RGBA8 颜色纹理 |
| 内容 | 玻璃层下方所有内容的模糊后颜色 |
| 生成 | 离屏渲染 + 可选高斯模糊（frost 参数） |
| 采样方式 | `pw(coord)` → `textureSample(texture0_, q, pp(coord, tileExpansionScale))` |
| 用途 | 折射后的最终背景颜色 |

### 3.2 texture1_ — 高度图（模糊 Alpha）

| 属性 | 值 |
|------|-----|
| 格式 | RGBA8，打包单精度浮点值 |
| 内容 | 形状 Alpha 的高斯模糊结果（近似 UDF） |
| 生成 | 二值 Alpha → 两次分离式高斯模糊 → RGBA8 打包 |
| 采样方式 | `rc(coord)` → `bf(textureSample(texture1_, s, coord))` |
| 解包函数 | `bf(rgba) = dot(rgba, vec4(1, 1/255, 1/65025, 1/16581375))` |
| 用途 | `rf()` 中心差分计算梯度 → 法线方向 → 折射方向 |

**为什么不是直接的 Alpha mask：**

| | 直接 Alpha mask | 模糊后的 Alpha（高度图） |
|---|---|---|
| 边缘 | 0→1 锐利过渡 | 0→1 平缓过渡 |
| **内部** | **恒定 = 1.0** | **从边缘到中心逐渐增大** |
| 内部梯度 | **= 0 ✗ 无折射** | **≠ 0 ✓ 有折射** |
| 中心 | 1.0 | ≈ 1.0（最大值） |

### 3.3 texture2_ — Bevel 纹理（SDF + 法线）

| 属性 | 值 |
|------|-----|
| 格式 | RGBA8 |
| RGB | 形状法线方向（编码为颜色） |
| A | 归一化有符号距离（SDF），范围 [0, 1] |
| 生成 | 从 Alpha mask 派生：SDF 计算 + 边缘检测（Sobel 等） |
| 采样方式 | `qx(coord)` → `textureSample(texture2_, t, pp(coord, bevelExpansionScale)).a × 2.0 - 1.0` |
| 用途 | 计算边缘权重 → 折射强度衰减 |

**Alpha 通道的 SDF 映射：**

```
.a = 1.0  →  SDF = +1  (形状内部深处)
.a = 0.5  →  SDF =  0  (边界上)
.a = 0.0  →  SDF = -1  (形状外部)
```

**在 bevel composite shader 中的额外用途：**

```glsl
vec3 normalRGB = texture2(_coord2).rgb;
float normalStrength = sqrt(dot(normalRGB², luminance_weights));
float sdf = texture2(_coord2).a;
fig_FragColor = mix(texture1, texture0, sdf × normalStrength);
```

合成 shader 用 bevel 纹理将高度图和背景混合：内部用背景，边缘用高度图。

---

## 4. 高度图生成详解

### 4.1 高斯模糊过程

```
输入: 五角星二值 Alpha
┌──────────────────────────┐
│ 0  0  0  0  0  0  0  0   │
│ 0  0  0  1  1  0  0  0   │
│ 0  0  1  1  1  1  0  0   │
│ 0  1  1  1  1  1  1  0   │  ← 内部全 1.0
│ 0  0  1  1  1  1  0  0   │
│ 0  0  0  1  1  0  0  0   │
│ 0  0  0  0  0  0  0  0   │
└──────────────────────────┘

经过高斯模糊 (σ ≈ refractionDistance × scale):
┌──────────────────────────┐
│.01.02.03.04.04.03.02.01  │
│.02.05.12.18.18.12.05.02  │
│.03.12.35.52.52.35.12.03  │
│.04.18.52.75.75.52.18.04  │  ← 中心最高 ≈ 0.75
│.03.12.35.52.52.35.12.03  │
│.02.05.12.18.18.12.05.02  │
│.01.02.03.04.04.03.02.01  │
└──────────────────────────┘
```

### 4.2 模糊 Alpha 近似 UDF 的原理

高斯模糊本质是卷积 `G_σ * α(x,y)`。对二值函数做卷积后：
- 在边缘像素：附近有很多 0 参与卷积 → 结果低
- 在中间像素：与边缘的**距离**决定了有多少 1 参与 → **近似线性于到边缘的距离**
- 在中心像素：几乎全是 1 → 结果趋近 1.0

```
到最近边距离:  0    5    10    15    20    25    30
精确 UDF:      0    5    10    15    20    25    30  (线性增长)
模糊 Alpha:    0   .15   .35   .55   .72   .86   .95 (S 形饱和)
```

虽然不是严格线性，但**梯度方向与精确 UDF 完全一致**——都指向远离最近边的方向。非线性反而是优势：
- **边缘处梯度大** → 折射强（玻璃截面斜率大）
- **中心处梯度小** → 折射弱但非零（柔和的内部微折射）

### 4.3 RGBA8 打包

模糊后的浮点值 ∈ [0, 1]，打包到 RGBA8 四通道以获得 32 bit 精度：

```
value = 0.7234

R = floor(value × 255)           = 184
G = floor(remainder × 255²)      = 116
B = floor(remainder₂ × 255³)     = 19
A = floor(remainder₃ × 255⁴)     = ...

RGBA8 = (184, 116, 19, ...)
```

**解包：**
```wgsl
const d = vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0);
fn bf(rgba: vec4<f32>) -> f32 { return dot(rgba, d); }
```

---

## 5. 着色器中的折射计算流程

### 步骤 1：坐标转换

```wgsl
sf = (tileToNodeSpace × vec3(u.x, u.y, 1.0)).xy;
```

屏幕 UV `u ∈ [0,1]²` → 节点空间坐标 `sf`。

### 步骤 2：高度图梯度（中心差分）

```wgsl
sh = rf(sf, refractionDistance);
```

```wgsl
fn rf(coord, stepDist) -> vec2<f32> {
    step = clamp(stepDist, 1.0, 6.0);
    left   = rc(pp(coord - vec2(step, 0),  heightExpansionScale));
    right  = rc(pp(coord + vec2(step, 0),  heightExpansionScale));
    bottom = rc(pp(coord - vec2(0, step),  heightExpansionScale));
    top    = rc(pp(coord + vec2(0, step),  heightExpansionScale));
    return vec2(right - left, top - bottom) / (2.0 × step);
}
```

### 步骤 3：Splay 混合方向

```wgsl
sh = ru(sh, sf);
```

```wgsl
fn ru(gradient, pixelPos) -> vec2<f32> {
    mag = length(gradient);
    gradDir = normalize(gradient);                        // 梯度方向
    centerDir = -normalize(pixelPos - nodeSpaceCenterPoint); // 指向中心
    mixedDir = mix(gradDir, centerDir, splay);            // 线性混合
    return mixedDir × mag;                                // 恢复模
}
```

### 步骤 4：保存法线 + 线性缩放

```wgsl
si = rp(sh);                                   // 归一化（供光照用）
sh = sh × ((refractionDistance - 1.0) × 0.5);  // 线性缩放
```

### 步骤 5：非线性增强

```wgsl
sh = sign(sh) × pow(abs(sh), vec2(1.4));
```

### 步骤 6：边缘衰减

```wgsl
pb = qx(sf);                                        // 采样 bevel SDF
sj = smoothstep(0, 0.5, 1 - pb);                    // 边缘权重
sk = vec2(0.999) × refractionDistance;              // 最大偏移上限

sl = refractionIntensity × sh × refractionDistance; // 全强度偏移
sl = clamp(sl, -sk, sk);                            // 钳制
sm = refractionIntensity × sh × 0.2 × sk;           // 弱化版偏移
sn = mix(sl, sm, vec2(sj));                         // 边缘处用弱化版
```

| 位置 | SDF (pb) | sj (边缘权重) | 折射强度 |
|------|---------|--------------|----------|
| 内部深处 | +1 | 0 | **全强度** (`sl`) |
| 边界附近 | 0 | 1 | **0.2× 弱化** (`sm`) |
| 外部 | −1 | 1 | 弱化 |

### 步骤 7：应用折射偏移

```wgsl
so = sf + sn;  // 最终采样坐标
```

### 步骤 8：色散采样（可选）

```wgsl
if (chromaticAberration < 0.001) {
    sp = pw(so);                                // 单采样
} else {
    sp = qa(so, sn, chromaticAberration);        // 8 次采样 RGB 分离
}
```

### 步骤 9：光照计算

```wgsl
ss = (lightAngle - frameRotation) + π/2;
st = -(cos(ss), sin(ss));                         // 光源方向

su = smoothstep(0.35, 1.0, dot(-si, -st)) × sj × lightIntensity;  // 漫反射
sv = smoothstep(0.35, 1.0, dot(-si, st)) × sj × lightIntensity × 0.6;  // 镜面 rim

sp.rgb += vec3(su + sv);
```

---

## 6. 参数影响总表

### 6.1 完整参数表

| 参数 | 类型 | 影响步骤 | 作用 | 效果 |
|------|------|---------|------|------|
| `nodeToTileSpace` | mat3x3 | 坐标变换 | 节点空间 → Tile 空间 | 决定采样位置映射 |
| `tileToNodeSpace` | mat3x3 | 坐标变换 | Tile 空间 → 节点空间 | 屏幕坐标回溯 |
| `nodeSpaceCenterPoint` | vec2 | Splay / 光照 | 中心参考点 | splay 混合的目标方向 |
| `tileExpansionScale` | f32 | 背景采样 | 背景纹理展开范围 | >1 采样更外围区域 |
| `heightExpansionScale` | f32 | 高度图采样 | 高度图展开范围 | 大→梯度更平滑 |
| `bevelExpansionScale` | f32 | Bevel 采样 | Bevel 纹理展开范围 | 控制 SDF 采样范围 |
| `heightMapTexelSize` | f32 | （未使用） | 高度图纹素尺寸 | v1 中为冗余字段 |
| `frameRotation` | f32 | 光照 | 框架旋转角 | 矫正光源方向到节点空间 |
| `refractionIntensity` | f32 | 步骤 6 | 折射强度乘数 | 0=无折射, 1=全强度 |
| `refractionDistance` | f32 | 步骤 2,4,6 | 差分步长 + 缩放 + 偏移 + clamp | 越大折射越强越宽 |
| `splay` | f32 | 步骤 3 | 梯度方向与向心方向混合 | 0=纯梯度, 1=纯向心 |
| `lightIntensity` | f32 | 步骤 9 | 光照强度 | 0=无光照 |
| `lightAngle` | f32 | 步骤 9 | 光源角度 | 控制高光位置 |
| `bevelSize` | f32 | 步骤 6（间接） | 通过 qx() 影响边缘权重 | 控制边缘衰减带宽度 |
| `chromaticAberration` | f32 | 步骤 8 | 色散强度 | <0.001 跳过色散 |

### 6.2 `refractionDistance` 的四重作用

这是最关键的参数，它在四个地方出现：

```
refractionDistance (d)
   │
   ├──[1] rf() 差分步长:  step = clamp(d, 1, 6)
   │      → d 越大, 梯度采样间距越大, 梯度越平滑
   │
   ├──[2] 线性缩放:  sh × (d-1)/2
   │      → d=1 时为 0 (无折射)
   │      → d 越大, 缩放越强
   │
   ├──[3] 最终偏移乘数:  sl = I × sh × d
   │      → d 越大, 偏移越远
   │
   └──[4] clamp 上限:  sk = 0.999 × d
          → d 越大, 允许的最大偏移越大
```

**直观理解：** `refractionDistance` 同时控制了"玻璃的厚度"（梯度平滑度）和"光线偏转的距离"（偏移大小）。值越大，玻璃越厚、折射越强。

### 6.3 参数交互关系

```
refractionIntensity × refractionDistance = 折射偏移的基础幅度
                                        ↓
                              × pow(梯度, 1.4)
                                        ↓
                              × 边缘衰减 (bevel)
                                        ↓
                              最终折射偏移
                              
splay 控制方向: 梯度方向 ←→ 向心方向
                                        ↓
                              折射偏移的方向

chromaticAberration 控制色散: 偏移 × (1±disp) 分别采样 RGB
```

---

## 7. 折射距离的最终公式

### 7.1 完整公式

\[
\vec{O} = \text{mix}\left(\vec{O}_{full},\, \vec{O}_{reduced},\, W_{edge}\right)
\]

其中：

\[
\vec{O}_{full} = I \cdot \text{sgn}(\vec{D}) \cdot \left|\vec{D} \cdot \frac{d-1}{2}\right|^{1.4} \cdot d
\]

\[
\vec{O}_{reduced} = I \cdot \text{sgn}(\vec{D}) \cdot \left|\vec{D} \cdot \frac{d-1}{2}\right|^{1.4} \cdot 0.2 \cdot 0.999 \cdot d
\]

\[
\vec{D} = \text{mix}\left(\hat{\nabla}h,\; -\hat{(p-c)},\; s\right) \cdot |\nabla h|
\]

\[
W_{edge} = \text{smoothstep}(0,\, 0.5,\, 1 - B), \quad B = \text{texture}_2(p).a \times 2 - 1
\]

变量定义：
- \(\nabla h\) = 高度图梯度（来自 `rf()`）
- \(I\) = `refractionIntensity`
- \(d\) = `refractionDistance`
- \(s\) = `splay`
- \(c\) = `nodeSpaceCenterPoint`
- \(B\) = bevel 纹理 SDF 值

### 7.2 最终采样坐标

\[
\vec{p}_{sample} = \vec{p} + \vec{O}
\]

---

## 8. 完整流程图

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   背景内容       │    │    图层形状       │    │   边缘检测       │
│ (所有下方图层)    │    │  (五角星Path)     │    │                 │
└────────┬────────┘    └────────┬─────────┘    └────────┬────────┘
         │                      │                       │
         ▼ 离屏渲染             ▼ Alpha 光栅化            ▼ 
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│  背景 RGBA       │    │  二值 Alpha      │    │   边缘 Alpha     │
└────────┬────────┘    │  (0/1)           │    └────────┬────────┘
         │             └────────┬─────────┘             │
         ▼ frost 模糊           ▼ 高斯模糊 (σ 大)         ▼ SDF + 法线检测
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   texture0_     │    │  模糊 Alpha      │    │   texture2_     │
│  RGBA8 颜色      │    │  (≈ 归一化 UDF)   │    │  RGBA8          │
│  (模糊背景)      │    └────────┬─────────┘    │  RGB=法线        │
└────────┬────────┘             │              │  A=SDF           │
         │                      ▼ RGBA8 打包    └────────┬────────┘
         │             ┌──────────────────┐             │
         │             │   texture1_      │             │
         │             │  RGBA8 打包 float │             │
         │             │  (高度图)         │             │
         │             └────────┬─────────┘             │
         │                      │                       │
         │ tileExpansionScale   │ heightExpansionScale   │ bevelExpansionScale
         ▼                      ▼                       ▼
    ┌─────────────────────────────────────────────────────────┐
    │                     着色器 se()                          │
    │                                                         │
    │  步骤 1: 坐标转换                                        │
    │    sf = tileToNodeSpace × uv                            │
    │                                                         │
    │  步骤 2: 高度图梯度 rf(sf, d):                           │
    │    step = clamp(d, 1, 6)                                │
    │    4× 采样 texture1_ → bf() 解包 → 中心差分              │
    │    → ∇h (梯度向量, 内部非零)                              │
    │                                                         │
    │  步骤 3: Splay 混合 ru(∇h, sf):                          │
    │    gradDir = normalize(∇h)                              │
    │    centerDir = -normalize(sf - center)                  │
    │    mixedDir = mix(gradDir, centerDir, splay)  ← splay   │
    │    → ∇h_splay (保留 |∇h|)                                │
    │                                                         │
    │  步骤 4: 保存法线 + 线性缩放:                              │
    │    si = normalize(∇h_splay)  ← 供光照                    │
    │    sh = ∇h_splay × (d-1)/2   ← d                        │
    │                                                         │
    │  步骤 5: 非线性增强:                                      │
    │    sh = sign(sh) × |sh|^1.4                             │
    │                                                         │
    │  步骤 6: 边缘衰减:                                       │
    │    pb = qx(sf) = texture2.a × 2 - 1  ← bevelSize(间接)  │
    │    sj = smoothstep(0, 0.5, 1-pb)                        │
    │    sk = 0.999 × d                                       │
    │    sl = I × sh × d            ← I, d                     │
    │    sl = clamp(sl, ±sk)                                  │
    │    sm = I × sh × 0.2 × sk     (弱化版)                    │
    │    sn = mix(sl, sm, sj)                                 │
    │                                                         │
    │  步骤 7: 应用偏移:                                       │
    │    so = sf + sn                                         │
    │                                                         │
    │  步骤 8: 色散采样 (可选):                                 │
    │    if chromaticAberration < 0.001:                      │
    │      sp = pw(so)  ← texture0_ (tileExpansionScale)      │
    │    else:                                                │
    │      sp = qa(so, sn, chromaticAberration)  ← 8× 采样    │
    │                                                         │
    │  步骤 9: 光照:                                           │
    │    ss = (lightAngle - frameRotation) + π/2              │
    │    st = -(cos(ss), sin(ss))                             │
    │    diffuse = smoothstep(0.35, 1, N·L) × sj × intensity  │
    │    specular = smoothstep(0.35, 1, N·(-L)) × sj ×        │
    │              intensity × 0.6                            │
    │    sp.rgb += diffuse + specular                         │
    │                                                         │
    │  → fig_FragColor = sp                                   │
    └─────────────────────────────────────────────────────────┘
```

---

## 9. 五角星实例

### 9.1 模糊后的高度场

```
五角星模糊 Alpha (σ ≈ 10):

          .05
         .15.15
       .10 .30 .10
     .05 .25 .50 .25 .05
   .05 .20 .45 .70 .45 .20 .05
     .15 .40 .65 .85 .65 .40 .15
       .30 .55 .80 .55 .30              ← 星形体积
     .15 .40 .65 .85 .65 .40 .15        ← 中心最高
   .05 .20 .45 .70 .45 .20 .05
     .05 .25 .50 .25 .05
       .10 .30 .10
         .15.15
          .05

尖角处: 值低 (.05~.30) → 梯度大 → 折射强
中心处: 值高 (.70~.85) → 梯度小 → 折射弱但非零
```

### 9.2 梯度方向图

```
      ↗ ↑ ↖            ← 上尖角: 梯度指向内部 (垂直于斜面)
    ↗   ↑   ↖
  →   ↗ ↑ ↖   ←
→  →   → ★ ←   ←  ←   ← 中心: 梯度从各方向指向中心
  →   ↙ ↓ ↘   ←           (即远离最近边的方向)
    ↙   ↓   ↘
      ↙ ↓ ↘            ← 下尖角: 同理
```

梯度方向 = 远离最近边缘的方向 = 形状"最厚"的方向

### 9.3 折射效果分布

| 区域 | 模糊 Alpha 梯度 | SDF (bevel) | 折射效果 |
|------|----------------|-------------|----------|
| 尖角尖端 | 大（快速从 0→0.3） | ≈ 0（边缘） | **强折射 + 边缘衰减** |
| 尖角内侧 | 中（从 0.3→0.6） | > 0（内部） | 中等折射（全强度） |
| 中心区域 | 小（从 0.7→0.85） | +1（深处） | **弱但非零折射** |
| 外部 | 0 | −1 | 无折射 |

---

## 10. 数学原理

### 10.1 高斯模糊对二值函数的效果

对于二值函数 `α(x) = 1 if x ∈ Ω, 0 otherwise`，高斯卷积：

\[
h(p) = (G_\sigma * \alpha)(p) = \int_\Omega G_\sigma(p - q) \, dq
\]

在边界附近，`h(p)` 近似于：

\[
h(p) \approx \Phi\left(\frac{d(p)}{\sigma}\right)
\]

其中 `d(p)` 是 p 到 ∂Ω 的有符号距离，`Φ` 是标准正态 CDF。

### 10.2 梯度

\[
\nabla h(p) \approx \frac{1}{\sigma} \phi\left(\frac{d(p)}{\sigma}\right) \cdot \hat{n}(p)
\]

其中 `φ` 是标准正态 PDF，`n̂(p)` 是最近边界点的**内法线**。

**关键性质：**
- `|∇h|` 在边缘处最大（`d ≈ 0` 时 `φ(0) = 0.399`）
- `|∇h|` 在内部逐渐减小（`d >> σ` 时 `φ(d/σ) → 0`）
- `∇h` 的**方向**处处 = 远离最近边的内法线方向

### 10.3 与精确 UDF 的对比

| 性质 | 精确 UDF | 模糊 Alpha |
|------|---------|-----------|
| 内部梯度模 | 恒定 = 1 | 边缘大、中心小（高斯衰减） |
| 梯度方向 | = 内法线 ✓ | = 内法线 ✓ |
| 中轴线行为 | 不可微（梯度跳变） | 平滑连续 |
| 折射特性 | 均匀（全部一样强） | **边缘强、中心弱**（更自然） |

---

## 11. 与 v2（SDF）的对比

| | v1（模糊 Alpha 高度图 / 本文档） | v2（SDF 解析） |
|---|---|---|
| 支持形状 | **任意形状** | 仅圆角矩形 |
| 纹理数 | 3 | 1 |
| 高度来源 | 模糊 Alpha（近似 UDF） | 解析 SDF |
| 内部折射 | ✓（梯度非零） | ✓（SDF 梯度恒非零）|
| 额外 Pass | 2 次模糊 Pass + Alpha 渲染 + Bevel 生成 | 无 |
| 法线精度 | 受模糊核和纹理分辨率影响 | 数学精确 |
| 中轴线行为 | 平滑（无跳变） | 需特殊处理 |
| 性能 | 多纹理 + 模糊开销 | 解析计算，无额外纹理 |
