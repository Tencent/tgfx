# SVG 滤镜与渐变导入修复

## 1 背景

Pixso 导出的一个 SVG 图标文件（28×24，圆角矩形带投影、内阴影与线性渐变填充）在 TGFX 中渲染错误：group 上的投影和内阴影完全丢失，矩形填充的线性渐变渲染成纯首停色。同一文件在 Figma 中解析为一个内阴影加一个外阴影，在 Chrome 中渲染正确。该文件的滤镜链与渐变已按 §4.2 原样整合进测试资源 filter.svg / gradient.svg。

该文件的结构特征：

- `<filter>` 内含 14 个滤镜原语，前 8 个编码外阴影（feOffset + feGaussianBlur + feComposite out + 染色 + feBlend），后 6 个编码内阴影（feOffset + feGaussianBlur + feComposite arithmetic + 染色 + feBlend）
- 文件是格式化输出的，元素节点之间有换行缩进
- 矩形的填充渐变使用 `gradientUnits="userSpaceOnUse"`，端点为绝对用户空间坐标

修复目标：这类文件在 TGFX 中正确渲染，且 TGFX 能把滤镜链还原为高层 `ImageFilter`（DropShadow/InnerShadow）。

术语定义：

- **捷径路径**：`SVGFilter::buildDropShadowFilter` / `buildInnerShadowFilter`，把符合固定模式的滤镜原语链逆向识别为单个高层 `ImageFilter`
- **DAG 路径**：`buildFilterDAG` 的通用兜底，逐原语构建 `ImageFilter` 并串联
- **TextLiteral**：XML 元素之间的空白文本解析出的文本节点（`SVGTag::TextLiteral`）

## 2 现有实现分析

### 2.1 滤镜解析管线

`SVGDOM::Make`（src/svg/SVGDOM.cpp:47）把 XML 解析为 SVG 节点树；渲染时 `SVGRenderContext::applyFilter`（src/svg/SVGRenderContext.cpp:264）按 id 找到 filter 节点，调用 `SVGFilter::buildFilterDAG`（src/svg/node/SVGFilter.cpp:54）：

```cpp
std::shared_ptr<ImageFilter> SVGFilter::buildFilterDAG(const SVGRenderContext& context) const {
  // ...
  if (auto dropShadowFilter = buildDropShadowFilter(localContext, filterContext)) {
    return dropShadowFilter;   // 捷径 1：外阴影
  }
  if (auto innerShadowFilter = buildInnerShadowFilter(localContext, filterContext)) {
    return innerShadowFilter;  // 捷径 2：内阴影
  }
  for (const auto& child : children) {   // DAG 路径：逐原语构建
    if (!SVGFe::IsFilterEffect(child)) {
      continue;
    }
    filter = feNode.makeImageFilter(localChildCtx, filterContext);
    if (!filter) {
      return nullptr;          // 任一原语失败则整条链作废
    }
    // ...
  }
  return filter;
}
```

### 2.2 问题一：TextLiteral 间隔使捷径模式匹配失配

`SVGNodeConstructor`（src/svg/SVGNodeConstructor.cpp:443-449）为每个 XML 文本节点创建 `SVGTextLiteral`，包括元素之间的纯空白。格式化 SVG 的 `<filter>` 解析出 29 个 children，其中 15 个是 TextLiteral，与 14 个滤镜原语交替排列（已打印解析后的节点树逐行核对确认）。

`buildDropShadowFilter` / `buildInnerShadowFilter` 用 `children[i-1]`、`children[blurIndex+1]` 等相邻索引做模式匹配（SVGFilter.cpp:110-142、174-199），文本节点插在中间后模式永远失配。DAG 路径有 `SVGFe::IsFilterEffect` 过滤不受影响。TGFX 自己的 SVGExporter 输出的滤镜链无空白，round-trip 测试因此从未覆盖到这个问题。

### 2.3 问题二：feFlood 未实现导致 DAG 路径中断

