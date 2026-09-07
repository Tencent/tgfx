# main 分支 TGFX 原始处理管线详解

本文基于 main 分支源码核实，详细说明没有 AOT 分支时，TGFX 对所有复合操作的处理方式。核心结论：**main 分支没有统一的自动拆 Pass 机制，所有复合操作都走同一条路径——能拼接就拼接成一个 Shader，不能拼接就运行时生成并编译，或者渲染到离屏纹理后手动拆分。**

---

## 1. 整体流程图

```
用户绘制调用
    ↓
OpsCompositor 组装 FragmentProcessor 链
    ↓
ProgramBuilder::emitAndInstallProcessors()
    ├─ emitAndInstallGeoProc()     → 顶点 Shader
    ├─ emitAndInstallFragProcessors() → 片元 Shader（拼接所有 FP）
    └─ emitAndInstallXferProc()      → 混合与输出
    ↓
后端编译（OpenGL/Vulkan/Metal/WebGPU）
    ↓
创建 Pipeline
    ↓
执行 Draw
```

**关键事实：** 所有 FragmentProcessor 的代码都被拼接到**同一个 FS 函数**里，不会为每个效果生成独立 Shader。

---

## 2. 逐阶段说明

### 2.1 几何阶段：生成顶点 Shader

```cpp
void ProgramBuilder::emitAndInstallGeoProc(std::string* outputColor, std::string* outputCoverage) {
  auto geometryProcessor = programInfo->getGeometryProcessor();
  ProcessorGuard processorGuard(this, geometryProcessor);
  nameExpression(outputColor, "outputColor");
  nameExpression(outputCoverage, "outputCoverage");

  auto processorIndex = programInfo->getProcessorIndex(geometryProcessor);
  fragmentShaderBuilder()->codeAppendf("{ // Processor%d : %s\n", processorIndex,
                                       geometryProcessor->name().c_str());
  vertexShaderBuilder()->codeAppendf("// Processor%d : %s\n", processorIndex,
                                     geometryProcessor->name().c_str());

  GeometryProcessor::FPCoordTransformHandler transformHandler(programInfo, &transformedCoordVars);
  GeometryProcessor::EmitArgs args(vertexShaderBuilder(), fragmentShaderBuilder(), varyingHandler(),
                                   uniformHandler(), getContext()->shaderCaps(), *outputColor,
                                   *outputCoverage, &transformHandler, &subsetVarName);
  geometryProcessor->emitCode(args);
  fragmentShaderBuilder()->codeAppend("}");
}
```

**作用：** 生成顶点位置、纹理坐标、初始颜色和覆盖。

### 2.2 颜色/覆盖阶段：拼接所有 FragmentProcessor

```cpp
void ProgramBuilder::emitAndInstallFragProcessors(std::string* color, std::string* coverage) {
  size_t transformedCoordVarsIdx = 0;
  std::string** inOut = &color;
  for (size_t i = 0; i < programInfo->numFragmentProcessors(); ++i) {
    if (i == programInfo->numColorFragmentProcessors()) {
      inOut = &coverage;
    }
    const auto fp = programInfo->getFragmentProcessor(i);
    auto output = emitAndInstallFragProc(fp, transformedCoordVarsIdx, **inOut);
    **inOut = output;
  }
}
```

每个 FragmentProcessor 都会：

1. 声明自己的纹理 Sampler；
2. 生成一段 GLSL 代码，把上一阶段的输出作为输入，输出给下一阶段；
3. 所有代码被包在同一个函数里，形成**一份完整的片元 Shader**。

### 2.3 单个 FragmentProcessor 的代码生成

```cpp
std::string ProgramBuilder::emitAndInstallFragProc(const FragmentProcessor* processor,
                                                   size_t transformedCoordVarsIdx,
                                                   const std::string& input) {
  ProcessorGuard processorGuard(this, processor);
  std::string output;
  nameExpression(&output, "output");

  fragmentShaderBuilder()->codeAppendf("{ // Processor%d : %s\n",
                                       programInfo->getProcessorIndex(processor),
                                       processor->name().c_str());

  // 声明所有纹理 Sampler
  std::vector<SamplerHandle> texSamplers;
  FragmentProcessor::Iter fpIter(processor);
  int samplerIndex = 0;
  while (const auto subFP = fpIter.next()) {
    for (size_t i = 0; i < subFP->numTextureSamplers(); ++i) {
      std::string name = "TextureSampler_";
      name += std::to_string(samplerIndex++);
      texSamplers.emplace_back(emitSampler(texture, name));
    }
  }

  // 生成处理代码
  FragmentProcessor::TransformedCoordVars coords(processor, ...);
  FragmentProcessor::TextureSamplers textureSamplers(processor, ...);
  FragmentProcessor::EmitArgs args(fragmentShaderBuilder(), uniformHandler(), output, input, ...);
  processor->emitCode(args);

  fragmentShaderBuilder()->codeAppend("}");
  return output;
}
```

