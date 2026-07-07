# GlassStyle 渲染流程文档

## 1. 资源来源

| 资源 | 来源 | 说明 |
|------|------|------|
| `input.content` | Layer 系统自动渲染 | glass 层自身的渲染结果（形状 alpha + 填充色） |
| `input.extraSource` | Layer 系统自动捕获（`extraSourceType = Background`） | glass 层下方所有已渲染内容的截图 |
| `input.contentScale` | Layer 系统 | 缩放因子 |
| GlassStyle 参数 | 用户设置 | refraction, depth, frost, dispersion, splay, lightAngle, lightIntensity, cornerRadius, shapeType |

---

## 2. 参数语义

### 2.1 用户可设置参数

| 参数 | 范围 | 语义 | 效果 |
|------|------|------|------|
| `refraction` | 0~100 | 折射强度 | 控制光线穿过玻璃时的偏转程度。0=无折射（平板玻璃），100=最大折射（厚透镜） |
| `depth` | 0~100 | 玻璃厚度/深度 | 控制玻璃截面的凸起程度。影响：①边缘到中心的过渡范围 ②折射距离缩放 ③AlphaMask 的模糊 sigma |
| `frost` | 0~100 | 磨砂模糊 | 对背景做高斯模糊，模拟磨砂玻璃。0=透明，100=强模糊 |
| `dispersion` | 0~100 | 色散强度 | RGB 三通道以不同偏移采样，模拟棱镜色散。0=无色散，值越大彩虹效果越明显 |
| `splay` | 0~100 | 方向扩散 | 控制折射方向的来源。0=纯梯度/径向方向，100=纯向心方向（指向中心） |
| `lightAngle` | 0~360° | 光照角度 | 高光/阴影的光源方向角度 |
| `lightIntensity` | 0~100 | 光照强度 | 边缘高光亮度。0=无光照效果 |
| `cornerRadius` | ≥0 | 圆角半径 | 圆角矩形的圆角大小（仅 RoundedRect 模式有效） |
| `shapeType` | enum | 形状类型 | `RoundedRect` / `Ellipse` / `AlphaMask`，决定使用解析 SDF 还是 UDF 高度图 |

### 2.2 内部派生参数

| 参数 | 公式 | 语义 |
|------|------|------|
| `refractionFactor` | `refraction / 100` | 归一化折射系数 [0, 1] |
| `depthRatio` | `(depth/100 × maxDepth) / maxDepth` ≈ `depth / 100` | 归一化深度 [0, 1] |
| `refractionRatio` | `refraction / 100` | 归一化折射系数（等价于 `(ior-1)/9`，ior 为中间变量） |
| `glassThickness` | `(1 + depthRatio × (minHalf-1)) × (1 + refractionRatio)` | 等效玻璃厚度（像素），仅 RoundedRect 使用 |
| `flatRatio` | depth≤50: `1 - depthRatio×1.4`; depth>50: `0.3` | 内部平坦区域占比（1=全平，0.3=窄边带） |
| `innerHalfW/H` | `halfW/H × flatRatio` | 内形状（平坦区域边界）的半尺寸 |
| `minHalf` | `min(halfW, halfH)` | 短边半径，用于归一化 |
| `blurSigma` (AlphaMask) | `minHalf × (0.2 + depth/100 × 0.2)` | UDF 模糊核大小，depth 越大越平缓 |
| `refDist` (AlphaMask) | `halfW × (0.5×refractionFactor + 0.5×depthRatio)` | AlphaMask 折射偏移的缩放基准 |

### 2.3 参数交互关系

