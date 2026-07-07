# GlassStyle Shader 分析文档

## 1. 概述

GlassStyle 模拟光线穿过玻璃表面的物理行为，产生折射、色散、磨砂模糊和边缘高光效果。它捕获图层下方的背景内容，通过基于图层形状推导的位移映射进行光学扭曲渲染。

**核心文件**：

| 文件 | 职责 |
|------|------|
| `include/tgfx/layers/layerstyles/GlassStyle.h` | 公开 API，定义参数和接口 |
| `src/layers/layerstyles/GlassStyle.cpp` | CPU 侧渲染流程编排（Frost 模糊、形状检测、Mask 生成、最终裁剪） |
| `src/layers/layerstyles/GlassRefractionEffect.h/.cpp` | GPU RuntimeEffect，管理 pipeline 创建、uniform 上传、纹理绑定 |
| `src/layers/layerstyles/GlassShaderFragments.h` | GLSL shader 片段（header、SDF 模块、main 函数） |

---

## 2. 参数说明

### 2.1 用户可设置参数

| 参数 | 类型 | 范围 | 默认值 | 语义 |
|------|------|------|--------|------|
| `refraction` | float | [0, 100] | 50 | 折射强度。控制光线穿过玻璃时的偏转程度。0=无折射（平板玻璃），100=最大折射 |
| `depth` | float | [0, 100] | 15 | 玻璃厚度/深度。控制玻璃截面凸起程度，影响边缘到中心的过渡范围、折射距离缩放、AlphaMask 模糊 sigma |
| `frost` | float | [0, 100] | 10 | 磨砂模糊。对背景做高斯模糊模拟磨砂玻璃。0=透明，100=最大模糊（sigma=50×contentScale） |
| `dispersion` | float | [0, 100] | 0 | 色散强度。RGB 三通道以不同偏移采样模拟棱镜色散。0=无色散 |
| `splay` | float | [0, 100] | 50 | 方向扩散。控制折射方向来源。0=纯梯度/径向方向，100=纯向心方向 |
| `lightAngle` | float | [0, 360] | 135 | 光源方向角度（度）。**注意：当前 shader 未实际使用此参数** |
| `lightIntensity` | float | [0, 100] | 50 | 边缘高光亮度。**注意：当前 shader 未实际使用此参数** |
| `cornerRadius` | float | ≥0 | 0 | 圆角半径，仅 RoundedRect 模式有效。若 layerContent 为 RRect 则自动从其 radii 提取 |
| `shapeType` | enum | — | 自动检测 | 形状类型：RoundedRect / Ellipse / AlphaMask，由 GlassStyle 根据 layerContent 自动检测，不可手动设置 |

> **重要说明**：`lightAngle` 和 `lightIntensity` 虽然在 CPU 侧计算并写入 shader uniform（`uParams4.z` 和 `uParams4.w`），但 `GLASS_SHADER_MAIN` 中并未读取这两个值，当前版本不产生任何光照高光效果。`onDraw` 中 `_lightIntensity > 0` 仅用于决定是否执行折射 Pass，不产生实际光照。

### 2.2 内部派生参数

CPU 侧（`GlassStyle::onDraw`）从用户参数和内容尺寸推导以下值：