**关键事实：** 所有 FragmentProcessor 的代码都被拼接到**同一个 FS 函数**里，不会为每个效果生成独立 Shader。

### 2.4 传输阶段：处理混合和输出

```cpp
void ProgramBuilder::emitAndInstallXferProc(const std::string& colorIn,
                                            const std::string& coverageIn) {
  auto xferProcessor = programInfo->getXferProcessor();
  ProcessorGuard processorGuard(this, xferProcessor);
  fragmentShaderBuilder()->codeAppendf("{ // Processor%d : %s\n",
                                       programInfo->getProcessorIndex(xferProcessor),
                                       xferProcessor->name().c_str());

  SamplerHandle dstTextureSamplerHandle;
  if (auto dstTextureView = xferProcessor->dstTextureView()) {
    dstTextureSamplerHandle = emitSampler(dstTextureView->getTexture(), "DstTextureSampler");
  }

  XferProcessor::EmitArgs args(fragmentShaderBuilder(), uniformHandler(), colorIn, coverageIn,
                               fragmentShaderBuilder()->colorOutputName(), dstTextureSamplerHandle);
  xferProcessor->emitCode(args);
  fragmentShaderBuilder()->codeAppend("}");
}
```

**作用：** 处理最终混合和输出，普通 SrcOver 由固定功能混合完成。

---

## 3. main 分支如何判定能否拼接成一个 Shader

main 分支没有显式的 `shouldDecompose` 或 `canStitch` 判断，而是**默认把所有 FragmentProcessor 拼接成一个 FS**，除非它们无法被拼接。判定逻辑分散在 `ProgramBuilder::emitAndInstallFragProcessors` 和 `FragmentProcessor::computeProcessorKey` 中。

#### 判定代码 1：依次拼接所有 FragmentProcessor

```cpp
void ProgramBuilder::emitAndInstallFragProcessors(std::string* color, std::string* coverage) {
  size_t transformedCoordVarsIdx = 0;
  std::string** inOut = &color;
  for (size_t i = 0; i < programInfo->numFragmentProcessors(); ++i) {
    if (i == programInfo->numColorFragmentProcessors()) {
      inOut = &coverage;
    }
    const auto fp = programInfo->getFragmentProcessor(i);
    auto output = emitAndInstallFragProc(fp, transformedCoordVarsIdx, **inOut);
    FragmentProcessor::Iter iter(fp);
    while (const FragmentProcessor* tempFP = iter.next()) {
      transformedCoordVarsIdx += tempFP->numCoordTransforms();
    }
    **inOut = output;
  }
}
```

**解析：** main 分支不会检查每个 FragmentProcessor 是否能被拼接，而是**依次调用 `emitAndInstallFragProc`，把它们的代码拼接到同一个 FS 函数里**。只有 `numColorFragmentProcessors()` 之前的 FP 拼到颜色链，之后的拼到覆盖链。

#### 判定代码 2：每个 FragmentProcessor 声明自己的纹理

```cpp
std::string ProgramBuilder::emitAndInstallFragProc(const FragmentProcessor* processor,
                                                   size_t transformedCoordVarsIdx,
                                                   const std::string& input) {
  ProcessorGuard processorGuard(this, processor);
  std::string output;
  nameExpression(&output, "output");

  fragmentShaderBuilder()->codeAppendf("{ // Processor%d : %s\n",
                                       programInfo->getProcessorIndex(processor),
                                       processor->name().c_str());

  std::vector<SamplerHandle> texSamplers;
  FragmentProcessor::Iter fpIter(processor);
  int samplerIndex = 0;
  while (const auto subFP = fpIter.next()) {
    for (size_t i = 0; i < subFP->numTextureSamplers(); ++i) {
      std::string name = "TextureSampler_";
      name += std::to_string(samplerIndex++);
      auto texture = subFP->textureAt(i);
      texSamplers.emplace_back(emitSampler(texture, name));
    }
  }

  FragmentProcessor::TransformedCoordVars coords(processor, ...);
  FragmentProcessor::TextureSamplers textureSamplers(processor, ...);
  FragmentProcessor::EmitArgs args(fragmentShaderBuilder(), uniformHandler(), output, input, ...);
  processor->emitCode(args);
  fragmentShaderBuilder()->codeAppend("}");
  return output;
}
```

