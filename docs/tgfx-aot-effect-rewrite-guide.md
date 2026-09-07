# TGFX AOT 效果改写手册：把复杂效果变成可预编译的形状

本文不列限制清单，而是给出一套**可重复使用的改写方法**。用户拿到任意复杂效果后，按四步判断、三种模式、示例 Shader 和决策树，就能把它改写成 TGFX AOT 可以预编译的形状。

---

## 1. 四步判断法：任意效果先过这四关

对任意效果，按顺序回答四个问题，每一步都能直接决定下一步怎么做。

### 第一步：输出像素只依赖同一坐标吗？

- **是**：继续第二步。
- **否**：先拆成离屏阶段（模式 A：离线烘焙）。

### 第二步：资源接口是否固定？

检查：

- 纹理数量 ≤ 4；
- 混合方式在 Porter-Duff 集合内；
- 不需要读取目标颜色；
- 没有 RuntimeEffect。

- **全部满足**：继续第三步。
- **任意不满足**：先分层合成或改用受支持混合（模式 B：分层合成）。

### 第三步：是否在容量内？

- 线性颜色链：最多 2 个颜色操作；
- 分支颜色关系：最多 16 条记录、最多 4 个真实纹理源。

- **满足**：可直接 AOT。
- **超出**：自动拆成多个 Pass，或先合并参数（模式 C：参数合并）。

### 第四步：中间格式是否可接受？

- 需要拆 Pass 时，中间纹理是 RGBA8。
- 如果精度足够：接受自动拆分。
- 如果精度不足：合并到同一 Pass，或接受运行时编译。

---

## 2. 三种通用改写模式

### 模式 A：离线烘焙

适用于：

- 模糊、阴影、邻居采样；
- 复杂遮罩；
- RuntimeEffect；
- 高精度链路。

**做法：**

```text
效果 A → 离屏渲染到纹理
效果 B → 对纹理应用受支持操作
```

**示意 Shader：**

```glsl
// 第一步：离线执行效果 A（不受 AOT 限制）
color = complexEffect(texture(coord));

// 第二步：对离屏纹理执行受支持操作（AOT 可预编译）
color = applyMatrix(texture(bakedTexture, coord));
```

### 模式 B：分层合成

适用于：

- 多纹理混合超过 4 张；
- 复杂嵌套 Blend；
- 需要读取目标颜色的混合。

**做法：**

```text
先把稳定层合成到一张纹理
再对结果应用受支持操作
```

**示意 Shader：**

```glsl
// 第一步：合成稳定层
vec4 stable = blend(textureA(coord), textureB(coord));

// 第二步：对合成结果执行受支持操作
color = blend(stable, textureC(coord));
```

### 模式 C：参数合并

适用于：

- 多个颜色矩阵；
- 多次色彩空间转换；
- 多组线性操作。

**做法：**

```text
合并矩阵 / 转换
减少链长
保持在单 Pass 容量内
```

**示意 Shader：**

```glsl
// 合并前：两个矩阵
color = applyMatrix(applyMatrix(texture(coord), m1), m2);

// 合并后：一个矩阵
color = applyMatrix(texture(coord), m2 * m1);
```

---

## 3. 每个场景的改写示例

### 3.1 单张图片直接绘制

**原效果：**

```cpp
canvas->drawImage(image, &paint);
```

**判断：**

- 输出像素 = texture(coord)，只依赖同一坐标；
- 资源接口固定。

**结论：** 直接 AOT。

**示意 Shader：**

```glsl
vec4 color = texture(TextureSampler_0, coord);
```

---

### 3.2 图片 + 颜色矩阵

**原效果：**

```cpp
paint.setColorFilter(ColorFilter::Matrix(matrix));
```

**判断：**

- 输出像素 = texture(coord) × matrix，只依赖同一坐标；
- matrix 是运行时参数。

**结论：** 直接 AOT。

**示意 Shader：**