`SVGFeFlood::onMakeImageFilter` 未实现，直接返回 nullptr（include/tgfx/svg/node/SVGFeFlood.h:40-44）。DAG 路径遇到任一 nullptr 立即整体返回 nullptr（SVGFilter.cpp:86-88）。Pixso 滤镜链的第一个原语就是 feFlood，因此捷径失配后兜底也失败，整个滤镜被丢弃。

### 2.4 问题三：捷径只支持单个效果

两条捷径命中即 return，返回值是单个 `ImageFilter`。Pixso 的链是「外阴影段 + 内阴影段」两段串联，即使模式匹配正常，命中外阴影后内阴影段也不会被处理。实测紧凑格式（无空白）的同一条链，`buildFilterDAG` 返回单个 `DropShadow(dx=1, dy=2, blur=0.667, 棕色 50%)`，内阴影丢失。

### 2.5 问题四：feBlend 的 in2 属性以前缀匹配失败被丢弃

`SVGFeInputType` 的解析先走枚举匹配（src/svg/node/SVGFe.cpp:122-144），`matchStringToken` 是纯前缀匹配（src/svg/SVGAttributeParser.cpp:77-94）。`in2="BackgroundImageFix"` 的前 15 个字符命中保留字 `BackgroundImage`，前缀消耗后 `parseEOSToken` 检查剩余 `"Fix"` 失败，整个属性被判定解析失败而丢弃。实测确认：打印解析后的节点树，该节点的 in2 属性为空；直接调用 `SVGAttributeParser::parse<SVGFeInputType>("BackgroundImageFix")` 返回解析失败。任何以保留字（SourceGraphic/SourceAlpha/BackgroundImage 等）开头的自定义 result 名都会触发。

### 2.6 问题五：SVGRect 归零绘制破坏 userSpaceOnUse 渐变

`SVGRect::onDrawFill`（src/svg/node/SVGRect.cpp:61-70）把矩形平移回原点后靠 `canvas->translate(offset)` 补偿绘制。几何上等价，但画笔上的 shader 锚定坐标随画布平移一起移动。

userSpaceOnUse 渐变的端点解析为绝对用户空间坐标（本文件为 (10,11)→(10,19)，探针实测正确）。归零绘制时矩形在绘制局部空间占 y∈[0,8]，渐变跨度在局部空间是 y∈[11,19]，两者不相交，所有片元投影参数 t<0 被 clamp 到首停，矩形渲染成纯首停色（实测 alpha 恒为 108）。

objectBoundingBox 渐变不受影响，机制：`SVGShape::onRender`（src/svg/node/SVGShape.cpp:34-35）把 paint 上下文的 lengthContext viewport 覆盖为形状尺寸，OBB 坐标解析为形状局部空间（0,0）→（宽,高），与归零绘制恰好对齐（实测 OBB 渐变渲染正常）。

`SVGLinearGradient::onMakeShader`（src/svg/node/SVGLinearGradient.cpp:40-54）忽略了 `localMatrix` 参数，而 `SVGRadialGradient::onMakeShader`（src/svg/node/SVGRadialGradient.cpp:58-59）会应用它，两者行为不一致。`SVGPath`/`SVGCircle`/`SVGEllipse` 原地绘制，只有 `SVGRect`（含 `onDrawStroke`，SVGRect.cpp:72-90）使用归零绘制。

## 3 修复方案

五个问题相互独立，分五节。问题一、二、四是正确性基础修复；问题三在捷径上支持多效果串联；问题五统一渐变的坐标空间处理。

### 3.1 捷径匹配跳过非滤镜原语节点

`buildFilterDAG` 入口处先把 children 过滤为只含 `SVGFe` 的序列，捷径函数和 DAG 循环都改用该序列：

```cpp
std::shared_ptr<ImageFilter> SVGFilter::buildFilterDAG(const SVGRenderContext& context) const {
  // ...
  std::vector<std::shared_ptr<SVGNode>> feNodes;
  for (const auto& child : children) {
    if (SVGFe::IsFilterEffect(child)) {
      feNodes.push_back(child);
    }
  }
  if (auto dropShadowFilter = buildDropShadowFilter(localContext, filterContext, feNodes)) {
    // ...
  }
  // DAG 循环同样遍历 feNodes
}
```