**解析：** 每个 FragmentProcessor 都会通过 `numTextureSamplers()` 和 `textureAt()` 声明自己的纹理，这些纹理被依次命名为 `TextureSampler_0`、`TextureSampler_1` 等。**main 分支不会检查纹理数量是否超过限制，也不会检查纹理类型是否兼容，它默认所有纹理都能被声明。**

#### 判定代码 3：FragmentProcessor 自己决定纹理数量

```cpp
size_t TextureEffect::onCountTextureSamplers() const {
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return 0;
  }
  if (textureView->isYUV()) {
    return reinterpret_cast<YUVTextureView*>(textureView)->textureCount();
  }
  return 1;
}
```

**解析：** `TextureEffect` 根据纹理类型决定纹理数量：普通纹理返回 1，YUV 纹理返回 2 或 3。**main 分支不会限制这个数量，只要 `numTextureSamplers()` 返回多少，就会声明多少个 Sampler。**

#### 判定代码 4：FragmentProcessor 自己决定纹理对象

```cpp
std::shared_ptr<Texture> TextureEffect::onTextureAt(size_t index) const {
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return nullptr;
  }
  if (textureView->isYUV()) {
    return reinterpret_cast<YUVTextureView*>(textureView)->getTextureAt(index);
  }
  return textureView->getTexture();
}
```

**解析：** `TextureEffect` 根据纹理类型返回对应的纹理对象。**main 分支不会检查纹理对象是否为 YUV、透视、RGBAAA 等特殊类型，只要 `textureAt()` 返回非空，就会声明对应的 Sampler。**

#### 判定代码 5：FragmentProcessor 自己决定代码内容

```cpp
void FragmentProcessor::computeProcessorKey(Context* context, BytesKey* bytesKey) const {
  bytesKey->write(classID());
  onComputeProcessorKey(bytesKey);
  auto textureSamplerCount = onCountTextureSamplers();
  for (size_t i = 0; i < textureSamplerCount; ++i) {
    auto texture = textureAt(i);
    if (texture != nullptr) {
      TextureView::ComputeTextureKey(texture, bytesKey);
    }
  }
  for (const auto& childProcessor : childProcessors) {
    childProcessor->computeProcessorKey(context, bytesKey);
  }
}
```

**解析：** `computeProcessorKey` 把 FragmentProcessor 的类型、参数、纹理和子处理器都写入缓存键。**main 分支不会检查这些参数是否安全或可预编译，只要 `computeProcessorKey` 能运行，就会生成对应的 Shader。**

### 3.3 五个必须同时满足的条件（对应代码解析）

main 分支没有显式的"能否拼接"判断，而是**默认把所有 FragmentProcessor 拼接成一个 FS**，除非它们无法被拼接。下面五个条件对应 main 分支代码中哪些部分会失败。

#### 1. 逐像素：每个输出像素只依赖同一坐标的输入

**满足的代码：**

```glsl
vec4 color = texture(TextureSampler_0, coord);
```

**不满足的代码：**

```glsl
// 模糊：依赖邻居像素
vec4 color = 0;
for (int i = -radius; i <= radius; ++i) {
  color += weight[i] * texture(TextureSampler_0, coord + vec2(i, 0));
}
```

**解析：** 逐像素意味着 `texture()` 的坐标就是当前像素坐标，没有偏移。模糊、阴影、卷积都需要 `coord + offset`，因此不能拼接。

#### 2. 固定资源接口：纹理数量、绑定方式在编译前已知

**满足的代码：**

```glsl
layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;
layout(set = 1, binding = 1) uniform sampler2D MaskTextureSampler;
```

**不满足的代码：**

```glsl
// 动态改变 sampler 数量或类型
if (useMask) {
  uniform sampler2D MaskTextureSampler;
}
```

**解析：** 资源接口必须在编译前确定，不能通过运行时条件改变 sampler 声明。`HAS_DEVICE_MASK` 是编译期开关，不是运行时条件。

#### 3. 不读取目标颜色：混合方式在固定功能或双源范围内

**满足的代码：**