```
refraction ──→ refractionFactor ──→ offsetDist 缩放
           └→ ior → glassThickness ──→ offsetDist 缩放 (RoundedRect)

depth ──→ depthRatio ──→ glassThickness ──→ offsetDist (RoundedRect)
      │              └→ refDist ──→ offsetDist (AlphaMask)
      │              └→ flatRatio → innerHalf → 边缘带宽度 (RoundedRect)
      └→ blurSigma ──→ UDF 平缓程度 (AlphaMask)

splay ──→ 折射方向混合权重
       RoundedRect: axisThreshold → axisWeight
       AlphaMask:   mix(gradDir, centerDir, splay)

dispersion ──→ RGB 三通道偏移差: offset × (1±disp)

frost ──→ 背景模糊 sigma (独立于折射)

lightAngle + lightIntensity ──→ 边缘高光 (TODO: 当前仅 RoundedRect 实现)
```

---

## 3. 圆角矩形路径（`GlassShapeType::RoundedRect`）

### 2.1 使用资源

| 资源 | 用途 |
|------|------|
| `input.extraSource->image()` | 背景图——折射采样的源纹理 |
| `input.content` 的宽高 | 确定玻璃尺寸（halfW, halfH） |
| `input.content` 的 alpha | 最终 mask 裁剪形状 |
| `_cornerRadius` 参数 | 解析 SDF 的圆角半径 |

### 2.2 渲染流程

```
┌─────────────────────────────────────────────────────┐
│  CPU 侧 (GlassStyle::onDraw)                       │
├─────────────────────────────────────────────────────┤
│                                                     │
│  1. 获取背景: bgImage = input.extraSource->image()  │
│                                                     │
│  2. Frost 模糊 (可选):                               │
│     bgImage → ImageFilter::Blur(sigma) → processedBg│
│                                                     │
│  3. 计算形状参数 (从参数 + content 尺寸推导):         │
│     halfW, halfH, crRadius, minHalf                 │
│     innerHalfW, innerHalfH (由 depth 决定 flatRatio)│
│     glassThickness (由 depth + refraction 决定)     │
│                                                     │
│  4. 创建 GlassRefractionEffect (Runtime ImageFilter)│
│     输入: processedBg                               │
│     参数: 上述所有形状/折射参数                      │
│     maskImage: nullptr (不需要)                      │
│                                                     │
│  5. processedBg → makeWithFilter(refraction) → 折射图│
│                                                     │
│  6. 最终绘制:                                       │
│     用 input.content 的 alpha 做 MaskFilter 裁剪    │
│     canvas->drawImage(折射图, offset, mask)          │
│                                                     │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│  GPU 侧 (GlassRefractionEffect shader)             │
├─────────────────────────────────────────────────────┤
│                                                     │
│  输入纹理: uSource = processedBg (模糊后的背景)     │
│  Uniform: halfW, halfH, cornerRadius, innerHalfW,  │
│           innerHalfH, innerRadius, glassThickness,  │
│           refractionFactor, dispersion, splay,      │
│           depthRatio                                │
│                                                     │
│  每个像素:                                          │
│  ┌─────────────────────────────────────────────┐    │
│  │ 1. UV → 像素坐标 (px, py), 以中心为原点     │    │
│  │                                             │    │
│  │ 2. 计算解析 SDF:                            │    │
│  │    outerSDF = roundedRectSDF(px, py, ...)   │    │
│  │    innerSDF = roundedRectSDF(px, py, inner) │    │
│  │                                             │    │
│  │ 3. 如果在边缘带内 (outerSDF<0, innerSDF≥0): │    │
│  │    edgeFactor = 1 - edgeDist/totalDist      │    │
│  │    offsetDist = thickness * factor * ef² * 2│    │
│  │                                             │    │
│  │ 4. 折射方向:                                │    │
│  │    radialDir = -normalize(px, py)           │    │
│  │    axisDir = 最近轴方向                      │    │
│  │    refractDir = mix(radial, axis, weight)   │    │
│  │    (weight 由 splay + depthRatio 控制)      │    │
│  │                                             │    │
│  │ 5. UV 偏移 = refractDir * offsetDist / size │    │
│  │                                             │    │
│  │ 6. 色散采样 (可选):                          │    │
│  │    R = sample(uv + offset * (1+disp))       │    │
│  │    G = sample(uv + offset)                  │    │
│  │    B = sample(uv + offset * (1-disp))       │    │
│  └─────────────────────────────────────────────┘    │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### 2.3 关键公式

```
offsetDist = glassThickness × refractionFactor × edgeFactor² × 2.0