`buildDropShadowFilter` / `buildInnerShadowFilter` 的签名从读 `children` 改为接收过滤后的序列，匹配逻辑不变。

不在构造阶段过滤纯空白 TextLiteral：文本节点的空白处理与 `xml:space` 规则相关，改动构造影响面大；捷径匹配是唯一的相邻索引消费方，就地修复即可。

### 3.2 实现 feFlood

feFlood 语义：以 flood-color × flood-opacity 的纯色填充滤镜子区域，忽略输入。`flood-color`/`flood-opacity` 呈现属性已在 `SVGNode` 解析（src/svg/node/SVGNode.cpp:132-133），实现只需读取并生成忽略输入的纯色滤镜：

```cpp
std::shared_ptr<ImageFilter> SVGFeFlood::onMakeImageFilter(
    const SVGRenderContext& context, const SVGFilterContext&) const {
  // FloodColor/FloodOpacity 为呈现属性，从 presentationContext 读取
  Color color = ...;  // floodColor，alpha 乘 floodOpacity
  // BlendMode::Src 使输出等于 shader 颜色，忽略输入
  return ImageFilter::Blend(BlendMode::Src, Shader::MakeColorShader(color));
}
```

`ImageFilter::Blend(BlendMode, Shader)` 已存在（src/core/filters/BlendImageFilter.cpp:25）。输出区域与滤镜子区域的精确对齐不做特殊处理：Blend 滤镜的输出跟随输入图层的边界，与本文件场景（flood-opacity=0 的透明底）一致；精确子区域裁剪留作后续优化。

### 3.3 捷径支持多效果串联识别

把 `buildDropShadowFilter` / `buildInnerShadowFilter` 从「整链匹配单效果」改为「切段后逐段识别」：

```cpp
// buildFilterDAG 中，捷径部分改为：
auto effects = recognizeEffectChain(localContext, filterContext, feNodes);
if (!effects.empty()) {
  return ImageFilter::Compose(effects);  // 按段顺序串联，先执行的在前
}
```

切段规则：以 `in=SourceGraphic` 的 feBlend 为段尾标记——它把当前段的结果与源图形合成，是单个效果的收尾；段尾也可以是整条链的结尾（不含 SourceGraphic blend 的单效果链，对应 DropShadowOnly/InnerShadowOnly 变体）。每段独立尝试识别为外阴影或内阴影（复用现有匹配逻辑，输入从整链改为段切片）。全部段识别成功才返回 Compose 结果；任一段失败则整体落到 DAG 路径。

对本文件的链：段 1（feFlood 到 in=SourceGraphic 的 feBlend）识别为 `DropShadow(1, 2, 0.667, 棕色 50%)`，段 2（feColorMatrix 到末尾 feBlend）识别为 `InnerShadow(0, 0, 0.333, 白色)`，返回 `ImageFilter::Compose({dropShadow, innerShadow})`。

语义说明：段 1 中的 `feComposite operator="out"` 会把图形自身区域的阴影挖掉，识别为 DropShadow 后该挖除不再保留——图形半透明填充时阴影会透过填充隐约可见。这与 Figma 效果模型的投影行为一致，可接受。

### 3.4 feBlend 的 in 解析改为完整标识符匹配

`SVGAttributeParser::parse(SVGFeInputType*)` 改为先读取完整标识符，再与保留字精确比较：

```cpp
template <>
bool SVGAttributeParser::parse(SVGFeInputType* type) {
  SVGStringType token;
  if (!parse(&token)) {          // 读取完整标识符
    return false;
  }
  for (保留字表) {               // SourceGraphic/SourceAlpha/BackgroundImage/...
    if (token == 保留字) {       // 精确相等才按标准输入处理
      *type = SVGFeInputType(对应枚举);
      return true;
    }
  }
  *type = SVGFeInputType(token); // 其余按命名引用处理
  return true;
}
```