```glsl
// 固定功能混合：不需要读取 dst
fragColor = color * coverage;

// 双源混合：通过两个源计算，不读取 dst
vec4 src = texture(TextureSampler_0, coord);
vec4 dst = texture(TextureSampler_1, coord);
fragColor = blend(src, dst);
```

**不满足的代码：**

```glsl
// 需要读取目标颜色：无法提前写成固定代码
fragColor = colorDodge(src, dst);
```

**解析：** 固定功能混合和双源混合不需要读取帧缓冲，可以提前写成固定代码。ColorDodge 等需要读取 `dst`，运行时才知道，无法提前枚举。

#### 4. 不依赖邻居采样：没有模糊、阴影、卷积等

**满足的代码：**

```glsl
vec4 color = texture(TextureSampler_0, coord);
color = applyColorMatrix(color, matrix);
color = applyAlphaThreshold(color, threshold);
```

**不满足的代码：**

```glsl
// 模糊：需要循环采样邻居像素
vec4 color = 0;
for (int i = -radius; i <= radius; ++i) {
  color += weight[i] * texture(TextureSampler_0, coord + vec2(i, 0));
}

// 阴影：需要读取周围像素计算遮挡
float shadow = 0;
for (int x = -blurRadius; x <= blurRadius; ++x) {
  for (int y = -blurRadius; y <= blurRadius; ++y) {
    shadow += texture(TextureSampler_0, coord + vec2(x, y)).a;
  }
}
```

**解析：** 邻居采样需要读取 `coord + offset`，无法用单条 `texture(coord)` 表示，必须拆成多个 Pass 或专用路径。

#### 5. 不是 RuntimeEffect：没有用户自定义的任意 GLSL

**满足的代码：**

```glsl
// 预定义类型：TextureFillShader、ColorMatrixFragmentProcessor、AlphaThresholdFragmentProcessor
vec4 color = texture(TextureSampler_0, coord);
color = applyColorMatrix(color, matrix);
color = applyAlphaThreshold(color, threshold);
```

**不满足的代码：**

```glsl
// RuntimeEffect：用户传入任意 GLSL
vec4 myCustomEffect(vec2 coord, sampler2D tex, float param) {
  // 任意逻辑，构建期无法预知
}
```

**解析：** 所有 FragmentProcessor 都是预定义类型，源码在构建期已知。RuntimeEffect 允许用户传入任意 GLSL，构建期无法预知其形态、资源接口和坐标依赖关系。

### 例子 1：纹理 + 颜色矩阵

```cpp
Paint paint;
paint.setShader(Shader::MakeImageShader(image));
paint.setColorFilter(ColorFilter::Matrix(matrix));
canvas->drawRect(rect, paint);
```

生成的 FS：

```glsl
vec4 color = texture(TextureSampler_0, coord);
color = applyColorMatrix(color, matrix);
return color;
```

**结果：** 单 Pass 执行，无中间纹理。

### 3.3 拼接后的 Shader 结构

```
用户绘制调用
    ↓
OpsCompositor 组装 FragmentProcessor 链
    ↓
ProgramBuilder::emitAndInstallProcessors()
    ├─ 几何阶段 → 顶点 Shader
    ├─ 颜色/覆盖阶段 → 片元 Shader（拼接所有 FP）
    └─ 传输阶段 → 混合与输出
    ↓
后端编译
    ↓
Pipeline 创建
    ↓
执行 Draw
```

**关键事实：** 所有 FragmentProcessor 的代码都被拼接到**同一个 FS 函数**里，不会为每个效果生成独立 Shader。

### 3.4 每种情况的例子

#### 情况 1：仅纹理采样

```cpp
Paint paint;
paint.setShader(Shader::MakeImageShader(image));
canvas->drawRect(rect, paint);
```

生成的 FS：

```glsl
vec4 color = texture(TextureSampler_0, coord);
return color;
```

#### 情况 2：纹理 + 颜色矩阵

```cpp
Paint paint;
paint.setShader(Shader::MakeImageShader(image));
paint.setColorFilter(ColorFilter::Matrix(matrix));
canvas->drawRect(rect, paint);
```

生成的 FS：

```glsl
vec4 color = texture(TextureSampler_0, coord);
color = applyColorMatrix(color, matrix);
return color;
```

#### 情况 3：纹理 + 颜色矩阵 + 透明度阈值

```cpp
Paint paint;
paint.setShader(Shader::MakeImageShader(image));
paint.setColorFilter(ColorFilter::Compose(
    ColorFilter::Luma(),
    ColorFilter::Matrix(matrix)));
canvas->drawRect(rect, paint);
```