| 派生参数 | 公式 | 语义 |
|----------|------|------|
| `halfW` / `halfH` | `layerWidth × 0.5` / `layerHeight × 0.5` | 玻璃半尺寸 |
| `minHalf` | `min(halfW, halfH)` | 短边半径，用于归一化 |
| `crRadius` | `min(cornerRadius × contentScale, minHalf)` | 缩放后的圆角半径 |
| `refractionFactor` | `refraction / 100` | 归一化折射系数 [0, 1] |
| `depthRatio` | `min((depth/100 × maxDepth) / maxDepth, 1)` ≈ `depth / 100` | 归一化深度 [0, 1]，maxDepth = minHalf - 1 |
| `glassThickness` | `(1 + depthRatio × (minHalf-1)) × (1 + refractionFactor)` | 等效玻璃厚度（像素），乘以 1~2 的折射倍率 |
| `flatRatio` | depthRatio ≤ 0.5: `1 - depthRatio × 1.4`; 否则 `0.3` | 内部平坦区域占比（1=全平，0.3=窄边带） |
| `innerHalfW/H` | `halfW/H × flatRatio` | 内形状（平坦区域边界）半尺寸 |
| `innerRadius` | `crRadius × flatRatio` | 内形状圆角 |
| `channelOffset` | `(dispersion / 100) × 0.2` | 色散通道偏移系数 |
| `splay` (shader) | `splay / 100` | 归一化方向扩散 [0, 1] |
| `blurSigma` (AlphaMask) | `minHalf × (0.2 + depth/100 × 0.2)` | UDF 高度图模糊核大小 |

### 2.3 参数交互关系

```
refraction ──→ refractionFactor ──→ offsetDist 缩放
                                 └→ glassThickness ──→ offsetDist 缩放 (解析 SDF 路径)

depth ──→ depthRatio ──→ glassThickness ──→ offsetDist (解析 SDF 路径)
      │              └→ refDist ──→ offsetDist (AlphaMask 路径)
      │              └→ flatRatio → innerHalf → 边缘带宽度 (解析 SDF 路径)
      └→ blurSigma ──→ UDF 平缓程度 (AlphaMask 路径)

splay ──→ 折射方向混合权重
       解析 SDF: axisThreshold → axisWeight (smoothstep)
       AlphaMask:   mix(gradDir, centerDir, splay)

dispersion ──→ RGB 三通道偏移差: offset × (1±dispersion)

frost ──→ 背景高斯模糊 sigma (独立于折射，预处理阶段)

lightAngle + lightIntensity ──→ 传入 uniform 但 shader 未使用（当前版本无光照效果）
```

---

## 3. 形状类型与 SDF 模块

`GlassShapeType` 决定 shader 中使用哪个 SDF 模块。GlassStyle 根据 `layerContent` 的类型自动检测形状，不需要用户手动设置。解析 SDF 形状（RoundedRect、Ellipse）使用 `#define GLASS_USE_AXIS_MIX` 编译分支，AlphaMask 使用纹理采样分支。

### 3.1 RoundedRect（圆角矩形）

```glsl
float outerShapeSDF(float px, float py) {
    float qx = abs(px) - hw + r;
    float qy = abs(py) - hh + r;
    float outsideDist = sqrt(max(qx,0)² + max(qy,0)²);
    float insideDist = min(max(qx, qy), 0);
    return outsideDist + insideDist - r;
}
```
- SDF < 0 → 在形状内；SDF = 0 → 在边缘上；SDF > 0 → 在形状外
- innerShapeSDF 使用 innerHalfW/H/innerRadius，定义内部平坦区域边界

### 3.2 Ellipse（椭圆）

```glsl
float outerShapeSDF(float px, float py) {
    float nx = px / hw;
    float ny = py / hh;
    float d = length(vec2(nx, ny)) - 1.0;
    return d * min(hw, hh);
}
```
- 将坐标归一化到单位圆空间，再乘回短边

 AlphaMask（任意形状）

使用高斯模糊后的 alpha 作为近似 UDF（Unsigned Distance Field）高度图：

```glsl
float outerShapeSDF(float px, float py) {
    return 0.01 - sampleHeight(px, py);
}
```
- `sampleHeight` 从 `uMask` 纹理采样并 RGBA8 解包恢复 float
- height > 0.01 → SDF < 0 → 在形状内

---

## 4. 渲染流程

### 4.1 整体架构