不改 `matchStringToken` 本身：前缀匹配加 EOS 检查是纯枚举场景的正确行为，问题只出在「枚举 + 自定义 id」混合的 SVGFeInputType 上。

### 3.5 统一渐变坐标空间，rect 改为原地绘制

统一所有形状为原地绘制，渐变的两种 units 都通过 localMatrix 承载空间映射：

1. `SVGRect::onDrawFill` / `onDrawStroke` 去掉归零与画布平移，原地 `drawRRect`/`drawPath`。
2. `SVGShape::onRender` 不再把 paint 上下文的 lengthContext viewport 覆盖为形状尺寸。该覆盖原为使 OBB 渐变对齐归零绘制而设，统一后不再需要；同时这也修正了 userSpaceOnUse 下百分比坐标应相对 SVG viewport 而非形状尺寸的解析口径。注意 `SVGPattern::onAsPaint`（src/svg/node/SVGPattern.cpp）同样依赖该覆盖解析 OBB tile，需一并迁移：tile 的 x/y/width/height 在 0-1 空间解析后经 `transformForCurrentBoundBox` 映射到用户空间；tile 内容以 tile 左上角为原点录制（内容长度按 boundingBoxUnits + obb 尺寸的内容上下文解析），首个 tile 通过 shader 的 localMatrix 平移锚定到 tile 原点，不再锚定全局原点（当前实现形状不在原点时 OBB pattern 位置有偏差）。
3. `SVGGradient` 的 OBB 坐标改在 0-1 单位空间解析，`SVGGradient::onAsPaint` 已算出的 localMatrix（`T(obb原点)·S(obb尺寸)·gradientTransform`）承担 OBB 到用户空间的映射：

```cpp
// SVGLinearGradient::onMakeShader
std::shared_ptr<Shader> SVGLinearGradient::onMakeShader(const SVGRenderContext& context,
                                                        ..., const Matrix& localMatrix) const {
  SVGLengthContext lengthContext =
      getGradientUnits().type() == SVGObjectBoundingBoxUnits::Type::ObjectBoundingBox
          ? SVGLengthContext(Size::Make(1, 1))   // OBB 坐标解析到 0-1 空间
          : context.lengthContext();             // userSpaceOnUse 解析到用户空间
  lengthContext.setBoundingBoxUnits(getGradientUnits());
  auto startPoint = ...;  // 同现有逻辑
  auto endPoint = ...;
  return Shader::MakeLinearGradient(startPoint, endPoint, colors, positions)
      ->makeWithMatrix(localMatrix);
}
```

`SVGRadialGradient` 同步对齐：OBB 坐标同样改为 0-1 空间解析，现有 `matrix.mapPoints` / 半径缩放保持不变（当前 OBB 径向渐变存在 resolve 乘 viewport 与 matrix 缩放叠加的重复缩放嫌疑，统一后消除）。

`Shader::makeWithMatrix` 的语义是把给定矩阵应用到 shader 自身矩阵之后（include/tgfx/core/Shader.h:190），线性渐变 shader 自身无矩阵，叠加后采样点先经 localMatrix 变换，行为符合预期。

## 4 测试计划

### 4.1 机制单测

- `ShadowFilterChainRecognition`：格式化（含空白文本节点）的串联链识别为 `Compose(DropShadow, InnerShadow)`，参数与链中数值一致
- `FilterChainFormattedVsCompact`：同一滤镜链的格式化版与紧凑版，识别结果一致
- `FeInputTypeReservedPrefix`：`BackgroundImageFix` 解析为命名引用，精确保留字与普通 id 不受影响
- `FeFloodChainBuildsFilter`：含 feFlood 的链 `buildFilterDAG` 不再返回 nullptr
- `LinearGradientUnits`：userSpaceOnUse 与 objectBoundingBox 两种口径下，rect 填充的渐变在矩形两端分别命中两端 stop 色