生成的 FS：

```glsl
vec4 color = texture(TextureSampler_0, coord);
color = applyColorMatrix(color, matrix);
color = applyAlphaThreshold(color, threshold);
return color;
```

#### 情况 4：纹理 + 颜色矩阵 + 亮度 + 透明度阈值

```cpp
Paint paint;
paint.setShader(Shader::MakeImageShader(image));
paint.setColorFilter(ColorFilter::Compose(
    ColorFilter::Compose(
        ColorFilter::Luma(),
        ColorFilter::Matrix(matrix)),
    ColorFilter::AlphaThreshold(0.25f)));
canvas->drawRect(rect, paint);
```

生成的 FS：

```glsl
vec4 color = texture(TextureSampler_0, coord);
color = applyColorMatrix(color, matrix);
color = applyAlphaThreshold(color, threshold);
return color;
```

#### 情况 5：纹理 + 设备遮罩

```cpp
Paint paint;
paint.setShader(Shader::MakeImageShader(image));
paint.setMaskFilter(MaskFilter::MakeShader(deviceMaskShader));
canvas->drawRect(rect, paint);
```

生成的 FS：

```glsl
vec4 color = texture(TextureSampler_0, coord);
color = applyDeviceMask(color, deviceMaskCoord);
return color;
```

#### 情况 6：两纹理混合

```cpp
Paint paint;
paint.setShader(Shader::MakeBlend(
    BlendMode::Multiply,
    Shader::MakeImageShader(imageA),
    Shader::MakeImageShader(imageB)));
canvas->drawRect(rect, paint);
```

生成的 FS：

```glsl
vec4 colorA = texture(TextureA, coord);
vec4 colorB = texture(TextureB, coord);
vec4 color = blend(colorA, colorB, Multiply);
return color;
```

### 例子 2：纹理 + 颜色矩阵 + 透明度阈值

```cpp
Paint paint;
paint.setShader(Shader::MakeImageShader(image));
paint.setColorFilter(ColorFilter::Compose(
    ColorFilter::Luma(),
    ColorFilter::Matrix(matrix)));
canvas->drawRect(rect, paint);
```

生成的 FS：

```glsl
vec4 color = texture(TextureSampler_0, coord);
color = applyColorMatrix(color, matrix);
color = applyAlphaThreshold(color, threshold);
return color;
```

**结果：** 单 Pass 执行，无中间纹理。

---

## 4. 什么情况下不能拼接成一个 Shader

### 4.1 需要邻居采样

- 模糊、阴影、卷积、马赛克。

这些效果必须读取周围像素，无法用单条 `texture(coord)` 表示。

### 4.2 需要读取目标颜色

- ColorDodge、ColorBurn 等需要 `dst` 的混合模式。

这些效果在运行时才知道 `dst`，无法提前写成固定代码。

### 4.3 资源接口不固定

- 纹理数量超过固定上限；
- 需要动态改变 sampler 数量或类型。

### 4.4 自定义 RuntimeEffect

- 用户传入任意 GLSL，构建期无法预知形态。

---

## 5. main 分支如何处理不能拼接的情况

main 分支**没有统一的自动拆 Pass 机制**，而是按类型分别处理：

### 5.1 复杂裁剪：渲染到离屏纹理

对于复杂裁剪，main 分支会把裁剪结果渲染到一张离屏纹理，再把这张纹理作为遮罩使用。

代码位置：`OpsCompositor::makeClipTexture`

```cpp
std::shared_ptr<TextureProxy> OpsCompositor::makeClipTexture(
    const std::vector<const ClipElement*>& elements, const Rect& bounds) const {
  const auto width = FloatSaturateToInt(bounds.width());
  const auto height = FloatSaturateToInt(bounds.height());
  auto clipRenderTarget = RenderTargetProxy::Make(context, width, height, true, 1, false,
                                                  ImageOrigin::TopLeft, BackingFit::Approx);

  std::vector<PlacementPtr<DrawOp>> clipDrawOps;
  for (size_t i = 0; i < elements.size(); ++i) {
    const auto element = elements[i];
    auto shape = Shape::MakeFrom(element->getDevicePath());
    shape = Shape::ApplyMatrix(std::move(shape), rasterizeMatrix);
    auto shapeProxy = proxyProvider()->createGPUShapeProxy(shape, aaType, clipBounds, renderFlags);
    auto drawOp = ShapeDrawOp::Make(std::move(shapeProxy), {}, uvMatrix, aaType);
    if (i > 0) {
      drawOp->setBlendMode(BlendMode::DstOut); // 后续裁剪用 DstOut 擦除
    }
    clipDrawOps.emplace_back(std::move(drawOp));
  }

  context->drawingManager()->addOpsRenderTask(std::move(clipRenderTarget), std::move(clipDrawOps),
                                              PMColor::Transparent());
  return clipRenderTarget->asTextureProxy();
}
```

