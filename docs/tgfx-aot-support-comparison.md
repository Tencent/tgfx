# TGFX AOT 支持对照：原来能画什么，现在能预编译什么，为什么

本文只回答三个问题：原来这套代码能画出什么效果、AOT 现在能提前编译其中哪些、为什么剩下的一部分暂时不能提前编译，以及怎么改写才能达到相同视觉效果。

阅读时只需要记住一条总原则：**凡是输出像素只依赖同一坐标输入、且不需要读目标像素的效果，AOT 才能预编译；否则当前仍要运行时编译。**

公开 API 允许下游**无限嵌套** Shader、ColorFilter、ImageFilter，例如 `Shader::MakeBlend` 可以继续嵌套另一个 Blend。但 AOT 只能覆盖其中**逐像素、资源接口固定、不读目标颜色**的那部分结构；其余结构会在运行时退回 JIT。

---

## 1. 单张图片直接绘制

| 维度 | 原来（运行时编译） | 现在（AOT） | 为什么 |
| --- | --- | --- | --- |
| 普通 RGBA 图片 | 可以 | 可以 | 只读当前像素，无额外条件 |
| 仅透明度图片 | 可以 | 可以 | 仍是逐像素读取 |
| YUV 视频帧 | 可以 | 可以，但走专用 YUV 路径 | 采样方式不同，不能混进普通颜色链；YUV 需要三个固定平面，坐标相同，因此有专用 Shader |
| 带透视变换的图片 | 可以 | 不能 | 透视会改变采样坐标映射，坐标关系不可枚举；当前无法提前确定每个像素对应哪个纹理坐标 |

**怎么改：** YUV 直接绘制，不要先转 RGBA；透视先光栅化到普通纹理，再进颜色链。

```cpp
// 可以：普通 RGBA 图片直接绘制
canvas->drawImage(rgbaImage, &paint);

// 可以：YUV 视频帧直接绘制（走专用路径）
canvas->drawImage(yuvVideoFrame, &paint);

// 不能：带透视的绘制进入颜色链
canvas->concat(Matrix::MakePerspective(...));
canvas->drawImage(image, &paint);
```

---

## 2. 图片后面加颜色滤镜

### 2.1 只加颜色矩阵（ColorMatrix）

| 维度 | 原来 | 现在 | 为什么 |
| --- | --- | --- | --- |
| 图片 + ColorMatrix | 可以 | 可以 | 输出像素 = texture(coord) × matrix，只依赖同一坐标，matrix 是运行时参数，不改变资源接口 |

**怎么改：** 不需要改，直接命中。

```cpp
// 可以：图片 + ColorMatrix
Paint paint;
paint.setShader(Shader::MakeImageShader(image));
paint.setColorFilter(ColorFilter::Matrix(matrix));
canvas->drawRect(rect, paint);
```

### 2.2 再加亮度（Luma）或透明度阈值（AlphaThreshold）

| 维度 | 原来 | 现在 | 为什么 |
| --- | --- | --- | --- |
| 图片 + Matrix + Luma | 可以 | 可以 | 两个逐像素操作，容量上限 2，单 Pass 完成 |
| 图片 + Matrix + Luma + AlphaThreshold | 可以 | 可以 | 三个逐像素操作，容量超过 2，自动拆成两个 Pass；中间纹理 RGBA8 会量化，但语义保持 |

**怎么改：** 能合并矩阵就先合并（矩阵乘法），减少 Pass；不能合并就接受自动多 Pass，注意中间纹理是 RGBA8，可能损失精度。

```cpp
// 可以：图片 + Matrix + Luma（链长 2，单 Pass）
Paint paint;
paint.setShader(Shader::MakeImageShader(image));
paint.setColorFilter(ColorFilter::Compose(
    ColorFilter::Luma(),
    ColorFilter::Matrix(matrix)));
canvas->drawRect(rect, paint);

// 可以：图片 + Matrix + Luma + AlphaThreshold（链长 3，自动拆两个 Pass）
Paint paint;
paint.setShader(Shader::MakeImageShader(image));
paint.setColorFilter(ColorFilter::Compose(
    ColorFilter::Compose(
        ColorFilter::Luma(),
        ColorFilter::Matrix(matrix)),
    ColorFilter::AlphaThreshold(0.25f)));
canvas->drawRect(rect, paint);
```

### 2.3 加色彩空间转换（ColorSpaceXform）

| 维度 | 原来 | 现在 | 为什么 |
| --- | --- | --- | --- |
| 图片 + ColorSpaceXform | 可以 | 可以 | 逐像素转换，链内最多一次，坐标关系可枚举 |
| 图片 + ColorSpaceXform + 再转换一次 | 可以 | 不能 | 链内最多一次，第二次会破坏坐标映射的可枚举性，当前无法证明安全 |

**怎么改：** 把两次转换合并成一次，或提前离线转换。

```cpp
// 可以：单次 ColorSpaceXform
paint.setColorFilter(ColorSpaceXform::MakeSRGBToLinear());

// 不能：链内连续两次 ColorSpaceXform
paint.setColorFilter(ColorFilter::Compose(
    ColorSpaceXform::MakeSRGBToLinear(),
    ColorSpaceXform::MakeLinearToSRGB()));
```

