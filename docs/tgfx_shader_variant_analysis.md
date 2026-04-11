# tgfx Shader 原子化改造：.glsl 文件清单与 UE Shader 变体评估

> 文档版本：v1.0
> 基于分支：`feature/henryjpxie_shader_permutation` HEAD（515c3b23）
> 目标：为 UE RHI 离线预编译提供 .glsl 文件清单、宏维度汇总和有效 Shader 变体数量估算

---

## 1. .glsl 文件是什么？

`.glsl` 文件是**磁盘上的独立源码文件**（位于 `src/gpu/shaders/` 目录下），但**运行时不通过文件 I/O 加载**。构建时，它们的内容被拷贝为 `ShaderModuleRegistry.cpp` 中的 C++ `R"GLSL(...)"` 原始字符串常量，编译后嵌入二进制。

运行时通过 `ShaderModuleRegistry::GetModule(id)` 获取嵌入的字符串，由 `FragmentShaderBuilder::addFunction()` 注入到 Shader 的 Functions section。

**对 UE RHI 适配的意义**：UE 端可以直接读取磁盘上的 `.glsl` 文件（或将其转为 `.ush` 格式），配合 `#define` 宏预处理出所有变体的完整 GLSL 代码，然后离线编译。

---

## 2. .glsl 文件清单（44 个，共 2148 行）

### 2.1 基础/工具（2 个）

| 文件 | 行数 | 用途 |
|------|------|------|
| `backends/tgfx_types_glsl.glsl` | 21 | GLSL 类型别名定义 |
| `common/tgfx_blend.glsl` | 298 | 15 种高级 blend mode 函数（Overlay→PlusDarker） |

### 2.2 叶子 FP — 简单（14 个）

| 文件 | 行数 | Processor | 宏数 |
|------|------|-----------|------|
| `fp/const_color.glsl` | 16 | ConstColorProcessor | 0 |
| `fp/linear_gradient_layout.glsl` | 8 | LinearGradientLayout | 0~1 |
| `fp/radial_gradient_layout.glsl` | 8 | RadialGradientLayout | 0~1 |
| `fp/conic_gradient_layout.glsl` | 9 | ConicGradientLayout | 0~1 |
| `fp/diamond_gradient_layout.glsl` | 8 | DiamondGradientLayout | 0~1 |
| `fp/single_interval_gradient_colorizer.glsl` | 8 | SingleIntervalGradientColorizer | 0 |
| `fp/dual_interval_gradient_colorizer.glsl` | 18 | DualIntervalGradientColorizer | 0 |
| `fp/texture_gradient_colorizer.glsl` | 8 | TextureGradientColorizer | 0 |
| `fp/aa_rect_effect.glsl` | 12 | AARectEffect | 0 |
| `fp/color_matrix.glsl` | 13 | ColorMatrixFragmentProcessor | 0 |
| `fp/luma.glsl` | 8 | LumaFragmentProcessor | 0 |
| `fp/alpha_threshold.glsl` | 13 | AlphaThresholdFragmentProcessor | 0 |
| `fp/device_space_texture_effect.glsl` | 17 | DeviceSpaceTextureEffect | 0~1 |

### 2.3 叶子 FP — 复杂（3 个）

| 文件 | 行数 | Processor | 宏数 |
|------|------|-----------|------|
| `fragment/texture_effect.frag.glsl` | 193 | TextureEffect | 8 |
| `fragment/tiled_texture_effect.frag.glsl` | 338 | TiledTextureEffect | 9 |
| `fragment/unrolled_binary_gradient.frag.glsl` | 117 | UnrolledBinaryGradientColorizer | 1 |

### 2.4 容器 FP（4 个）

| 文件 | 行数 | Processor | 宏数 |
|------|------|-----------|------|
| `fragment/clamped_gradient_effect.frag.glsl` | 34 | ClampedGradientEffect | 0 |
| `fragment/gaussian_blur_1d.frag.glsl` | 27 | GaussianBlur1DFragmentProcessor | 2 |
| `fragment/xfermode.frag.glsl` | 97 | XfermodeFragmentProcessor | 2 |
| `fragment/color_space_xform.frag.glsl` | 114 | ColorSpaceXformEffect | 9 |

### 2.5 GP（20 个 = 10 vert + 10 frag）