**结果：** 裁剪区域被渲染到一张纹理，后续绘制用这张纹理作为遮罩。

### 5.2 遮罩路径：用离屏纹理作为 FragmentProcessor

生成裁剪纹理后，main 分支会把它包装成 `DeviceSpaceTextureEffect`，作为后续绘制的遮罩：

```cpp
PlacementPtr<FragmentProcessor> OpsCompositor::makeMaskFP(
    std::shared_ptr<TextureProxy> maskTexture, const Rect& bounds,
    PlacementPtr<FragmentProcessor> inputFP) const {
  auto uvMatrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
  auto maskFP = DeviceSpaceTextureEffect::Make(allocator, std::move(maskTexture), uvMatrix);
  if (!inputFP) {
    return maskFP;
  }
  return FragmentProcessor::Compose(allocator, std::move(maskFP), std::move(inputFP));
}
```

**结果：** 遮罩作为一张纹理参与后续绘制，但仍然是在同一个 FS 中处理。

### 5.3 模糊、阴影、RuntimeEffect：运行时生成并编译

对于模糊、阴影、RuntimeEffect 等无法拼接的效果，main 分支会：

1. 在运行时生成对应的 GLSL 代码；
2. 调用后端编译器编译；
3. 创建 Pipeline；
4. 执行绘制。

没有预编译，没有自动拆 Pass，只有运行时编译。

---

## 6. 实际例子：模糊后调色

用户写：

```cpp
paint.setImageFilter(ImageFilter::Compose(
    ImageFilter::Blur(8, 8),
    ImageFilter::ColorFilter(ColorFilter::Matrix(matrix))));
```

main 分支的处理：

1. **模糊**：由于模糊需要邻居采样，无法拼接，运行时生成模糊 Shader 并编译；
2. **调色**：模糊结果作为输入纹理，再应用颜色矩阵，生成新的 FS 并编译；
3. **执行**：两个 Pass 分别执行，中间用离屏纹理传递。

**注意：** main 分支不会自动把"模糊 → 调色"拆成两个 Pass，而是**每个效果单独编译**，通过纹理传递结果。

---

## 7. main 分支的实际行为总结

| 复合操作 | main 分支处理方式 |
|---------|----------------|
| 逐像素颜色链（纹理 + 矩阵 + 阈值） | **拼接成一个 FS，单 Pass 执行** |
| 复杂裁剪 | **渲染到离屏纹理，用纹理作为遮罩** |
| 模糊、阴影、邻居采样 | **运行时生成并编译，可能多 Pass，但由效果自己决定** |
| 需要读取目标颜色的混合 | **运行时生成并编译，单 Pass 执行** |
| RuntimeEffect | **运行时生成并编译，单 Pass 执行** |

**关键结论：** main 分支没有统一的自动拆 Pass 机制，只有"能拼接就拼接，不能拼接就运行时编译"的二元路径。

---

## 8. AOT 分支改变了什么

AOT 分支在 main 分支的基础上，新增了：

- `EffectDecomposer`：把长链颜色效果拆成多个 Pass；
- `OpsCompositor` 中的 `shouldDecompose` 逻辑：决定哪些链需要拆；
- `PipelineCanonicalizer`：统一处理 FP 规范化；
- `PrecompiledShaderCache`：构建期预编译所有支持的组合。

**AOT 分支没有改变"不能拼接就运行时编译"的事实，只是为"能拼接的那部分"提供了预编译路径，并为"部分不能拼接的长链"提供了自动拆 Pass 的机制。**

---

## 9. 结论

main 分支原来对所有复合操作的处理方式是：

> **能拼接就拼接成一个 Shader，不能拼接就运行时生成并编译，或者渲染到离屏纹理后手动拆分。没有统一的自动拆 Pass 机制。**

AOT 分支才引入了自动拆 Pass 和预编译的机制，但这些不是 main 分支原有的行为。