### 4.2 截图测试

不新建用例文件，把阴影滤镜链与线性渐变分别整合进现有用例：

- `SVGRenderTest.BlurSVG` 改名为 `SVGRenderTest.Filter`，资源 `blur.svg` 改名为 `filter.svg`。在现有矩形右侧加一个等大矩形：纯色填充，外包 `<g>` 挂本次修复的滤镜链（feFlood + 投影段 + 内阴影段，逐字节保留自 Pixso 导出文件；仅删除 filter 元素的 x/y/width/height 区域属性改用默认区域，并删除 `customEffect` 属性——它引用的私有 defs 节点在原文件中已被删除，属悬空引用）。滤镜挂在 `<g>` 上而非矩形上，与原文件一致地走 saveLayer + ImageFilter 路径
- `SVGRenderTest.GradientSVG` 改名为 `SVGRenderTest.Gradient`，资源 `radialGradient.svg` 改名为 `gradient.svg`。在现有径向渐变矩形右侧加一个等大矩形，用 `gradientUnits="userSpaceOnUse"` 的线性渐变填充
- 两个用例的 baseline key（`SVGTest/blur`、`SVGTest/radialGradient`）随改名变化，旧基准不再参与比较，新基准在修复完成后由人工执行 `/accept-baseline` 接受
- 既有基准 `SVGTest/complex2`、`SVGTest/complex6` 记录的是修复前的错误输出（complex6 的 userSpaceOnUse 渐变矩形旧输出为纯首停色），修复后渲染发生变化属预期，同样需要重新接受基准
- 回归其余现有 SVG 截图用例（complex1、complex3-5、complex7、text 等），确认渐变与滤镜改动不破坏既有行为

流程：先改 SVG 资源与用例并在修复前运行一次，确认输出复现当前的错误效果；修复后跑通，再接受基准。

## 5 实现步骤

| 步骤 | 文件 | 说明 |
|------|------|------|
| 1 | src/svg/node/SVGFilter.cpp | buildFilterDAG 过滤 feNodes，捷径函数改签名 |
| 2 | include/tgfx/svg/node/SVGFeFlood.h、src/svg/node/SVGFeFlood.cpp | 实现 feFlood |
| 3 | src/svg/node/SVGFilter.cpp | recognizeEffectChain 切段与逐段识别 |
| 4 | src/svg/node/SVGFe.cpp | SVGFeInputType 完整标识符匹配 |
| 5 | src/svg/node/SVGRect.cpp | 去掉归零绘制 |
| 6 | src/svg/node/SVGShape.cpp、SVGLinearGradient.cpp、SVGRadialGradient.cpp、SVGPattern.cpp | 渐变与 pattern 坐标空间统一 |
| 7 | test/src/SVGRenderTest.cpp、resources/apitest/SVG/ | 机制单测；BlurSVG→Filter、GradientSVG→Gradient 改名与资源扩展（见 §4.2） |

步骤 1、2、4 相互独立；3 依赖 1；5 和 6 必须一起改（只改其一会使 OBB 渐变错位）。

## 6 参考资料

- src/svg/node/SVGFilter.cpp — 滤镜链识别与 DAG 构建
- include/tgfx/svg/node/SVGFeFlood.h — feFlood 现状
- src/svg/node/SVGFe.cpp、src/svg/SVGAttributeParser.cpp:77-94 — in2 解析与前缀匹配
- src/svg/node/SVGRect.cpp:61-90 — 归零绘制
- src/svg/node/SVGShape.cpp:30-54 — paint 上下文 viewport 覆盖
- src/svg/node/SVGGradient.cpp、SVGLinearGradient.cpp、SVGRadialGradient.cpp、SVGPattern.cpp — 渐变与 pattern 的 shader 构建
- src/svg/SVGNodeConstructor.cpp:443-449 — TextLiteral 构造
- resources/apitest/SVG/filter.svg、gradient.svg — 整合后的测试资源（滤镜链与渐变取自 Pixso 导出的问题文件）