```glsl
vec4 color = texture(TextureSampler_0, coord);
color = applyColorMatrix(color, matrix);
```

---

### 3.3 图片 + 颜色矩阵 + 亮度

**原效果：**

```cpp
paint.setColorFilter(ColorFilter::Compose(
    ColorFilter::Luma(),
    ColorFilter::Matrix(matrix)));
```

**判断：**

- 两个逐像素操作，容量上限 2；
- 单 Pass 完成。

**结论：** 直接 AOT。

**示意 Shader：**

```glsl
vec4 color = texture(TextureSampler_0, coord);
color = applyColorMatrix(color, matrix);
color = applyLuma(color);
```

---

### 3.4 图片 + 颜色矩阵 + 亮度 + 透明度阈值

**原效果：**

```cpp
paint.setColorFilter(ColorFilter::Compose(
    ColorFilter::Compose(
        ColorFilter::Luma(),
        ColorFilter::Matrix(matrix)),
    ColorFilter::AlphaThreshold(0.25f)));
```

**判断：**

- 三个逐像素操作，容量超过 2；
- 自动拆成两个 Pass；
- 中间纹理 RGBA8 会量化，但语义保持。

**结论：** 自动多 Pass，AOT 可执行。

**示意 Shader：**

```glsl
// Pass 0
vec4 color = texture(TextureSampler_0, coord);
color = applyColorMatrix(color, matrix);
color = applyLuma(color);
writeToIntermediate(color);

// Pass 1
vec4 intermediate = texture(IntermediateTexture, coord);
color = applyAlphaThreshold(intermediate, threshold);
```

---

### 3.5 两张图片混合

**原效果：**

```cpp
paint.setShader(Shader::MakeBlend(
    BlendMode::Multiply,
    Shader::MakeImageShader(imageA),
    Shader::MakeImageShader(imageB)));
```

**判断：**

- 输出像素 = blend(textureA(coord), textureB(coord))，只依赖同一坐标；
- 资源上限 4 个 sampler。

**结论：** 直接 AOT。

**示意 Shader：**

```glsl
vec4 colorA = texture(TextureA, coord);
vec4 colorB = texture(TextureB, coord);
vec4 color = blend(colorA, colorB, Multiply);
```

---

### 3.6 五张图片混合

**原效果：**

```cpp
// 超过 4 个真实纹理源
```

**判断：**

- 输出像素仍只依赖同一坐标，但资源接口超出 4 个 sampler；
- 无法枚举。

**改写：** 用模式 B：分层合成。

**示意 Shader：**

```glsl
// 第一步：离线合成稳定层
vec4 stable = blend(
    blend(textureA(coord), textureB(coord)),
    textureC(coord));

// 第二步：对合成结果执行受支持操作
color = blend(stable, textureD(coord));
```

---

### 3.7 高斯模糊

**原效果：**

```cpp
paint.setImageFilter(ImageFilter::Blur(8, 8));
```

**判断：**

- 输出像素依赖周围像素，坐标关系不可枚举；
- 但一维模糊被严格限定，因此有专用路径。

**结论：** 专用路径可 AOT；普通链内不能。

**改写：** 用模式 A：离线烘焙。

**示意 Shader：**

```glsl
// 第一步：离线模糊（专用路径）
color = blur(texture(coord));

// 第二步：对离屏纹理执行受支持操作
color = applyMatrix(texture(blurred, coord));
```

---

### 3.8 模糊后调色

**原效果：**

```cpp
paint.setImageFilter(ImageFilter::Compose(
    ImageFilter::Blur(8, 8),
    ImageFilter::ColorFilter(ColorFilter::Matrix(matrix))));
```

**判断：**

- 模糊是邻居效果，坐标关系不可枚举；
- 模糊结果必须先落中间纹理，无法只用寄存器传递。

**改写：** 用模式 A：离线烘焙。

**示意 Shader：**

```glsl
// 第一步：离线模糊
vec4 blurred = blur(texture(coord));

// 第二步：对离屏纹理调色（逐像素）
color = applyMatrix(texture(blurred, coord));
```