```
┌──────────────────────────────────────────────────────────────────┐
│  Layer 系统                                                       │
│                                                                   │
│  1. 渲染 glass 层自身内容 → input.content (形状 alpha + 填充)     │
│  2. 捕获下方背景 → input.extraSource (extraSourceType=Background) │
│  3. 调用 GlassStyle::onDraw(canvas, input, alpha, blendMode)     │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│  GlassStyle::onDraw (CPU 侧)                                     │
│                                                                   │
│  Step 1: Frost 模糊（可选）                                       │
│    bgImage → ImageFilter::Blur(sigma) → processedBg              │
│    sigma = (frost/100) × 50 × contentScale                       │
│                                                                   │
│  Step 2: 形状类型自动检测                                         │
│    layerContent 类型 → effectiveShapeType + crRadius             │
│    RRect → RoundedRect (从 radii 提取圆角)                        │
│    Rect  → RoundedRect (crRadius=0)                               │
│    Path/Shape + oval → Ellipse                                    │
│    Path/Shape 无 oval → AlphaMask                                 │
│                                                                   │
│  Step 3: UDF 高度图生成（仅 AlphaMask）                           │
│    input.content → Blur(blurSigma) → GlassMaskEffect(RGBA8打包)  │
│    → maskImage                                                    │
│                                                                   │
│  Step 4: 折射 Pass（GPU RuntimeEffect）                           │
│    processedBg → GlassRefractionEffect → refractedImage           │
│                                                                   │
│  Step 5: 最终裁剪绘制                                             │
│    用 input.content 的 alpha 做 MaskFilter                        │
│    canvas->drawImage(refractedImage, offset, mask)                │
└──────────────────────────────────────────────────────────────────┘
```

### 4.2 Step 1: Frost 模糊

```cpp
float sigma = (_frost / 100.0f) * MaxFrostSigma * contentScale;  // MaxFrostSigma = 50
frostFilter = ImageFilter::Blur(sigma, sigma, TileMode::Mirror);
processedBg = bgImage->makeWithFilter(blurFilter, &blurOffset, &clipRect);
```
- 模糊结果缓存（`frostFilter`），contentScale 不变时复用
- frost=0 时跳过，直接使用原始背景

### 4.3 Step 2: 形状类型自动检测

即使 `layerContent` 存在，`onDraw` 仍会根据 `layerContent` 的实际类型自动检测形状：

| layerContent 类型 | effectiveShapeType | crRadius 来源 |
|---|---|---|
| RRect | RoundedRect | `min(radii[0].x, radii[0].y) × contentScale` |
| Rect | RoundedRect | 0 |
| Path/Shape + oval | Ellipse | — |
| Path/Shape 无 oval | AlphaMask | — |
| 其他 | AlphaMask | — |

### 4.4 Step 3: UDF 高度图生成（仅 AlphaMask）

AlphaMask 路径需要将任意形状的 alpha 转为高度图，分两个 GPU Pass：

**Pass A: 高斯模糊**

```cpp
float blurSigma = minHalf * (0.2f + (_depth / 100.0f) * 0.2f);
auto blurFilter = ImageFilter::Blur(blurSigma, blurSigma, TileMode::Decal);
blurredMask = input.content->makeWithFilter(blurFilter, ...);
```
- 二值 alpha 经高斯模糊后，边缘 ≈ 0，中心 ≈ max，近似 UDF

**Pass B: RGBA8 精度打包（GlassMaskEffect）**

由于 8 位纹理精度不足以存储 [0,1] 浮点高度，使用 `GlassMaskEffect` 将 float 打包为 RGBA8 四通道（32 位精度）：

```glsl
// 打包 (GlassMaskEffect fragment shader)
float value = texture(uSource, vTexCoord).a;
vec4 enc = vec4(1.0, 255.0, 65025.0, 16581375.0) * value;
enc = fract(enc);
enc -= enc.yzww * vec4(1.0/255.0, 1.0/255.0, 1.0/255.0, 0.0);
tgfx_FragColor = enc;

// 解包 (refraction shader 中)
const vec4 UNPACK_FACTORS = vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0);
float height = dot(packed, UNPACK_FACTORS);
```