| Processor | vert 行数 | frag 行数 | 宏数 |
|-----------|---------|---------|------|
| DefaultGeometryProcessor | 24 | 21 | 1 |
| QuadPerEdgeAAGeometryProcessor | 27 | 29 | 5 |
| EllipseGeometryProcessor | 23 | 42 | 2 |
| AtlasTextGeometryProcessor | 29 | 49 | 3 |
| RoundStrokeRectGeometryProcessor | 31 | 51 | 3 |
| NonAARRectGeometryProcessor | 31 | 53 | 2 |
| MeshGeometryProcessor | 35 | 29 | 3 |
| ShapeInstancedGeometryProcessor | 27 | 23 | 2 |
| HairlineLineGeometryProcessor | 15 | 22 | 1 |
| HairlineQuadGeometryProcessor | 15 | 28 | 1 |

### 2.6 XP（2 个）

| 文件 | 行数 | Processor | 宏数 |
|------|------|-----------|------|
| `xfer/empty_xfer.frag.glsl` | 13 | EmptyXferProcessor | 0 |
| `xfer/porter_duff_xfer.frag.glsl` | 146 | PorterDuffXferProcessor | 3 |

---

## 3. 宏维度详细定义（51 个宏）

### 3.1 GeometryProcessor 宏（24 个）

| 宏名 | 类型 | GP | 含义 | 值域 |
|------|------|----|------|------|
| TGFX_GP_DEFAULT_COVERAGE_AA | bool | DefaultGP | Coverage AA | on/off |
| TGFX_GP_QUAD_COVERAGE_AA | bool | QuadPerEdgeAAGP | Coverage AA | on/off |
| TGFX_GP_QUAD_COMMON_COLOR | bool | QuadPerEdgeAAGP | 共用颜色 | on/off |
| TGFX_GP_QUAD_UV_MATRIX | bool | QuadPerEdgeAAGP | UV 矩阵 | on/off |
| TGFX_GP_QUAD_SUBSET | bool | QuadPerEdgeAAGP | Subset 约束 | on/off |
| TGFX_GP_QUAD_SUBSET_MATRIX | bool | QuadPerEdgeAAGP | Subset 矩阵（依赖 SUBSET∧UV_MATRIX） | on/off |
| TGFX_GP_ELLIPSE_STROKE | bool | EllipseGP | Stroke 模式 | on/off |
| TGFX_GP_ELLIPSE_COMMON_COLOR | bool | EllipseGP | 共用颜色 | on/off |
| TGFX_GP_ATLAS_COVERAGE_AA | bool | AtlasTextGP | Coverage AA | on/off |
| TGFX_GP_ATLAS_COMMON_COLOR | bool | AtlasTextGP | 共用颜色 | on/off |
| TGFX_GP_ATLAS_ALPHA_ONLY | bool | AtlasTextGP | Alpha-only 纹理 | on/off |
| TGFX_GP_RRECT_COVERAGE_AA | bool | RoundStrokeRectGP | Coverage AA | on/off |
| TGFX_GP_RRECT_COMMON_COLOR | bool | RoundStrokeRectGP | 共用颜色 | on/off |
| TGFX_GP_RRECT_UV_MATRIX | bool | RoundStrokeRectGP | UV 矩阵 | on/off |
| TGFX_GP_NONAA_COMMON_COLOR | bool | NonAARRectGP | 共用颜色 | on/off |
| TGFX_GP_NONAA_STROKE | bool | NonAARRectGP | Stroke 模式 | on/off |
| TGFX_GP_MESH_TEX_COORDS | bool | MeshGP | 纹理坐标 | on/off |
| TGFX_GP_MESH_VERTEX_COLORS | bool | MeshGP | 顶点色 | on/off |
| TGFX_GP_MESH_VERTEX_COVERAGE | bool | MeshGP | 顶点 Coverage | on/off |
| TGFX_GP_SHAPE_COVERAGE_AA | bool | ShapeInstancedGP | Coverage AA | on/off |
| TGFX_GP_SHAPE_VERTEX_COLORS | bool | ShapeInstancedGP | 顶点色 | on/off |
| TGFX_GP_HLINE_COVERAGE_AA | bool | HairlineLineGP | Coverage AA | on/off |
| TGFX_GP_HQUAD_COVERAGE_AA | bool | HairlineQuadGP | Coverage AA | on/off |

### 3.2 TextureEffect 宏（8 个）