---

## 3. 图片后面加遮罩

| 维度 | 原来 | 现在 | 为什么 |
| --- | --- | --- | --- |
| 图片 + 设备空间遮罩 | 可以 | 可以 | 遮罩也是逐像素读取，坐标关系可枚举，属于标准开关 |
| 图片 + 多层嵌套遮罩 | 可以 | 不能 | 覆盖率无法折叠成单次乘法，坐标关系不可枚举，无法安全预编译 |

**怎么改：** 把多层遮罩预先合成一张遮罩纹理，再用 `HAS_DEVICE_MASK` 路径。

```cpp
// 可以：单层设备遮罩
Paint paint;
paint.setShader(Shader::MakeImageShader(image));
paint.setMaskFilter(MaskFilter::MakeShader(deviceMaskShader));

// 不能：多层嵌套遮罩
Paint paint;
paint.setShader(Shader::MakeImageShader(image));
paint.setMaskFilter(MaskFilter::Compose(
    MaskFilter::MakeShader(mask1),
    MaskFilter::Blur(4, 4)));
```

---

## 4. 两张图片做混合

| 维度 | 原来 | 现在 | 为什么 |
| --- | --- | --- | --- |
| 两张图片按 BlendMode 混合 | 可以 | 可以，但最多 4 张真实纹理 | 输出像素 = blend(textureA(coord), textureB(coord))，只依赖同一坐标；资源上限 4 个 sampler |
| 三张图片混合 | 可以 | 可以 | 不足 4 张时由库用占位纹理补齐，坐标关系仍可枚举 |
| 五张图片混合 | 可以 | 不能 | 超过 4 个真实纹理源，坐标关系超出固定 sampler 接口，无法枚举 |

**怎么改：** 把其中稳定的两层先离线合成一张，再参与混合。

```cpp
// 可以：两张图片混合
Paint paint;
paint.setShader(Shader::MakeBlend(
    BlendMode::Multiply,
    Shader::MakeImageShader(imageA),
    Shader::MakeImageShader(imageB)));
canvas->drawRect(rect, paint);

// 不能：五张图片混合
Paint paint;
paint.setShader(Shader::MakeBlend(
    BlendMode::Multiply,
    Shader::MakeImageShader(imageA),
    Shader::MakeBlend(
        BlendMode::Plus,
        Shader::MakeImageShader(imageB),
        Shader::MakeBlend(
            BlendMode::Overlay,
            Shader::MakeImageShader(imageC),
            Shader::MakeImageShader(imageD)))));
canvas->drawRect(rect, paint);
```

---

## 5. 渐变、文本、噪声

| 维度 | 原来 | 现在 | 为什么 |
| --- | --- | --- | --- |
| 线性/径向/圆锥/钻石渐变 | 可以 | 可以 | 输出像素由坐标在渐变几何中的位置决定，公式固定，坐标关系可枚举 |
| 图集文本 | 可以 | 可以 | 输出像素由字符在图集中的固定位置决定，坐标关系可枚举 |
| Perlin 噪声 | 可以 | 可以，噪声后最多再跟 3 个颜色操作 | 输出像素由噪声函数决定，坐标关系可枚举；专用路径容量 3 |
| Perlin 噪声 + 第 4 个颜色操作 | 可以 | 不能 | 超过专用路径容量，当前没有更大容量的专用 Shader |

**怎么改：** 噪声效果超过 3 个操作时，把前几个操作离线烘焙成纹理，再进链。

```cpp
// 可以：Perlin 噪声 + 最多 3 个颜色操作
Paint paint;
paint.setShader(Shader::MakeFractalNoise(fx, fy, octaves, seed));
paint.setColorFilter(ColorFilter::Compose(
    ColorFilter::Luma(),
    ColorFilter::Matrix(matrix)));

// 不能：Perlin 噪声 + 第 4 个颜色操作
Paint paint;
paint.setShader(Shader::MakeFractalNoise(fx, fy, octaves, seed));
paint.setColorFilter(ColorFilter::Compose(
    ColorFilter::Compose(
        ColorFilter::Luma(),
        ColorFilter::Matrix(matrix)),
    ColorFilter::AlphaThreshold(0.5f)));
```

---

## 6. 模糊、阴影、邻居采样

| 维度 | 原来 | 现在 | 为什么 |
| --- | --- | --- | --- |
| 高斯模糊 | 可以 | 参数受契约约束的专用路径可以，普通链内不行 | 输出像素依赖周围像素，坐标关系不可枚举；但一维模糊被严格限定，因此有专用 Shader |
| DropShadow | 可以 | 不能 | 阴影也是邻域效果，坐标关系不可枚举 |
| 模糊后再调色 | 可以 | 不能 | 模糊结果必须先落中间纹理，无法只用寄存器传递；拆 Pass 后精度损失不可控 |

**怎么改：** 模糊走专用路径；阴影拆成“阴影层 + 内容层”两次绘制；模糊后调色在应用层用两个 Surface 显式分开。