### 4.5 Step 4: 折射 Pass（GlassRefractionEffect）

创建 `GlassRefractionEffect` RuntimeEffect 并应用为 ImageFilter：

```cpp
auto refractionEffect = std::make_shared<GlassRefractionEffect>(
    layerWidth, layerHeight, halfW, halfH, crRadius, minHalf,
    innerHalfW, innerHalfH, innerRadius, glassThickness,
    refractionFactor, channelOffset, splay, depthRatio,
    lightAngle, lightIntensity, effectiveShapeType, maskImage);
auto refractionFilter = ImageFilter::Runtime(refractionEffect);
refractedImage = processedBg->makeWithFilter(refractionFilter, ...);
```

GPU 侧细节见第 5 节。

### 4.6 Step 5: 最终裁剪

```cpp
auto maskShader = Shader::MakeImageShader(input.content, TileMode::Decal, TileMode::Decal);
Paint paint = {};
paint.setMaskFilter(MaskFilter::MakeShader(maskShader, false));
paint.setBlendMode(BlendMode::Src);
canvas->drawImage(processedBg, processedOffset.x, processedOffset.y, &paint);
```
- 用 glass 层自身的 alpha 裁剪折射后的背景，形状外部透明
- `BlendMode::Src` 直接替换，不做混合

---

## 5. Shader 行为详解

### 5.1 Shader 组装

`GlassRefractionEffect::buildFragmentShader()` 按形状类型拼接 shader：

```
#version 150 / #version 300 es      ← 桌面/移动端
GLASS_SHADER_HEADER                   ← uniform 声明 + precision
[#define GLASS_USE_AXIS_MIX]          ← 解析 SDF 形状才定义
GLASS_SDF_ROUNDED_RECT / ELLIPSE / ALPHA_MASK  ← 对应 SDF 模块
GLASS_SHADER_MAIN                     ← main 函数
```

### 5.2 Uniform 布局（std140）

```
layout(std140) uniform GlassUniforms {
    vec4 uParams0;  // glassOffsetX, glassOffsetY, glassScaleX, glassScaleY
    vec4 uParams1;  // halfW, halfH, cornerRadius, minHalf
    vec4 uParams2;  // innerHalfW, innerHalfH, innerRadius, glassThickness
    vec4 uParams3;  // refractionFactor, dispersion, 1/sourceWidth, 1/sourceHeight
    vec4 uParams4;  // splay, depthRatio, lightAngleRad, lightIntensity
};
```

**uParams0（UV 映射参数）**：由 CPU 侧根据 glass 尺寸与 source 纹理尺寸计算：

```cpp
glassOffsetX = (glassWidth < sourceWidth) ? (1 - glassWidth/sourceWidth) * 0.5 : 0;
glassOffsetY = (glassHeight < sourceHeight) ? (1 - glassHeight/sourceHeight) * 0.5 : 0;
glassScaleX = sourceWidth / glassWidth;
glassScaleY = sourceHeight / glassHeight;
```
当背景纹理大于 glass 区域时，居中映射。

**uParams4 中 lightAngleRad 和 lightIntensity 当前未被 shader 使用**。

### 5.3 顶点着色器

```glsl
in vec2 aPosition;
in vec2 aTextureCoord;
out vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTextureCoord;
}
```
- 4 顶点 TriangleStrip，覆盖 source 纹理区域
- NDC 坐标由 `collectVertices` 根据 source/target 尺寸和 offset 计算

### 5.4 MSAA 抗锯齿

折射 Pass 使用 4× MSAA（`MSAA_SAMPLE_COUNT = 4`）：
```cpp
descriptor.multisample.count = MSAA_SAMPLE_COUNT;
// 创建 MSAA render target，resolve 到 outputTexture
```
Mask 打包 Pass 不使用 MSAA。

### 5.5 纹理绑定