| 宏名 | 类型 | 值域 | 约束 |
|------|------|------|------|
| TGFX_TE_TEXTURE_MODE | int | 0/1/2 | 0=RGBA, 1=I420, 2=NV12 |
| TGFX_TE_YUV_LIMITED_RANGE | bool | on/off | 仅当 TEXTURE_MODE≠0 |
| TGFX_TE_RGBAAA | bool | on/off | 与 ALPHA_ONLY 互斥 |
| TGFX_TE_ALPHA_ONLY | bool | on/off | 与 RGBAAA 互斥 |
| TGFX_TE_SUBSET | bool | on/off | — |
| TGFX_TE_STRICT_CONSTRAINT | bool | on/off | — |
| TGFX_TE_PERSPECTIVE | bool | on/off | — |
| TGFX_TE_SAMPLER_TYPE | string | sampler2D / sampler2DRect | — |

### 3.3 TiledTextureEffect 宏（9 个）

| 宏名 | 类型 | 值域 | 约束 |
|------|------|------|------|
| TGFX_TTE_MODE_X | int | 0-8 | ShaderMode 枚举 |
| TGFX_TTE_MODE_Y | int | 0-8 | ShaderMode 枚举 |
| TGFX_TTE_HAS_SUBSET | bool | on/off | 由 MODE_X/Y 决定 |
| TGFX_TTE_HAS_CLAMP | bool | on/off | 由 MODE_X/Y 决定 |
| TGFX_TTE_HAS_DIMENSION | bool | on/off | 由 MODE_X/Y + 纹理类型决定 |
| TGFX_TTE_STRICT_CONSTRAINT | bool | on/off | — |
| TGFX_TTE_ALPHA_ONLY | bool | on/off | — |
| TGFX_TTE_PERSPECTIVE | bool | on/off | — |
| TGFX_TTE_SAMPLER_TYPE | string | sampler2D / sampler2DRect | — |

### 3.4 XferProcessor 宏（3 个）

| 宏名 | 类型 | 值域 | 约束 |
|------|------|------|------|
| TGFX_BLEND_MODE | int | 0-29 | BlendMode 枚举（30 种） |
| TGFX_PDXP_DST_TEXTURE_READ | bool | on/off | — |
| TGFX_PDXP_NON_COEFF | bool | on/off | 由 BLEND_MODE 决定（mode>14 时 true） |

### 3.5 容器 FP 宏（4 个）

| 宏名 | 类型 | 值域 | 约束 |
|------|------|------|------|
| TGFX_XFP_CHILD_MODE | int | 0/1/2 | 0=DstChild, 1=SrcChild, 2=TwoChild |
| TGFX_BLEND_MODE | int | 0-29 | 与 XP 共用 |
| TGFX_BLUR_LOOP_LIMIT | int | 4..N | 4×maxSigma |
| TGFX_GB1D_SAMPLE(coord) | 函数宏 | — | C++ 注入子 FP 采样调用 |

### 3.6 ColorSpaceXformEffect 宏（9 个）

| 宏名 | 类型 | 值域 | 约束 |
|------|------|------|------|
| TGFX_CSX_UNPREMUL | bool | on/off | — |
| TGFX_CSX_SRC_TF | bool | on/off | — |
| TGFX_CSX_SRC_TF_TYPE | int | 1-4 | 仅当 SRC_TF=on；1=sRGBish,2=PQish,3=HLGish,4=HLGinvish |
| TGFX_CSX_SRC_OOTF | bool | on/off | — |
| TGFX_CSX_GAMUT_XFORM | bool | on/off | — |
| TGFX_CSX_DST_OOTF | bool | on/off | — |
| TGFX_CSX_DST_TF | bool | on/off | — |
| TGFX_CSX_DST_TF_TYPE | int | 1-4 | 仅当 DST_TF=on |
| TGFX_CSX_PREMUL | bool | on/off | — |

### 3.7 其他 FP 宏（3 个）

| 宏名 | 类型 | Processor | 值域 |
|------|------|-----------|------|
| TGFX_UBGC_INTERVAL_COUNT | int | UnrolledBinaryGradientColorizer | 1..8 |
| TGFX_DSTE_ALPHA_ONLY | bool | DeviceSpaceTextureEffect | on/off |
| TGFX_*GRAD_PERSPECTIVE | bool | 4 种 GradientLayout | on/off |

---

## 4. 单 Processor 有效变体数

### 4.1 GP 变体数