```cpp
// 不能：模糊后调色
Paint paint;
paint.setShader(Shader::MakeImageShader(image));
paint.setImageFilter(ImageFilter::Compose(
    ImageFilter::Blur(8, 8),
    ImageFilter::ColorFilter(ColorFilter::Matrix(matrix))));
canvas->drawImage(image, &paint);

// 可以：先模糊到离屏纹理，再对离屏纹理调色
auto blurred = renderToTexture(image, blurFilter);
Paint paint;
paint.setShader(Shader::MakeImageShader(blurred));
paint.setColorFilter(ColorFilter::Matrix(matrix));
canvas->drawImage(blurred, &paint);
```

---

## 7. 需要读取目标颜色的混合

| 维度 | 原来 | 现在 | 为什么 |
| --- | --- | --- | --- |
| ColorDodge、ColorBurn 等自定义混合 | 可以 | 不能 | 需要读取目标颜色 dst，运行时才知道，构建期无法枚举 |
| Porter-Duff 混合（SrcOver、SrcIn 等） | 可以 | 可以 | 固定功能混合或双源实现，不需要读目标，坐标关系可枚举 |

**怎么改：** 改用 Porter-Duff 集合内的模式；或先把前景画到离屏纹理，再按 Porter-Duff 合成。

```cpp
// 可以：Porter-Duff 混合
paint.setBlendMode(BlendMode::SrcOver);

// 不能：需要读取目标颜色的自定义混合
paint.setBlendMode(BlendMode::ColorDodge);
```

---

## 8. 自定义 RuntimeEffect

| 维度 | 原来 | 现在 | 为什么 |
| --- | --- | --- | --- |
| 用户自定义 GLSL | 可以 | 不能 | 构建期无法预知任意源码的坐标依赖、资源接口和是否读目标颜色 |

**怎么改：** 逐项检查自定义效果，能表示为矩阵、亮度、阈值、两源混合的，改写为枚举内操作；不能改写的，离线烘焙成纹理。

```cpp
// 不能：任意 RuntimeEffect
auto effect = RuntimeEffect::MakeForShader(customGLSL);
paint.setShader(effect->makeShader(...));

// 可以：改写为枚举内操作
paint.setShader(Shader::MakeImageShader(image));
paint.setColorFilter(ColorFilter::Matrix(matrix));
```

---

## 9. 递归或深层滤镜序列

| 维度 | 原来 | 现在 | 为什么 |
| --- | --- | --- | --- |
| 模糊 → 调色 → 再模糊 | 可以 | 不能 | 需要多次中间纹理，当前不自动规划多 Pass；且模糊本身坐标关系不可枚举 |
| 简单颜色链 | 可以 | 可以 | 逐像素，坐标关系可枚举，可自动拆 Pass |

**怎么改：** 在应用层用多个 Surface 显式拆分，每段分别落在受支持形状内。

```cpp
// 不能：自动拆分模糊 → 调色 → 模糊
Paint paint;
paint.setImageFilter(ImageFilter::Compose(
    ImageFilter::Blur(4, 4),
    ImageFilter::Compose(
        ImageFilter::ColorFilter(matrix),
        ImageFilter::Blur(4, 4))));

// 可以：应用层显式拆分
auto blurred1 = renderToTexture(image, blurFilter);
auto tinted = renderToTexture(blurred1, colorFilter);
auto blurred2 = renderToTexture(tinted, blurFilter);
canvas->drawImage(blurred2, &paint);
```

---

## 10. 高精度颜色链路

| 维度 | 原来 | 现在 | 为什么 |
| --- | --- | --- | --- |
| 需要 16 位浮点精度的调色链 | 可以 | 不能拆 Pass | 中间纹理是 RGBA8，8 位量化会引入色带，坐标映射的精度要求超出当前契约 |
| 普通调色链 | 可以 | 可以 | 精度足够，RGBA8 量化在可接受范围内 |

**怎么改：** 把精度敏感的操作合并到同一 Pass；或接受运行时编译，不用拆分换命中率。

```cpp
// 不能：高精度链路自动拆 Pass
Paint paint;
paint.setColorFilter(ColorFilter::Compose(
    ColorFilter::Matrix(highPrecisionMatrix1),
    ColorFilter::Matrix(highPrecisionMatrix2)));
// 拆成两个 Pass 后，中间纹理量化到 8 位，产生色带。

// 可以：合并到同一 Pass
Paint paint;
paint.setColorFilter(ColorFilter::Matrix(combinedMatrix));
```

---

## 总结

- **现在就能用的**：普通图片、颜色矩阵、亮度、阈值、少量混合、渐变、文本、Perlin 噪声、设备遮罩。
- **现在不能用的**：模糊/阴影/邻居采样、读目标混合、RuntimeEffect、复杂遮罩、递归滤镜、高精度链路。
- **怎么改**：把复杂效果拆成多个受支持的小段，每段分别预编译，用中间纹理连接；或者提前离线烘焙成纹理。

判断规则只有一条：**输出像素只依赖同一坐标输入，AOT 才能预编译；否则当前仍要运行时编译。**