| 绑定槽 | 名称 | 内容 | 采样方式 |
|--------|------|------|----------|
| 0 | `uSource` | 背景（可能已 Frost 模糊） | ClampToEdge + Linear |
| 1 | `uMask` | RGBA8 打包的高度图（仅 AlphaMask） | ClampToEdge + Linear |

---

## 6. 像素采样流程

### 6.1 坐标转换流程

```
vTexCoord (UV 空间 [0,1], Y 向上)
    │
    │  glassUV = (vTexCoord - uParams0.xy) * uParams0.zw
    │  // 减去偏移、乘以缩放，从 source UV 映射到 glass 局部 UV
    ▼
glassUV (glass 局部 UV)
    │
    │  glassUV.y = 1.0 - glassUV.y
    │  // Y 翻转：UV Y 向上 → 像素 Y 向下
    ▼
glassPixel = glassUV × vec2(halfW×2, halfH×2)
    │  // UV 转像素坐标
    ▼
px = glassPixel.x - halfW
py = glassPixel.y - halfH
    │  // 平移到以玻璃中心为原点
    ▼
(px, py)  // 玻璃局部像素坐标，中心为 (0,0)
```

### 6.2 解析 SDF 路径（RoundedRect / Ellipse）

当 `#define GLASS_USE_AXIS_MIX` 时走此路径：

```
输入: (px, py) — 玻璃局部坐标，中心为原点

Step 1: 计算 SDF
┌──────────────────────────────────────────────────┐
│ outerSDF = outerShapeSDF(px, py)                 │
│ innerSDF = innerShapeSDF(px, py)                 │
│                                                  │
│ 外形状定义玻璃边界，内形状定义平坦区域边界         │
└──────────────────────────────────────────────────┘
         │
         ▼
Step 2: 判断是否在边缘带内
┌──────────────────────────────────────────────────┐
│ if (outerSDF < 0.0 && innerSDF >= 0.0):          │
│   // 在外形状内、内形状外 → 边缘带 → 有折射       │
│ elif (outerSDF >= 0.0):                          │
│   // 在形状外 → uvOffset = 0（最终被 mask 裁掉） │
│ else (outerSDF < 0 && innerSDF < 0):             │
│   // 在平坦区域 → uvOffset = 0（无折射）         │
└──────────────────────────────────────────────────┘
         │
         ▼
Step 3: 计算归一化边缘位置
┌──────────────────────────────────────────────────┐
│ edgeDist = -outerSDF           // 到外边缘距离    │
│ totalDist = edgeDist + innerSDF  // 边缘带总宽度  │
│ xNorm = edgeDist / totalDist   // 0=外缘, 1=内缘 │
│ edgeFactor = 1.0 - xNorm       // 外缘=1, 内缘=0 │
│ edgeFactor = min(edgeFactor, 1.0)                │
└──────────────────────────────────────────────────┘
         │
         ▼
Step 4: 计算折射距离
┌──────────────────────────────────────────────────┐
│ offsetDist = glassThickness                      │
│             × refractionFactor                   │
│             × edgeFactor²                        │
│             × 2.0                                │
│                                                  │
│ // edgeFactor² 使折射强度在边缘最强，向内快速衰减  │
└──────────────────────────────────────────────────┘
         │
         ▼
Step 5: 计算折射方向（径向 + 轴向混合）
┌──────────────────────────────────────────────────┐
│ // 径向方向：指向中心                             │
│ dirLen = sqrt(px² + py²)                         │
│ if dirLen < 0.001: 跳过（中心点无方向）           │
│ radialDir = vec2(-px/dirLen, -py/dirLen)         │
│                                                  │
│ // 轴向方向：选最近坐标轴                         │
│ if |radialDir.x| > |radialDir.y|:                │
│   axisDir = (sign(radialDir.x), 0)               │
│ else:                                            │
│   axisDir = (0, sign(radialDir.y))               │
│                                                  │
│ // 混合权重：直边区域用轴方向，角落用径向          │
│ maxAxisDot = max(|radialDir.x|, |radialDir.y|)   │
│ controlValue = depthRatio × splay                │
│ axisThreshold = mix(0.9, 1.0, controlValue)      │
│ axisWeight = smoothstep(axisThreshold, 1.0, dot) │
│                                                  │
│ refractDir = mix(radialDir, axisDir, axisWeight) │
└──────────────────────────────────────────────────┘
         │
         ▼
Step 6: 转为 UV 偏移
┌──────────────────────────────────────────────────┐
│ dispX = refractDir.x × offsetDist                │
│ dispY = refractDir.y × offsetDist                │
│ uvOffset = vec2(dispX × invSourceW,              │
│                 -dispY × invSourceH)              │
│ // Y 翻转：像素 Y 向下，UV Y 向上                 │
└──────────────────────────────────────────────────┘
```