| GP | 独立宏数 | 约束 | 有效组合数 |
|----|---------|------|-----------|
| DefaultGP | 1 bool | — | 2 |
| QuadPerEdgeAAGP | 5 bool | SUBSET_MATRIX 依赖 SUBSET∧UV_MATRIX | 20 |
| EllipseGP | 2 bool | — | 4 |
| AtlasTextGP | 3 bool | — | 8 |
| RoundStrokeRectGP | 3 bool | — | 8 |
| NonAARRectGP | 2 bool | — | 4 |
| MeshGP | 3 bool | — | 8 |
| ShapeInstancedGP | 2 bool | — | 4 |
| HairlineLineGP | 1 bool | — | 2 |
| HairlineQuadGP | 1 bool | — | 2 |
| **GP 合计** | | | **62** |

### 4.2 FP 变体数

| FP | 宏 | 约束 | 有效组合数 |
|----|-----|------|-----------|
| ConstColorProcessor | 0 | — | 1 |
| LinearGradientLayout | 1 bool (PERSPECTIVE) | — | 2 |
| RadialGradientLayout | 1 bool | — | 2 |
| ConicGradientLayout | 1 bool | — | 2 |
| DiamondGradientLayout | 1 bool | — | 2 |
| SingleIntervalGradientColorizer | 0 | — | 1 |
| DualIntervalGradientColorizer | 0 | — | 1 |
| TextureGradientColorizer | 0 | — | 1 |
| AARectEffect | 0 | — | 1 |
| ColorMatrixFragmentProcessor | 0 | — | 1 |
| LumaFragmentProcessor | 0 | — | 1 |
| AlphaThresholdFragmentProcessor | 0 | — | 1 |
| DeviceSpaceTextureEffect | 1 bool | — | 2 |
| **TextureEffect** | 3 int值 + 6 bool + 1 sampler | RGBAAA⊕ALPHA_ONLY互斥; YUV_LIMITED_RANGE依赖MODE≠0 | **~288** |
| **TiledTextureEffect** | 2 int(9值) + 4 bool + 1 sampler | HAS_SUBSET/CLAMP/DIMENSION由MODE决定 | **~648** |
| UnrolledBinaryGradientColorizer | 1 int(1-8) | — | 8 |
| ClampedGradientEffect | 0（子 FP 组合在 Program 级） | — | 1 |
| GaussianBlur1DFragmentProcessor | 1 int(LOOP_LIMIT) + 1 函数宏 | LOOP_LIMIT=4×maxSigma，常见值10/20/40 | ~3-5 |
| XfermodeFragmentProcessor | 1 int(CHILD_MODE 0-2) + 1 int(BLEND_MODE 0-29) | — | 90 |
| ColorSpaceXformEffect | 7 bool + 2 条件 int(1-4) | SRC_TF_TYPE仅当SRC_TF=on; DST_TF_TYPE仅当DST_TF=on | **~512** |
| **FP 合计** | | | **~1,573** |

### 4.3 XP 变体数

| XP | 宏 | 约束 | 有效组合数 |
|----|-----|------|-----------|
| EmptyXferProcessor | 0 | — | 1 |
| PorterDuffXferProcessor | 1 int(0-29) + 1 bool + 1 依赖bool | NON_COEFF由BLEND_MODE决定 | 60 |
| **XP 合计** | | | **61** |

---

## 5. UE 平台 Shader 变体数量估算

### 5.1 估算方法

UE 的 Shader 变体是一个完整的 Program = GP(.vert + .frag) + FP 链 + XP。但 tgfx 中每种 GP 类型只搭配特定的 FP/XP 组合（不是任意笛卡尔积）。

**关键约束：一次 Draw Call 中只有 1 个 GP + N 个 FP（串联）+ 1 个 XP。** FP 链的长度和类型由渲染场景决定。

### 5.2 典型 Program 组合场景