其中:
  glassThickness = (1 + depthRatio × (minHalf-1)) × (1 + refractionRatio)
  edgeFactor = 1 - edgeDist / totalDist  (边缘=1, 内部=0)
  refractDir = mix(radialDir, axisDir, axisWeight)
```

---

## 3. AlphaMask 路径（`GlassShapeType::AlphaMask`）

### 3.1 使用资源

| 资源 | 用途 |
|------|------|
| `input.extraSource->image()` | 背景图——折射采样的源纹理 |
| `input.content` | 1. 形状 alpha → 高斯模糊 → UDF 高度图<br>2. 最终 mask 裁剪形状 |
| `input.content` 的宽高 | 确定玻璃尺寸 |
| UDF 高度图 (maskImage) | 传入 shader 作为第二纹理，用于梯度计算和高度采样 |

### 3.2 渲染流程

```
┌─────────────────────────────────────────────────────┐
│  CPU 侧 (GlassStyle::onDraw)                       │
├─────────────────────────────────────────────────────┤
│                                                     │
│  1. 获取背景: bgImage = input.extraSource->image()  │
│                                                     │
│  2. Frost 模糊 (可选):                               │
│     bgImage → ImageFilter::Blur(sigma) → processedBg│
│                                                     │
│  3. 生成 UDF 高度图 (AlphaMask 专有):                │
│     ┌─────────────────────────────────────────┐     │
│     │ input.content (二值 alpha)              │     │
│     │        ↓                                │     │
│     │ ImageFilter::Blur(sigma)                │     │
│     │ sigma = minHalf × (0.2 + depth/100×0.2)│     │
│     │        ↓                                │     │
│     │ 模糊后的 alpha (≈ UDF: 边缘→0, 中心→max)│     │
│     │        ↓                                │     │
│     │ GlassMaskEffect (RGBA8 32位打包)        │     │
│     │ float → vec4(R,G,B,A) 编码             │     │
│     │        ↓                                │     │
│     │ maskImage (高度图纹理)                   │     │
│     └─────────────────────────────────────────┘     │
│                                                     │
│  4. 创建 GlassRefractionEffect (Runtime ImageFilter)│
│     输入: processedBg                               │
│     参数: 形状/折射参数 + maskImage                  │
│                                                     │
│  5. processedBg → makeWithFilter(refraction) → 折射图│
│                                                     │
│  6. 最终绘制:                                       │
│     用 input.content 的 alpha 做 MaskFilter 裁剪    │
│     canvas->drawImage(折射图, offset, mask)          │
│                                                     │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│  GPU 侧 (GlassRefractionEffect shader)             │
├─────────────────────────────────────────────────────┤
│                                                     │
│  输入纹理: uSource = processedBg (模糊后的背景)     │
│            uMask   = maskImage (RGBA8打包的高度图)   │
│  Uniform: halfW, halfH, minHalf, refractionFactor,  │
│           dispersion, splay, depthRatio             │
│                                                     │
│  辅助函数:                                          │
│  sampleHeight(px, py):                              │
│    uv = maskPixelToUV(px, py)                       │
│    packed = texture(uMask, uv)                      │
│    return dot(packed, vec4(1, 1/255, 1/65025,       │
│                            1/16581375))  // 32位解包│
│                                                     │
│  每个像素:                                          │
│  ┌─────────────────────────────────────────────┐    │
│  │ 1. UV → 像素坐标 (px, py), 以中心为原点     │    │
│  │                                             │    │
│  │ 2. outerSDF = 0.01 - sampleHeight(px, py)  │    │
│  │    (height > 0.01 即认为在形状内)           │    │
│  │                                             │    │
│  │ 3. 如果在形状内 (outerSDF < 0):             │    │
│  │                                             │    │
│  │ 4. 采样当前高度:                            │    │
│  │    height = sampleHeight(px, py)            │    │
│  │                                             │    │
│  │ 5. 中心差分取梯度方向:                       │    │
│  │    step = minHalf/20 + 1                    │    │
│  │    grad = (right-left, top-bottom) / 2step  │    │
│  │    gradDir = normalize(grad)                │    │
│  │                                             │    │
│  │ 6. Splay 混合:                              │    │
│  │    centerDir = -normalize(px, py)           │    │
│  │    mixedDir = mix(gradDir, centerDir, splay)│    │
│  │                                             │    │
│  │ 7. 折射距离 (由高度控制):                    │    │
│  │    refDist = halfW × (0.5×refFactor +       │    │
│  │                        0.5×depthRatio)      │    │
│  │    edgeProximity = 1.0 - height             │    │
│  │    offsetDist = refDist × edgeProximity     │    │
│  │                                             │    │
│  │ 8. UV 偏移 = mixedDir × offsetDist / size  │    │
│  │    clamp 到 ±(0.999 × refDist)             │    │
│  │                                             │    │
│  │ 9. 色散采样 (同圆角矩形)                    │    │
│  └─────────────────────────────────────────────┘    │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### 3.3 关键公式