### 6.3 AlphaMask 路径

当形状为 AlphaMask 时（不定义 `GLASS_USE_AXIS_MIX`）：

```
输入: (px, py) — 玻璃局部坐标，中心为原点

Step 1: 计算高度
┌──────────────────────────────────────────────────┐
│ height = sampleHeight(px, py)                    │
│                                                  │
│ // 内部采样过程:                                  │
│ uv = ((px + halfW) / (halfW×2),                  │
│       1 - (py + halfH) / (halfH×2))             │
│ packed = texture(uMask, uv)   // RGBA8 采样       │
│ height = dot(packed, UNPACK_FACTORS)             │
│ // UNPACK_FACTORS = vec4(1, 1/255,               │
│ //            1/65025, 1/16581375)               │
└──────────────────────────────────────────────────┘
         │
         ▼
Step 2: 判断是否在形状内
┌──────────────────────────────────────────────────┐
│ outerSDF = 0.01 - height                         │
│ if (outerSDF >= 0.0): uvOffset = 0  // 形状外    │
└──────────────────────────────────────────────────┘
         │
         ▼
Step 3: 中心差分梯度方向
┌──────────────────────────────────────────────────┐
│ step = minHalf / 20 + 1   // 自适应步长           │
│                                                  │
│ left   = sampleHeight(px - step, py)             │
│ right  = sampleHeight(px + step, py)             │
│ bottom = sampleHeight(px, py - step)             │
│ top    = sampleHeight(px, py + step)             │
│                                                  │
│ grad = (right - left, top - bottom) / (2 × step) │
│ gradLen = length(grad)                           │
│                                                  │
│ if gradLen < 0.0001: 无梯度，跳过折射             │
│ gradDir = grad / gradLen  // 归一化方向           │
└──────────────────────────────────────────────────┘
         │
         ▼
Step 4: Splay 混合方向
┌──────────────────────────────────────────────────┐
│ dirLen = sqrt(px² + py²)                         │
│ centerDir = (dirLen > 0.001)                     │
│             ? vec2(-px/dirLen, -py/dirLen)       │
│             : vec2(0.0)                          │
│ mixedDir = mix(gradDir, centerDir, splay)        │
│ mixedDir = normalize(mixedDir)                   │
└──────────────────────────────────────────────────┘
         │
         ▼
Step 5: 计算折射距离
┌──────────────────────────────────────────────────┐
│ refDist = halfW                                  │
│          × (0.5 × refractionFactor               │
│             + 0.5 × depthRatio)                  │
│                                                  │
│ edgeProximity = 1.0 - height                     │
│ // 边缘 height≈0 → 全强度                        │
│ // 中心 height≈max → 0                           │
│                                                  │
│ offsetDist = refDist × edgeProximity             │
└──────────────────────────────────────────────────┘
         │
         ▼
Step 6: 转为 UV 偏移
┌──────────────────────────────────────────────────┐
│ sn = mixedDir × offsetDist                       │
│ sk = vec2(0.999) × refDist   // clamp 上限       │
│ sn = clamp(sn, -sk, sk)                          │
│                                                  │
│ uvOffset = vec2(sn.x × invSourceW,               │
│                 -sn.y × invSourceH)              │
│ // Y 翻转                                         │
└──────────────────────────────────────────────────┘
```