| 场景 | GP | FP 链 | XP | 变体估算 |
|------|-----|-------|-----|---------|
| 纯色填充 | DefaultGP(2) | ConstColor(1) | PorterDuff(60) | 2×1×60 = 120 |
| 图片绘制 | QuadPerEdgeAAGP(20) | TextureEffect(288) | PorterDuff(60) | 20×288×60 = 345,600 |
| 带 ColorMatrix 的图片 | QuadPerEdgeAAGP(20) | TextureEffect(288) → ColorMatrix(1) | PorterDuff(60) | 20×288×1×60 = 345,600 |
| 线性渐变 | QuadPerEdgeAAGP(20) | ClampedGradient(1) → [LinearLayout(2) + SingleInterval(1)] | PorterDuff(60) | 20×1×2×1×60 = 2,400 |
| 高斯模糊 | QuadPerEdgeAAGP(20) | GaussianBlur1D(~5) → TextureEffect(288) | PorterDuff(60) | 20×5×288×60 = 1,728,000 |
| Tiled 纹理 | QuadPerEdgeAAGP(20) | TiledTextureEffect(648) | PorterDuff(60) | 20×648×60 = 777,600 |
| 文本绘制 | AtlasTextGP(8) | — | EmptyXP(1) | 8×1 = 8 |
| 椭圆/圆 | EllipseGP(4) | — | PorterDuff(60) | 4×60 = 240 |
| 圆角矩形 | RoundStrokeRectGP(8) | — | PorterDuff(60) | 8×60 = 480 |
| 色彩空间转换 | QuadPerEdgeAAGP(20) | TextureEffect(288) → ColorSpaceXform(512) | PorterDuff(60) | 极大 |

### 5.3 理论上界 vs 实际上界

**理论上界**（笛卡尔积）：62 × 1,573 × 61 = **5,949,686** — 约 600 万

但大量组合在实际中不会出现：
- 不是每个 GP 都搭配每个 FP
- TextureEffect 的 288 个变体中大多数是极端组合（YUV + RGBAAA + Perspective + Strict + Subset 同时出现的概率极低）
- ColorSpaceXform 的 512 个变体中多数标志组合不会出现（如 srcOOTF + dstOOTF 几乎不同时使用）
- TiledTextureEffect 的 9×9=81 种 MODE 组合中，实际常用的不超过 10 种

### 5.4 实际有效变体估算

| 场景类别 | 实际常用变体数 | 说明 |
|---------|-------------|------|
| 图片绘制（TextureEffect） | ~500 | 常用：MODE=0, 非 YUV, SUBSET/STRICT 几种组合 |
| Tiled 纹理 | ~200 | 常用 Clamp/Repeat/Mirror 的 X/Y 组合 |
| 渐变 | ~100 | 4 种 layout × ~5 种 colorizer × 几种 ClampedGradient 参数 |
| 高斯模糊 | ~50 | ~5 种 LOOP_LIMIT × ~10 种 TextureEffect 变体 |
| Xfermode 合成 | ~90 | 3 种 child mode × 30 种 blend mode |
| 色彩空间转换 | ~30 | 常用：sRGB↔Linear，偶尔 P3/Rec2020 |
| GP 变体 | ~30 | 常用 GP 类型的常见配置 |
| XP 变体 | ~10 | 常用：SrcOver, Clear, 少数高级 blend |
| **实际有效变体合计** | **~1,000 - 3,000** | 保守估计 |

### 5.5 UE ShouldCompilePermutation 裁剪建议

在 UE 的 `ShouldCompilePermutation` 中可以应用以下裁剪规则减少变体数：

1. **TextureEffect**：YUV 模式（MODE=1/2）仅在视频播放场景编译，非视频场景裁掉 ~200 个变体
2. **TiledTextureEffect**：MODE_X/Y 的 9×9=81 组合裁剪为常用 ~15 种（Clamp×Clamp, Repeat×Repeat, Mirror×Mirror, Clamp×Repeat 等）
3. **ColorSpaceXform**：7 个 bool 的 128 种组合裁剪为常用 ~10 种（sRGB→Linear, Linear→sRGB, P3→sRGB 等）
4. **PorterDuffXP**：30 种 blend mode 裁剪为常用 ~8 种（SrcOver, Clear, Src, DstIn, DstOut, SrcATop, Screen, Multiply）
5. **GP 变体**：CommonColor 在运行时确定，但 CoverageAA 可编译期裁剪

**裁剪后估计：500 - 1,500 个有效 Shader 变体。** 这在 UE 项目的正常范围内（UE 自身材质系统通常有数千到数万个变体）。

---

## 6. 结论

| 指标 | 数值 |
|------|------|
| .glsl 文件总数 | 44 个 |
| .glsl 代码总行数 | 2,148 行 |
| 宏维度总数 | 51 个 |
| 单 Processor 变体数合计 | GP 62 + FP ~1,573 + XP 61 = ~1,696 |
| 理论 Program 变体上界 | ~600 万（笛卡尔积，绝大多数无效） |
| 实际有效 Program 变体估算 | 1,000 - 3,000 |
| UE 裁剪后变体估算 | 500 - 1,500 |