```
offsetDist = refDist × (1 - height)

其中:
  refDist = halfW × (0.5 × refractionFactor + 0.5 × depthRatio)
  height = dot(RGBA8_packed, vec4(1, 1/255, 1/65025, 1/16581375))
  direction = mix(gradientDir, centerDir, splay)
```

---

## 4. 两种路径的对比

| | 圆角矩形 (RoundedRect) | AlphaMask |
|---|---|---|
| **形状来源** | 参数推导（halfW, halfH, cornerRadius） | `input.content` 的 alpha |
| **SDF 计算** | 解析公式 `roundedRectSDF()` | 模糊 alpha → RGBA8 打包高度图 |
| **额外 Pass** | 无 | 2 个（blur + pack） |
| **额外纹理** | 无（纯计算） | 1 个（uMask 高度图） |
| **折射方向** | `mix(radialDir, axisDir, weight)` | `mix(gradDir, centerDir, splay)` |
| **折射距离** | `thickness × factor × edgeFactor² × 2` | `refDist × (1 - height)` |
| **形状限制** | 仅圆角矩形/椭圆 | 任意形状 |
| **性能** | GPU 纯计算，无额外带宽 | 额外 2 个 Pass + 1 次纹理采样 |

---

## 5. 公共最终步骤

两种路径最终都执行相同的收尾：

```cpp
// 用 input.content 的 alpha 裁剪折射后的背景
auto maskShader = Shader::MakeImageShader(input.content, TileMode::Decal, TileMode::Decal);
Paint paint = {};
paint.setMaskFilter(MaskFilter::MakeShader(maskShader, false));
paint.setBlendMode(BlendMode::Src);
canvas->drawImage(processedBg, processedOffset.x, processedOffset.y, &paint);
```

效果：只在 glass 形状内部显示折射后的背景，形状外部透明。

---

## 6. RGBA8 32位打包细节

AlphaMask 使用 RGBA8 四通道编码 float [0,1]：

**打包 (GlassMaskEffect shader)**:
```glsl
float value = texture(uSource, uv).a;
vec4 enc = vec4(1.0, 255.0, 65025.0, 16581375.0) * value;
enc = fract(enc);
enc -= enc.yzww * vec4(1.0/255.0, 1.0/255.0, 1.0/255.0, 0.0);
fragColor = enc;
```

**解包 (refraction shader)**:
```glsl
const vec4 UNPACK_FACTORS = vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0);
float height = dot(packed_rgba, UNPACK_FACTORS);
```

精度：~1/16581375 ≈ 6×10⁻⁸，远优于 8位的 1/255 ≈ 3.9×10⁻³。