### 6.4 色散采样（公共最终步骤）

两种路径计算完 `uvOffset` 后，执行相同的色散采样：

```glsl
vec2 uvR, uvG, uvB;
if (dispersion < 0.01) {
    // 无色散：三通道同一 UV
    uvR = uvG = uvB = clamp(vTexCoord + uvOffset, vec2(0.0), vec2(1.0));
} else {
    // 色散：R 偏移更大，B 偏移更小
    uvR = clamp(vTexCoord + uvOffset * (1.0 + dispersion), vec2(0.0), vec2(1.0));
    uvG = clamp(vTexCoord + uvOffset, vec2(0.0), vec2(1.0));
    uvB = clamp(vTexCoord + uvOffset * (1.0 - dispersion), vec2(0.0), vec2(1.0));
}

// 分别采样各通道
float r = texture(uSource, uvR).r;
float g = texture(uSource, uvG).g;
float b = texture(uSource, uvB).b;
float a = texture(uSource, uvG).a;   // alpha 取绿色通道的 UV

tgfx_FragColor = vec4(r, g, b, a);
```

- `dispersion` 在 CPU 侧已归一化为 `(dispersion/100) × 0.2`，范围 [0, 0.2]
- R 通道偏移放大，B 通道偏移缩小，产生棱镜色散效果
- Alpha 始终取 G 通道的 UV（无色散）

### 6.5 纹理采样次数统计

| 路径 | uSource 采样 | uMask 采样 | 总计 |
|------|-------------|-----------|------|
| RoundedRect / Ellipse | 4 次 (R/G/B/A) | 0 | 4 |
| AlphaMask | 4 次 (R/G/B/A) | 5 次 (center + 4 邻居) | 9 |

---

## 7. 两种路径对比

| | 解析 SDF (RoundedRect/Ellipse) | AlphaMask |
|---|---|---|
| **形状来源** | 参数推导（halfW, halfH, cornerRadius） | `input.content` 的 alpha |
| **SDF 计算** | 解析公式（roundedRectSDF / 椭圆） | 模糊 alpha → RGBA8 打包高度图 |
| **额外 GPU Pass** | 无 | 2 个（blur + pack） |
| **额外纹理** | 无（纯计算） | 1 个（uMask 高度图） |
| **折射方向** | `mix(radialDir, axisDir, axisWeight)` — smoothstep 控制轴向/径向混合 | `mix(gradDir, centerDir, splay)` — 线性混合 |
| **折射距离** | `glassThickness × refractionFactor × edgeFactor² × 2` | `refDist × (1 - height)` |
| **边缘判定** | outerSDF < 0 && innerSDF ≥ 0（边缘带） | height > 0.01（在形状内即有折射） |
| **形状限制** | 仅圆角矩形/椭圆 | 任意形状 |
| **性能** | GPU 纯计算，无额外带宽 | 额外 2 Pass + 5 次纹理采样/像素 |
| **抗锯齿** | 4× MSAA | 4× MSAA + 高度图本身有模糊过渡 |

---

## 8. SDF 公式汇总

### 圆角矩形 SDF

```
qx = |px| - hw + r
qy = |py| - hh + r
outside = sqrt(max(qx,0)² + max(qy,0)²)
inside  = min(max(qx, qy), 0)
SDF = outside + inside - r
```

### 椭圆 SDF

```
nx = px / hw
ny = py / hh
d = length(nx, ny) - 1.0
SDF = d × min(hw, hh)
```

### AlphaMask SDF（从高度图推导）

```
height = RGBA8解包(texture(uMask, uv))
SDF = 0.01 - height
// height > 0.01 → SDF < 0 → 在形状内
```