---

### 3.9 需要读取目标颜色的混合

**原效果：**

```cpp
paint.setBlendMode(BlendMode::ColorDodge);
```

**判断：**

- 需要读取目标颜色 dst，运行时才知道；
- 构建期无法枚举。

**改写：** 改用 Porter-Duff 或先离屏。

**示意 Shader：**

```glsl
// 改用 Porter-Duff
color = srcOver(src, dst);

// 或先离屏，再合成
vec4 intermediate = texture(IntermediateTexture, coord);
color = srcOver(intermediate, dst);
```

---

### 3.10 RuntimeEffect

**原效果：**

```cpp
auto effect = RuntimeEffect::MakeForShader(customGLSL);
```

**判断：**

- 构建期无法预知任意源码的坐标依赖、资源接口和是否读目标颜色。

**改写：** 改写为枚举内操作，或离线烘焙。

**示意 Shader：**

```glsl
// 改写为枚举内操作
vec4 color = texture(TextureSampler_0, coord);
color = applyColorMatrix(color, matrix);
```

---

### 3.11 递归滤镜序列

**原效果：**

```cpp
paint.setImageFilter(ImageFilter::Compose(
    ImageFilter::Blur(4, 4),
    ImageFilter::Compose(
        ImageFilter::ColorFilter(matrix),
        ImageFilter::Blur(4, 4))));
```

**判断：**

- 需要多次中间纹理，当前不自动规划多 Pass；
- 模糊本身坐标关系不可枚举。

**改写：** 在应用层显式拆分。

**示意 Shader：**

```glsl
// 第一步：模糊到离屏纹理
vec4 blurred1 = blur(texture(coord));

// 第二步：对离屏纹理调色
vec4 tinted = applyMatrix(texture(blurred1, coord));

// 第三步：对结果再模糊
vec4 blurred2 = blur(tinted);
```

---

### 3.12 高精度调色链

**原效果：**

```cpp
paint.setColorFilter(ColorFilter::Compose(
    ColorFilter::Matrix(highPrecisionMatrix1),
    ColorFilter::Matrix(highPrecisionMatrix2)));
```

**判断：**

- 拆成两个 Pass 后，中间纹理量化到 8 位，产生色带；
- 坐标映射的精度要求超出当前契约。

**改写：** 合并到同一 Pass，或接受运行时编译。

**示意 Shader：**

```glsl
// 合并矩阵
vec4 color = texture(TextureSampler_0, coord);
color = applyColorMatrix(color, m2 * m1);
```

---

## 4. 最终决策树

```text
你的效果是什么类型？
├─ 逐像素颜色运算
│  ├─ 链长 ≤ 2 → 直接 AOT
│  └─ 链长 > 2 → 自动拆 Pass
├─ 分支颜色关系
│  ├─ ≤ 16 记录 / ≤ 4 纹理 → 单 Pass
│  └─ 超出 → 模式 B：分层合成
├─ 邻居效果
│  └─ 模式 A：离线烘焙
├─ 自定义 GLSL
│  └─ 改写为枚举内操作或模式 A：离线烘焙
└─ 需要读取目标颜色
   └─ 改用 Porter-Duff 或先离屏
```

---

## 5. 总结

不要背“哪些不能”，而是按四步判断：

1. **输出像素只依赖同一坐标吗？**
2. **资源接口固定吗？**
3. **在容量内吗？**
4. **中间格式可接受吗？**

然后按三种模式改写：

- **离线烘焙**：邻居效果、复杂遮罩、RuntimeEffect、高精度链路；
- **分层合成**：多纹理混合、复杂嵌套、需要读取目标颜色；
- **参数合并**：多个矩阵、多次转换、多组线性操作。

最终，任何复杂效果都可以被拆成**逐像素、资源接口固定、不读目标颜色**的小段，从而获得 AOT 覆盖。
