# Flutter Impeller 渲染引擎 Shader 架构分析

## 修订历史

| 版本 | 日期 | 说明 |
|:----:|------|------|
| v1.0 | 2026-06-24 | 初稿 |
| v1.1 | 2026-06-24 | 补充源码级实现细节：Specialization Constant 机制、ContentContext 变体管理、Contents 渲染流程、Shader 源码分析、滤镜多 Pass 分解实现等 |

---

## 1. 研究目的

本文对 Flutter Impeller 渲染引擎的 shader 架构进行系统性分析，重点关注以下问题：

1. Impeller 如何实现"运行时零 shader 编译"的设计目标
2. Shader 的组织方式、数量规模与变体策略
3. 效果组合（ColorFilter、BlendMode、MaskFilter 等）的处理机制
4. 该架构对 tgfx 原子 Shader 方案的参考价值与差异

---

## 2. 背景与动机

### 2.1 Skia 架构的问题

Flutter 早期使用 Skia 作为渲染后端。Skia 采用与 tgfx 相同的 Processor 树动态拼接架构：运行时根据 Paint 属性组合动态生成 GLSL/SkSL，再编译为 GPU shader。该模式导致首帧渲染时出现 shader 编译卡顿（shader compilation jank），在低端设备上尤为严重。

Flutter 团队自 2015 年起尝试了三种缓解方案，均未成功：

| 方案 | 失败原因 |
|------|---------|
| 通用 shader 预热 | 无法覆盖所有运行时组合 |
| 用户侧 shader 预热 API | 平台相关、增加包体积、部分应用首帧延迟达 6 秒 |
| 训练式 SkSL 采集与打包 | 设备差异导致缓存失效，无 Google 内部应用采用 |

### 2.2 Impeller 的核心设计原则

Impeller 于 2021 年立项，2023 年在 iOS 上默认启用。其核心设计原则为：

> "All shader compilation and reflection is performed offline at build time. All pipeline state objects are built upfront. Caching is explicit and under the control of the engine."

即：**所有 shader 编译和反射在构建期离线完成，所有 Pipeline State Object (PSO) 预先构建，缓存由引擎显式控制**。

---

## 3. Shader 编译工具链

### 3.1 编译流水线

Impeller 的 shader 编译采用单源多目标（single-source, multi-target）模型。所有 shader 以 GLSL 4.60 编写一次，通过构建期工具链转换为各后端格式：

```
GLSL 4.60 源码
    │
    ▼
impellerc（Impeller Shader Compiler）
    │
    ├─ SPIR-V（未优化）
    │      │
    │      ▼
    │   SPIR-V 优化
    │      │
    │      ├─ Metal Shading Language → Metal Library
    │      ├─ Vulkan SPIR-V → Shader Archive
    │      └─ GLSL ES 1.00 → Shader Archive (via blobcat)
    │
    └─ SPIR-V Reflection → C++ Binding 代码生成
```

### 3.2 Reflection 与类型安全绑定

`impellerc` 在编译 SPIR-V 的同时执行 reflection，自动生成 C++ 头文件，包含：

- 具有正确 padding 和 alignment 的 uniform struct 定义
- Vertex 数据布局描述
- Sampler 绑定信息

**关键设计**：shader 接口的任何变更都会导致 C++ 编译错误，从而在构建期而非运行时暴露不一致。这消除了手动维护 uniform 偏移量的需要。

### 3.3 Reflection 生成的 C++ 绑定示例

以 `SolidColorContents` 的渲染流程为例，Reflection 生成的绑定如何在实际代码中使用：

```cpp
// impellerc 自动生成的类型别名
using VS = SolidFillPipeline::VertexShader;
using FS = SolidFillPipeline::FragmentShader;

// FS::FragInfo 是 impellerc 从 solid_fill.frag 的 uniform block 自动生成的 C++ struct
// 字段名、类型、对齐方式与 GLSL 完全一致
FS::FragInfo frag_info;
frag_info.color = GetColor().Premultiply() *
                  GetGeometry()->ComputeAlphaCoverage(entity.GetTransform());

// FS::BindFragInfo 是自动生成的静态方法，将 struct 绑定到正确的 slot/offset
FS::BindFragInfo(pass, data_host_buffer.EmplaceUniform(frag_info));
```

**关键特性**：
- `FragInfo` struct 的字段名与 shader 源码中的 uniform 变量名一一对应
- `EmplaceUniform()` 将 struct 写入 transient host buffer 并返回 `BufferView`
- `BindFragInfo()` 在编译期确定 binding slot，无需运行时字符串查找
- shader 中增删字段会导致 C++ 侧编译错误，强制保持同步

### 3.4 二进制打包

编译后的 shader 通过 `xxd.py` 转换为 C 源文件中的 hex dump，直接编译进引擎二进制，不依赖外部文件加载。

---

## 4. Shader 文件清单与分类

### 4.1 文件统计

基于 flutter/flutter 仓库 master 分支（截至 2026 年 6 月），`impeller/entity/shaders/` 目录下的 shader 文件完整清单如下：

#### 4.1.1 根目录（基础渲染）

| 文件名 | 类型 | 功能 |
|--------|:----:|------|
| `solid_fill.vert/frag` | VS+FS | 纯色填充 |
| `texture_fill.vert/frag` | VS+FS | 纹理填充 |
| `texture_fill_strict_src.frag` | FS | 严格源约束的纹理填充 |
| `tiled_texture_fill.frag` | FS | 平铺纹理填充 |
| `tiled_texture_fill_external.frag` | FS | 外部纹理（Android HardwareBuffer）|
| `texture_uv_fill.vert` | VS | UV 变换的纹理填充顶点 |
| `circle.vert/frag` | VS+FS | 圆形 SDF 渲染 |
| `line.vert/frag` | VS+FS | 线段渲染 |
| `clip.vert/frag` | VS+FS | 裁剪操作 |
| `glyph_atlas.vert/frag` | VS+FS | 文字 atlas 渲染 |
| `rrect_like_blur.vert` | VS | 圆角矩形模糊顶点 |
| `rrect_blur.frag` | FS | 圆角矩形解析模糊 |
| `rsuperellipse_blur.frag` | FS | 超椭圆解析模糊 |
| `complex_rse.frag` | FS | 复杂超椭圆 |
| `shadow_vertices.vert/frag` | VS+FS | 阴影顶点渲染 |
| `uber_sdf.frag` | FS | 统一 SDF shader（圆角矩形/圆/线等）|
| `runtime_effect.vert` | VS | RuntimeEffect 顶点 |
| `texture_downsample.frag` | FS | 纹理降采样 |
| `texture_downsample_bounded.frag` | FS | 带边界约束的纹理降采样 |
| `texture_downsample_gles.frag` | FS | GLES 专用纹理降采样 |
| `downsample.glsl` | 共享 | 降采样工具函数 |
| `sdf_functions.glsl` | 共享 | SDF 工具函数 |
| `sdf_utils.glsl` | 共享 | SDF 辅助函数 |

#### 4.1.2 `blending/` 子目录（混合模式）

| 文件名 | 类型 | 功能 |
|--------|:----:|------|
| `advanced_blend.vert/frag` | VS+FS | 高级混合（离屏采样 dst）|
| `framebuffer_blend.vert/frag` | VS+FS | 帧缓冲混合（FB fetch 优化）|
| `porter_duff_blend.vert/frag` | VS+FS | Porter-Duff 混合 |
| `vertices_uber_1.frag` | FS | drawVertices uber shader 变体 1 |
| `vertices_uber_2.frag` | FS | drawVertices uber shader 变体 2 |
| `vertices_uber.glsl` | 共享 | drawVertices uber 工具函数 |
| `blend_select.glsl` | 共享 | 混合模式选择函数 |

#### 4.1.3 `filters/` 子目录（滤镜效果）

| 文件名 | 类型 | 功能 |
|--------|:----:|------|
| `gaussian.frag` | FS | 高斯模糊 |
| `border_mask_blur.frag` | FS | 边框遮罩模糊 |
| `color_matrix_color_filter.frag` | FS | 颜色矩阵变换 |
| `linear_to_srgb_filter.frag` | FS | Linear → sRGB 色彩空间转换 |
| `srgb_to_linear_filter.frag` | FS | sRGB → Linear 色彩空间转换 |
| `morphology_filter.frag` | FS | 形态学滤镜（膨胀/腐蚀）|
| `yuv_to_rgb_filter.frag` | FS | YUV → RGB 色彩空间转换 |
| `filter_position.vert` | VS | 滤镜定位顶点 |
| `filter_position_uv.vert` | VS | 带 UV 的滤镜定位顶点 |

#### 4.1.4 `gradients/` 子目录（渐变）

| 文件名 | 类型 | 功能 |
|--------|:----:|------|
| `fast_gradient.vert/frag` | VS+FS | 快速渐变（2 色）|
| `gradient_fill.vert` | VS | 通用渐变顶点 |
| `linear_gradient_fill.frag` | FS | 线性渐变（texture colorizer）|
| `linear_gradient_ssbo_fill.frag` | FS | 线性渐变（SSBO colorizer）|
| `linear_gradient_uniform_fill.frag` | FS | 线性渐变（uniform colorizer）|
| `radial_gradient_fill.frag` | FS | 径向渐变（texture）|
| `radial_gradient_ssbo_fill.frag` | FS | 径向渐变（SSBO）|
| `radial_gradient_uniform_fill.frag` | FS | 径向渐变（uniform）|
| `sweep_gradient_fill.frag` | FS | 扫描渐变（texture）|
| `sweep_gradient_ssbo_fill.frag` | FS | 扫描渐变（SSBO）|
| `sweep_gradient_uniform_fill.frag` | FS | 扫描渐变（uniform）|
| `conical_gradient_fill_conical.frag` | FS | 锥形渐变-锥形（texture）|
| `conical_gradient_fill_radial.frag` | FS | 锥形渐变-径向（texture）|
| `conical_gradient_fill_strip.frag` | FS | 锥形渐变-条带（texture）|
| `conical_gradient_fill_strip_radial.frag` | FS | 锥形渐变-条带径向（texture）|
| `conical_gradient_ssbo_fill.frag` | FS | 锥形渐变（SSBO）|
| `conical_gradient_uniform_fill_conical.frag` | FS | 锥形渐变-锥形（uniform）|
| `conical_gradient_uniform_fill_radial.frag` | FS | 锥形渐变-径向（uniform）|
| `conical_gradient_uniform_fill_strip.frag` | FS | 锥形渐变-条带（uniform）|
| `conical_gradient_uniform_fill_strip_radial.frag` | FS | 锥形渐变-条带径向（uniform）|

### 4.2 汇总统计

| 目录 | .vert | .frag | .glsl | 小计 |
|------|:-----:|:-----:|:-----:|:----:|
| 根目录 | 10 | 17 | 3 | 30 |
| `blending/` | 3 | 5 | 2 | 10 |
| `filters/` | 2 | 7 | 0 | 9 |
| `gradients/` | 2 | 21 | 0 | 23 |
| **合计** | **17** | **50** | **5** | **72** |

**独立 shader 程序数**（VS+FS 对或独立 FS）：约 **50 个**，与官方 FAQ 中 "fewer than 50 shaders" 的表述一致。

---

## 5. Pipeline 架构与变体管理

### 5.1 ContentContext::Pipelines 结构

Impeller 在 `ContentContext::Pipelines` 结构体中预定义了全部 pipeline 类型。根据 API 文档（`content_context.cc` 第 216–307 行），共计 **92 个 pipeline 成员**，分类如下：

| 类别 | 数量 | 说明 |
|------|:----:|------|
| Advanced Blend | 15 | 离屏采样 dst 的高级混合（ColorBurn ~ Luminosity）|
| Framebuffer Blend | 15 | FB fetch 优化的高级混合（同上 15 种 mode）|
| Porter-Duff Blend | 15 | 基础混合（Clear ~ Xor），共享 `PorterDuffBlendPipeline` |
| Conical Gradient | 12 | 3 种存储策略 × 4 种几何变体 |
| Linear/Radial/Sweep Gradient | 9 | 3 种渐变类型 × 3 种存储策略 |
| Geometry & Effects | 14 | 模糊、裁剪、SDF、ColorMatrix、色彩空间转换等 |
| Texture | 5 | 基础纹理、严格源约束纹理、降采样、平铺纹理 |
| Vertices/SDF/Other | 5 | 阴影、uber SDF、YUV 转换 |
| Fast Gradient | 1 | 2 色快速路径 |
| Glyph Atlas | 1 | 文字渲染 |

### 5.2 Variants 模板与变体缓存机制

#### 5.2.1 数据结构

每个 pipeline 成员使用 `Variants<T>` 模板包装。`Variants` 继承自 `GenericVariants`，其内部存储结构为：

```cpp
class GenericVariants {
  // 扁平 vector 存储 pipeline 变体，key = ContentContextOptions::ToKey() 产生的 64-bit 哈希
  std::vector<std::pair<uint64_t, std::unique_ptr<GenericRenderPipelineHandle>>> pipelines_;
  // 缓存的 PipelineDescriptor（从默认变体拷贝后修改选项即可派生新变体）
  std::optional<PipelineDescriptor> desc_;
  // 默认选项集
  std::optional<ContentContextOptions> default_options_;
};
```

使用**扁平 vector 线性查找**而非 hash map——因为同一 pipeline 的变体数通常很少（3-5 个），线性查找比 hash map 更高效。

#### 5.2.2 ContentContextOptions 的 64-bit Key

`ContentContextOptions` 将以下渲染状态编码为一个 64-bit key：

| 字段 | 大小 | 说明 |
|------|------|------|
| `sample_count` | 1 byte | MSAA 采样数 |
| `blend_mode` | 1 byte | 混合模式 |
| `depth_compare` | 1 byte | 深度比较函数 |
| `stencil_mode` | 1 byte | 模板测试配置 |
| `primitive_type` | 1 byte | 图元类型（Triangle/TriangleStrip/Line 等） |
| `color_attachment_pixel_format` | 1 byte | 输出像素格式 |
| `has_depth_stencil_attachments` | 1 bit | 是否有深度/模板附件 |
| `depth_write_enabled` | 1 bit | 是否允许深度写入 |
| `is_for_rrect_blur_clear` | 1 bit | 圆角矩形模糊清除优化标志 |

通过 `ApplyToPipelineDescriptor()` 方法，这些高层选项被转换为低层 PSO 状态：
- `blend_mode` → 设置 color attachment 0 的 `src/dst color/alpha blend factors`
- `stencil_mode` → 配置 front/back stencil operations（支持 NonZeroFill、EvenOddFill、CoverCompare 等模式）
- `depth_compare` → 设置深度比较函数

#### 5.2.3 变体创建策略："预热默认 + 懒派生"

Impeller 采用两阶段策略：

**阶段一：构造时异步预编译默认变体**

在 `ContentContext` 构造函数中，为全部 ~92 个 pipeline 类型创建默认变体。预定义三组默认选项：

| 选项集 | 用途 |
|--------|------|
| `options`（MSAA 4x, Triangle） | 大部分渲染 pipeline |
| `options_trianglestrip`（MSAA 4x, TriangleStrip） | 纹理填充等 |
| `options_no_msaa_no_depth_stencil`（1x, 无深度/模板） | 后处理 Pass（滤镜、混合） |

每个默认变体通过 `CreateDefault()` **异步编译**——shader 编译在后台线程进行，不阻塞主线程。

**阶段二：运行时懒派生新变体**

当 `Get*Pipeline(options)` 请求的 options 与默认不同时，`CreateIfNeeded()` 执行：

1. 查找已有变体 → 命中则直接返回
2. 检查是否匹配默认 → 匹配则返回默认
3. **等待默认变体编译完成**（阻塞，但通常已完成）
4. 从默认变体的已编译 PSO **派生**新变体（`CreateVariant(/*async=*/false, ...)`）——拷贝 PipelineDescriptor，修改新选项，同步创建 PSO
5. 缓存并返回

**关键**：派生变体是**同步**的但非常快——因为 shader 已经编译，只需重组装 PSO（修改 blend/stencil/depth state）。这与运行时编译 shader 有本质区别。

官方注释中的设计约束：

> "A sufficiently complicated Flutter application may easily require building hundreds of PSOs in total, but they shouldn't require e.g. 10s of thousands."

### 5.3 Specialization Constant 的使用

Impeller 在两类场景中使用了 specialization constant，这是理解 "50 个 shader → 92 个 pipeline" 映射关系的关键。

#### 5.3.1 Porter-Duff Blend：广义混合方程

15 个 Porter-Duff 混合模式共享 `porter_duff_blend.frag`，通过 6 个 specialization constant 参数化：

```glsl
layout(constant_id = 0) const float supports_decal = 1.0;
layout(constant_id = 1) const float src_coeff = 1.0;
layout(constant_id = 2) const float src_coeff_dst_alpha = 1.0;
layout(constant_id = 3) const float dst_coeff = 1.0;
layout(constant_id = 4) const float dst_coeff_src_alpha = 1.0;
layout(constant_id = 5) const float dst_coeff_src_color = 1.0;
```

shader 中的广义混合方程为：

```glsl
frag_color = src * (src_coeff + dst.a * src_coeff_dst_alpha) +
             dst * (dst_coeff + src.a * dst_coeff_src_alpha +
                    src * dst_coeff_src_color);
```

各模式的系数配置：

| BlendMode | src_coeff | src_coeff_dst_alpha | dst_coeff | dst_coeff_src_alpha | dst_coeff_src_color |
|---|:---:|:---:|:---:|:---:|:---:|
| Clear | 0 | 0 | 0 | 0 | 0 |
| Source | 1 | 0 | 0 | 0 | 0 |
| Destination | 0 | 0 | 1 | 0 | 0 |
| SourceOver | 1 | 0 | 1 | -1 | 0 |
| DestOver | 1 | -1 | 1 | 0 | 0 |
| SourceIn | 0 | 1 | 0 | 0 | 0 |
| DestIn | 0 | 0 | 0 | 1 | 0 |
| SourceOut | 1 | -1 | 0 | 0 | 0 |
| DestOut | 0 | 0 | 1 | -1 | 0 |
| SourceATop | 0 | 1 | 1 | -1 | 0 |
| DestATop | 1 | -1 | 0 | 1 | 0 |
| XOR | 1 | -1 | 1 | -1 | 0 |
| Plus | 1 | 0 | 1 | 0 | 0 |
| Modulate | 0 | 0 | 0 | 0 | 1 |

**效率保证**：specialization constant 在 pipeline 创建时解析，GPU 编译器消除乘以 0 的项，使每个特化变体与手写单独 shader 性能一致。

#### 5.3.2 Advanced Blend：混合模式选择

`advanced_blend.frag` 和 `framebuffer_blend.frag` 使用 specialization constant 选择具体的高级混合算法：

```glsl
layout(constant_id = 0) const float blend_type = 0.0;  // 选择混合方程
layout(constant_id = 1) const float supports_decal = 1.0;  // 控制采样路径
```

混合模式通过 `blend_select.glsl` 中的 `AdvancedBlend()` 函数分发，该函数使用 `if` 链（而非 `switch`，因 GLSL ES 1.0 不支持 `switch`）：

| blend_type | 模式 | blend_type | 模式 |
|:---:|---|:---:|---|
| 0 | Screen | 8 | Difference |
| 1 | Overlay | 9 | Exclusion |
| 2 | Darken | 10 | Multiply |
| 3 | Lighten | 11 | Hue |
| 4 | ColorDodge | 12 | Saturation |
| 5 | ColorBurn | 13 | Color |
| 6 | HardLight | 14 | Luminosity |
| 7 | SoftLight | | |

为降低 shader 寄存器压力，`blend_select.glsl` 提供了三个变体：
- `AdvancedBlend()`：覆盖全部 15 种模式
- `AdvancedBlendHalf1()`：覆盖前 7 种（Screen ~ HardLight）
- `AdvancedBlendHalf2()`：覆盖后 8 种（SoftLight ~ Luminosity）

`vertices_uber_1.frag` 和 `vertices_uber_2.frag` 分别使用 `Half1` 和 `Half2`，避免单个 shader 中 15 路分支导致的寄存器溢出。

#### 5.3.3 Gaussian Blur：Decal 模式

`gaussian.frag` 使用 specialization constant 控制纹理边界行为：

```glsl
layout(constant_id = 0) const float supports_decal = 1.0;
```

- `supports_decal = 1.0`：使用硬件 decal 采样模式（边界外返回透明）
- `supports_decal = 0.0`：回退到 `IPHalfSampleDecal()` 软件模拟

### 5.4 50 个 Shader 如何生成 92 个 Pipeline

shader 文件数（~50）与 pipeline 数（92）的差异源于以下因素：

1. **Specialization constant 复用**：`porter_duff_blend.frag` 通过 5 个系数的不同组合生成 15 个 pipeline；`advanced_blend.frag` 通过 `blend_type` 生成 15 个 pipeline
2. **同一 fragment shader 搭配不同 vertex shader**：如 `gaussian.frag` 可与 `filter_position.vert` 或 `filter_position_uv.vert` 搭配
3. **Framebuffer blend 与 advanced blend 是同一混合算法的两种实现**：前者使用 FB fetch 读取 dst（单 Pass），后者通过离屏 texture 采样 dst（多 Pass）

---

## 6. 渲染流程与 Contents 架构

### 6.1 Contents 类层次结构

Impeller 将渲染内容抽象为 `Contents` 类层次：

```
Contents (抽象基类)
├── ColorSourceContents (颜色源基类)
│   ├── SolidColorContents      → solid_fill pipeline
│   ├── TextureContents         → texture_fill pipeline
│   ├── TiledTextureContents    → tiled_texture_fill pipeline
│   ├── LinearGradientContents  → linear_gradient_fill pipeline
│   ├── RadialGradientContents  → radial_gradient_fill pipeline
│   ├── SweepGradientContents   → sweep_gradient_fill pipeline
│   └── ConicalGradientContents → conical_gradient_fill pipeline
├── FilterContents (滤镜基类)
│   ├── ColorFilterContents
│   │   └── BlendFilterContents
│   ├── GaussianBlurFilterContents
│   ├── BorderMaskBlurFilterContents
│   ├── DirectionalMorphologyFilterContents
│   ├── MatrixFilterContents
│   ├── LocalMatrixFilterContents
│   ├── YUVToRGBFilterContents
│   └── RuntimeEffectFilterContents
└── AnonymousContents (内联 render proc)
```

**关键设计**：`ColorSourceContents` 与 `Geometry` 正交分离——颜色源定义"怎么着色"，Geometry 定义"在哪着色"。两者独立变化，通过 `DrawGeometry<VS>()` 模板方法组合。

### 6.2 SolidColorContents 渲染流程（典型示例）

以最简单的 `SolidColorContents` 为例，展示完整的渲染管线：

```
SolidColorContents::Render(renderer, entity, pass)
│
├─ 1. 准备 uniform 数据
│     VS::FrameInfo frame_info;        // MVP 矩阵（由 DrawGeometry 填充）
│     FS::FragInfo frag_info;
│     frag_info.color = GetColor()
│         .Premultiply()               // 转为预乘 alpha
│         * ComputeAlphaCoverage();    // 乘以几何体的 alpha 覆盖率（细线抗锯齿）
│
├─ 2. 通过回调选择 pipeline
│     pipeline_callback = [](options) {
│         return renderer.GetSolidFillPipeline(options);
│     };
│
└─ 3. 分发到 DrawGeometry<VS>()
      DrawGeometry<VS>(renderer, entity, pass, pipeline_callback, frame_info,
          [&](RenderPass& pass) {
              FS::BindFragInfo(pass, host_buffer.EmplaceUniform(frag_info));
              pass.SetCommandLabel("Solid Fill");
              return true;
          });
```

#### DrawGeometry 内部流程

`ColorSourceContents::DrawGeometry<VS>()` 是所有颜色源的核心渲染方法：

| 步骤 | 操作 | 说明 |
|:----:|------|------|
| 1 | 判断几何模式 | 简单填充 / Stencil-then-Cover（复杂路径 non-zero/even-odd）/ 防过绘（描边） |
| 2 | Stencil 准备（如需） | 使用 `ClipPipeline` 写入 stencil 值 |
| 3 | 设置顶点缓冲 | 从 Geometry 获取 vertex buffer |
| 4 | 配置 ContentContextOptions | 设置 `blend_mode`、`stencil_mode`、`depth_write`、`primitive_type` |
| 5 | 调用 fragment bind 回调 | 子类绑定各自的 fragment uniform |
| 6 | 通过 pipeline_callback 选择 pipeline | 基于累积的 options 获取正确的 PSO |
| 7 | 发射 draw 命令 | `pass.Draw()` |

#### ColorFilter 折叠优化

`SolidColorContents` 提供了一个重要的优化——`ApplyColorFilter()`：

```cpp
bool ApplyColorFilter(const ColorFilterProc& color_filter_proc) {
    color_ = color_filter_proc(color_);  // 直接修改颜色值
    return true;                          // 无需额外 Pass
}
```

当 `ColorFilter` 可以折叠为颜色变换时（如 `Paint` 中的 ColorFilter 应用于纯色填充），Impeller 直接修改颜色值而非创建额外的离屏 Pass。这是一个关键的 **Pass 消除**优化。

### 6.3 TextureContents 渲染流程

`TextureContents` 展示了稍复杂的管线配置：

```
TextureContents::Render(renderer, entity, pass)
│
├─ 1. 构造 4 顶点 quad（TriangleStrip）
│     vertices[4] = {
│         {dest_rect.LeftTop(),    tex_coords.LeftTop()},
│         {dest_rect.RightTop(),   tex_coords.RightTop()},
│         {dest_rect.LeftBottom(), tex_coords.LeftBottom()},
│         {dest_rect.RightBottom(),tex_coords.RightBottom()},
│     };
│
├─ 2. Pipeline 选择（三种路径）
│     ├─ strict_source_rect → GetTextureStrictSrcPipeline()
│     ├─ External OES 纹理 → GetTiledTextureExternalPipeline()
│     └─ 默认 → GetTexturePipeline()
│
├─ 3. 绑定 uniform
│     ├─ VS: frame_info.mvp = entity.GetShaderTransform(pass)
│     └─ FS: alpha（组合不透明度）
│            source_rect（严格源约束时，缩进 0.5 texel 防止线性滤波溢出）
│
└─ 4. 绑定纹理 + 采样器
      sampler = SamplerLibrary.GetSampler(sampler_descriptor_)
```

**Snapshot 快速路径**：当源矩形覆盖整个纹理且不透明度为 1.0 时，`RenderToSnapshot()` 直接返回纹理引用和变换矩阵，完全跳过渲染——这在滤镜链中是重要的优化。

---

## 7. 效果组合处理策略

### 7.1 核心设计：参数化而非组合

Impeller 官方 FAQ 指出：

> "Due to how rendering intent is **parameterized**, Impeller needs **far fewer shaders than Skia**."

即 Impeller 通过**参数化渲染意图**来减少 shader 数量，而非为每种效果组合编写独立 shader。具体策略：

1. **颜色源**（Shader）作为独立 Contents 类型，每种 Contents 对应一个预编译 shader：`SolidColorContents` → `solid_fill`，`TextureContents` → `texture_fill`，`LinearGradientContents` → `linear_gradient_fill`，等
2. **效果**（ColorFilter、ImageFilter）通过独立的滤镜 Pass 处理，而非内联到颜色源 shader 中

### 7.2 ColorFilter 的处理方式

Impeller 中 `ColorFilter` 不与颜色源 shader 合并在同一 Pass 中。当 `Paint` 包含 `colorFilter` 时，Impeller 的处理策略取决于滤镜类型：

| ColorFilter 类型 | 对应 Pipeline | 处理方式 |
|---|---|---|
| ColorMatrixFilter | `color_matrix_color_filter` | 独立 Pass：先渲染内容到离屏纹理，再对纹理应用颜色矩阵变换 |
| BlendModeFilter | `porter_duff_blend` / `advanced_blend` | 独立合成 Pass |
| sRGB/Linear 转换 | `linear_to_srgb_filter` / `srgb_to_linear_filter` | 独立 Pass |

#### ColorMatrixFilter Shader 源码分析

`color_matrix_color_filter.frag` 的完整处理流程：

```glsl
// 1. 采样输入纹理并乘以 input_alpha
f16vec4 color = texture(input_texture, v_texture_coords) * frag_info.input_alpha;

// 2. 反预乘（颜色矩阵变换在非预乘空间进行）
color = IPHalfUnpremultiply(color);

// 3. 4×4 矩阵乘法 + 平移向量
//    color_m: 4×4 矩阵部分, color_v: 第 5 列（平移）
color = clamp(mat4(frag_info.color_m) * color + frag_info.color_v, 0.0, 1.0);

// 4. 重新预乘并乘以 output_alpha
frag_color = IPHalfPremultiply(color) * frag_info.output_alpha;
```

注意 `input_alpha` 和 `output_alpha` 两个独立的 alpha 控制——`input_alpha` 用于 saveLayer 的不透明度传播，`output_alpha` 用于合成时的额外不透明度调制。

关键函数 `IsPipelineBlendOrMatrixFilter()` 判断滤镜是否可以在标准 pipeline 中直接执行。对于不可内联的滤镜，Impeller 通过 `saveLayer` 机制隐式创建离屏 Pass。

### 7.3 BlendMode 的处理方式

Impeller 将 BlendMode 分为三个层级：

| 层级 | BlendMode | 实现方式 |
|------|-----------|---------|
| 硬件混合 | Clear ~ Screen（前 15 种） | 硬件 blend state，零额外开销 |
| Framebuffer Blend | Overlay ~ Luminosity（后 15 种）| `framebuffer_blend.frag` + FB fetch 读取 dst |
| Advanced Blend | 同上（无 FB fetch 时的 fallback） | `advanced_blend.frag` + 离屏 texture 采样 dst |

当设备支持 framebuffer fetch（如 Apple GPU）时，高级混合在单 Pass 中完成；否则退化为多 Pass（先渲染到离屏 texture，再混合）。

#### BlendFilterContents 的四条渲染路径

`BlendFilterContents` 根据混合模式和设备能力选择不同的渲染路径：

| 路径 | 触发条件 | 是否离屏 | Draw Call 数 | MSAA |
|------|---------|:--------:|:------------:|:----:|
| `PipelineBlend` | Porter-Duff 模式 | 是（subpass） | N（每个输入一次） | 否 |
| `AdvancedBlend<T>` | 高级模式 + 无 FB fetch | 是（subpass） | 1 | 否 |
| `CreateFramebufferAdvancedBlend` | 高级模式 + 支持 FB fetch | 是（subpass） | 2 | 是 |
| `CreateForeground*Blend` | 单输入 + 前景色 + 可吸收不透明度 | 否（内联） | 1 | 继承 |

**PipelineBlend 的多输入顺序混合**：

```
MakeSubpass("Pipeline Blend Filter", subpass_size)
│
├─ Draw dst (input[0]) with BlendMode::kSrc        // 覆写写入
├─ Draw src (input[1]) with selected blend_mode     // 第一次混合
├─ Draw src (input[2]) with selected blend_mode     // 第二次混合
│   ...
└─ (可选) Draw foreground_color with blend_mode     // 前景色混合
```

**AdvancedBlend 的离屏双纹理混合**：

```
MakeSubpass("Advanced Blend Filter", subpass_size)
│
├─ Snapshot dst → dst_texture
├─ Snapshot src → src_texture
├─ 单次 draw：advanced_blend pipeline
│   ├─ FS::BindTextureSamplerDst(dst_texture)
│   ├─ FS::BindTextureSamplerSrc(src_texture)
│   └─ FS::BindBlendInfo({dst_input_alpha, src_input_alpha, color_factor, color})
```

**Framebuffer Blend 的 FB fetch 路径**（`framebuffer_blend.frag`）：

```glsl
// 使用 Vulkan subpassInputMS 读取当前帧缓冲（4x MSAA 手动 resolve）
layout(input_attachment_index = 0) uniform subpassInputMS uSub;

vec4 ReadDestination() {
    return (subpassLoad(uSub, 0) + subpassLoad(uSub, 1) +
            subpassLoad(uSub, 2) + subpassLoad(uSub, 3)) / vec4(4.0);
}
```

注意 Impeller 的 FB fetch 在 Vulkan 上使用 `subpassInputMS` 实现，需要**手动 resolve 4 个 MSAA 样本**（简单平均）。这是因为 Vulkan 的 subpass input 访问的是未 resolve 的 MSAA 纹理。

**Advanced Blend 的离屏 dst 采样路径**（`advanced_blend.frag`）：

源颜色支持两种输入方式，通过 `color_factor` uniform 切换：

| `color_factor` 值 | 源 | 说明 |
|:---:|---|---|
| `> 0.0` | `blend_info.color`（uniform 颜色） | 前景色混合，无需 src 纹理 |
| `== 0.0` | `texture_sampler_src`（纹理采样） | 纹理间混合 |

两种 shader 都在**非预乘色彩空间**执行混合运算（先 unpremultiply src 和 dst），混合结果通过 `IPApplyBlendedColor()` 处理 alpha 合成并重新预乘。

### 7.4 ImageFilter（模糊、形态学等）

ImageFilter 始终作为独立的 filter Pass 处理。

#### Gaussian Blur 的实现细节

`gaussian.frag` 是一个**单方向可分离模糊 Pass**，完整的 2D 模糊需要执行两次（水平 + 垂直）：

```glsl
// 内核数据通过 uniform buffer 传入（最多 50 个采样点）
uniform KernelSamples {
    vec4 sample_data[50];  // .xy = UV 偏移, .z = 高斯系数, .w = padding
};
uniform FragInfo {
    float sample_count;    // 实际采样数（运行时动态设定）
    float unpremultiply;   // 最终 Pass 是否需要反预乘
};

void main() {
    f16vec4 total_color = f16vec4(0.0hf);
    for (int i = 0; i < int(frag_info.sample_count); i++) {
        float16_t coefficient = float16_t(kernel_samples.sample_data[i].z);
        vec2 offset = kernel_samples.sample_data[i].xy;
        total_color += coefficient * Sample(texture_sampler,
                                            v_texture_coords + offset);
    }
    frag_color = total_color;
}
```

**设计特点**：
- CPU 侧预计算高斯核权重和偏移，可利用**双线性采样优化**（在两个 texel 之间采样，一次获取加权混合）
- 全链路使用 **FP16 半精度**，移动 GPU 上性能显著优于 FP32
- Decal 模式通过 specialization constant 控制，边界外像素贡献为零

#### Morphology Filter 的两 Pass 分解

2D 形态学滤镜（膨胀/腐蚀）通过 `MakeMorphology()` 工厂方法分解为两个 `DirectionalMorphologyFilterContents`：

```cpp
auto filter_x = std::make_shared<DirectionalMorphologyFilterContents>();
filter_x->SetDirection(Vector2(1, 0));  // X 方向
filter_x->SetRadius(radius_x);

auto filter_y = std::make_shared<DirectionalMorphologyFilterContents>();
filter_y->SetDirection(Vector2(0, 1));  // Y 方向
filter_y->SetRadius(radius_y);
filter_y->SetInputs({FilterInput::Make(filter_x)});  // X 的输出作为 Y 的输入
```

这是 Impeller 中**滤镜链**的典型模式——通过 `FilterInput::Make()` 将前一个滤镜的输出包装为下一个滤镜的输入，形成 DAG。

### 7.5 滤镜链的覆盖区域传播

滤镜链中的覆盖区域（coverage）在两个方向传播：

| 方向 | 方法 | 作用 |
|------|------|------|
| 向前（渲染方向） | `GetCoverage()` / `GetFilterCoverage()` | 计算滤镜输出的边界（如模糊扩大边界） |
| 向后（源方向） | `GetSourceCoverage()` / `GetFilterSourceCoverage()` | 给定所需输出区域，计算需要的输入区域 |

**向后传播的意义**：配合 `coverage_hint`（通常来自 clip 边界），可以将中间 texture 的尺寸缩小到仅覆盖可见区域：

```cpp
auto maybe_subpass_coverage = subpass_coverage.Intersection(*coverage_hint);
```

### 7.6 Uber Shader 的有限使用

Impeller 在两个场景中使用了 uber shader：

#### uber_sdf.frag 源码分析

`uber_sdf.frag` 通过 `FragInfo.type` uniform 切换几何类型：

```glsl
float filledSDF(vec2 p) {
    if (frag_info.type < 0.5)       return distanceFromCircle(p, size.x);
    else if (frag_info.type < 1.5)  return distanceFromRect(p, size);
    else if (frag_info.type < 2.5)  return distanceFromOval(p, size);
    else if (frag_info.type < 3.5)  return distanceFromRoundedRect(p, size, radii);
    else                            return distanceFromRoundedSuperellipse(...);
}
```

**uniform block 是所有几何类型参数的超集**：

| Uniform | 使用方 |
|---------|--------|
| `color`, `center`, `size` | 所有类型 |
| `stroke_width`, `stroke_join`, `stroked` | 描边渲染 |
| `aa_pixels` | 所有类型（控制 SDF 抗锯齿宽度） |
| `type` | 类型分发 |
| `radii` | 圆角矩形（4 角半径）、超椭圆（2 半径） |
| `superellipse_degree`, `superellipse_semi_axis`, ... | 仅超椭圆 |

对于简单几何类型（如圆形），超椭圆专用的 uniform 虽然被上传但不被使用。这是 uber shader 的经典代价——**浪费 uniform 带宽换取更少的 pipeline 切换**。

描边模式通过 `frag_info.stroked` 切换 `filledSDF()` / `strokedSDF()`，矩形描边还支持 miter/bevel/round 三种 join 风格。

抗锯齿通过 `dFdx`/`dFdy` 计算屏幕空间像素尺寸，配合 `aa_pixels` uniform 控制 smoothstep 宽度，实现分辨率无关的解析抗锯齿。

#### 两个 vertices uber shader

`vertices_uber_1.frag` 和 `vertices_uber_2.frag` 分别覆盖高级混合模式的前半和后半（通过 `AdvancedBlendHalf1()` 和 `AdvancedBlendHalf2()`），避免单个 shader 中 15 路分支导致的寄存器压力过大。

**Impeller 使用 uber shader 的共同特征**：
1. **变体空间小**——几何类型 ~5 种，blend mode 分两组各 7-8 种
2. **不涉及纹理采样分支**——SDF 是纯数学计算，blend 分发时两侧纹理已经采样完毕
3. **分支可预测性高**——同一 draw call 内所有 fragment 走相同分支（uniform 驱动，非 per-fragment 数据驱动）

---

## 8. Shader 源码深度分析：渐变系统

### 8.1 三种 Colorizer 策略

Impeller 为每种渐变类型（Linear/Radial/Sweep/Conical）提供三种存储策略的独立 shader，而非通过分支在一个 shader 中切换：

| 策略 | Shader 后缀 | 色标数据存储 | 适用场景 |
|------|------------|------------|---------|
| Texture | `_fill` | 1D 色标纹理 | 大量色标（>16），GPU 纹理插值免费 |
| Uniform | `_uniform_fill` | `vec4 colors[256]` + `vec4 stop_pairs[128]` | 少量色标（2-16），避免纹理分配开销 |
| SSBO | `_ssbo_fill` | Shader Storage Buffer Object | 大量色标，设备支持 SSBO 时的高性能路径 |

#### Texture Colorizer（`linear_gradient_fill.frag`）

```glsl
// 1. 投影到渐变轴
float t = dot(v_position - start_point, start_to_end) * inverse_dot;

// 2. Tile mode 处理（clamp/repeat/mirror/decal）
//    IPSampleLinearWithTileMode() 封装了所有寻址模式

// 3. 直接采样色标纹理
frag_color = IPSampleLinearWithTileMode(texture_sampler, t, ...);

// 4. 预乘 + 全局 alpha
frag_color = IPPremultiply(frag_color) * alpha;
```

**优势**：无分支、无循环，GPU 硬件纹理单元自动处理色标间插值。
**代价**：需要预先生成 1D 色标纹理（CPU 侧），额外的纹理绑定开销。

#### Uniform Colorizer（`linear_gradient_uniform_fill.frag`）

```glsl
// 色标数据通过 uniform 数组传入
uniform vec4 colors[256];
uniform vec4 stop_pairs[128];  // 每个 vec4 打包两个色标（.xy 和 .zw）
uniform float colors_length;

// 循环查找 t 所在的色标区间
for (int i = 0; i < int(colors_length) - 1; i++) {
    // 取出当前色标的 t 值和到下一色标的逆增量
    float stop_t = ...;
    float inverse_delta = ...;

    if (inverse_delta > 1000.0) {
        // 硬色标（两个相邻色标位置相同）
        frag_color = colors[i + 1];
    } else {
        // 线性插值
        float relative_t = (t - stop_t) * inverse_delta;
        frag_color = mix(colors[i], colors[i + 1], clamp(relative_t, 0.0, 1.0));
    }
}
```

**色标数据打包**：`stop_pairs` 每个 `vec4` 存储两个色标的 `(t, inverse_delta)` 对——`.xy` 存偶数索引色标，`.zw` 存奇数索引色标，通过 `bool even` 切换访问。这将 uniform 数组占用量减半。

**优势**：无纹理分配和绑定，对少量色标渐变更高效。
**代价**：GPU 上有循环和条件分支，可能导致 warp 发散；受 uniform buffer 大小限制（256 色标）。

### 8.2 Conical Gradient 的几何变体

锥形渐变是变体最多的渐变类型。除了 3 种 colorizer 策略外，还有 4 种几何变体（`ConicalKind`）：

| Kind | 含义 | 独立 Shader |
|------|------|------------|
| `kConical` | 标准锥形（两圆不同心） | `conical_gradient_fill_conical.frag` |
| `kRadial` | 退化为径向（两圆同心） | `conical_gradient_fill_radial.frag` |
| `kStrip` | 退化为条带（一圆半径为 0） | `conical_gradient_fill_strip.frag` |
| `kStripAndRadial` | 条带 + 径向混合 | `conical_gradient_fill_strip_radial.frag` |

每种 kind × 3 种 colorizer = 12 个锥形渐变 pipeline。Impeller 选择为每种组合编写独立 shader 文件而非使用 `#ifdef` 或分支。

### 8.3 快速渐变路径

`fast_gradient.vert/frag` 是针对 2 色渐变的极致优化路径：

- 两个颜色直接作为 vertex attribute 传入
- fragment shader 仅做一次 `mix()` 插值
- 无 tile mode 处理、无 uniform buffer 查找、无纹理采样
- 适用于最常见的简单渐变场景（如按钮背景、状态栏等）

---

## 9. 架构对比：Impeller vs tgfx 当前架构

### 9.1 结构性差异

| 维度 | Impeller | tgfx（当前 Skia 式架构） |
|------|----------|------------------------|
| **Shader 来源** | 手写 GLSL 4.60，构建期编译 | FragmentProcessor `emitCode()` 运行时拼接 GLSL |
| **效果组合方式** | 每种效果 = 独立 Contents/Filter 类 + 独立 shader，效果叠加 = 多 Pass | FP 树动态嵌套，ProgramBuilder 递归遍历拼接为单 shader |
| **Shader 数量** | ~50 个（固定、有界） | 理论 ~54,000 种组合（动态、无界） |
| **运行时编译** | 无（构建期完成） | 首帧编译 15-70 ms |
| **Uniform 绑定** | 构建期 reflection 自动生成 C++ struct | 运行时 `UniformHandler` 按名称查找 |
| **Pipeline 缓存** | 显式预构建 + 运行时组装 PSO | `ProgramCache` 按 `BytesKey` 查找 |
| **RuntimeEffect** | 支持（独立编译路径） | 支持（独立 `CommandEncoder` 路径） |

### 9.2 效果组合策略的核心差异

**Impeller 的核心洞察**：效果组合不需要在一个 shader 中完成。

在 Skia/tgfx 架构中，`drawRect(paint{shader=texture, colorFilter=colorMatrix, blendMode=Overlay})` 生成一个包含纹理采样 + 颜色矩阵变换 + Overlay 混合的单一 shader。变体空间是三个维度的笛卡尔积。

在 Impeller 中，同一 draw 被分解为：

```
Pass 1: texture_fill → 离屏 texture T1
Pass 2: color_matrix_color_filter → T1 → T2
Pass 3: advanced_blend(Overlay) → T2 + dst → RT
```

三个 Pass 各自使用构建期已编译的固定 shader，变体空间从乘法变为加法。

### 9.3 性能 Trade-off

Impeller 为消除运行时编译付出的代价：

| 代价 | 量化 |
|------|------|
| 多 Pass 的额外 draw call | 效果叠加场景增加 1-3 个 draw call |
| 中间 texture 的带宽与显存 | 每个中间 Pass 需要一次 texture 读写 |
| 引擎二进制体积 | 预编译 shader 增加 ~100 KB（压缩后，单架构） |

Impeller 团队的实测结论：**多 Pass 的带宽代价被消除首帧卡顿的收益远远覆盖**。在 iOS/Android 的 Tile-Based GPU 上，中间 texture 的带宽增加可通过 `loadAction=DontCare` 和尺寸最小化缓解。

### 9.4 二进制体积

Impeller 的 FAQ 中提供了关键数据：

> "Impeller's binary size impact is **~100 KB per architecture** (compressed), including all precompiled shaders."
>
> "Removing Skia GPU (SKSL compilation machinery) **reduces Flutter engine binary size by 17%**."

即预编译 shader 本身的体积远小于 Skia 中运行时编译所需的基础设施（shaderc/spirv-cross 等）。

---

## 10. 对 tgfx 方案的启示

### 10.1 已验证的设计原则

Impeller 在 iOS/Android 生产环境的大规模部署验证了以下设计原则：

1. **原子 shader + 多 Pass 串联是可行的**：Impeller 用 ~50 个预编译 shader 覆盖了 Flutter 的全部 2D 渲染需求，效果叠加通过多 Pass 实现
2. **运行时编译可以完全消除**：包括 RuntimeEffect 在内的所有路径都不依赖运行时 shader 拼接
3. **构建期 reflection 生成 C++ binding 是最佳实践**：消除了手动维护 uniform 偏移量的需要，shader 接口变更产生编译期错误
4. **预编译 shader 的体积开销可忽略**：~100 KB/架构，远小于运行时编译基础设施的体积

### 10.2 tgfx 与 Impeller 的差异

tgfx 在应用 Impeller 策略时需要注意的差异：

| 维度 | Impeller | tgfx |
|------|----------|------|
| **GeometryProcessor 多样性** | ~5 种几何类型 | 12 种 GP，含 SDF 椭圆、实例化渲染等 |
| **渐变变体** | 3 种存储策略（texture/SSBO/uniform）| 4 种 colorizer（texture/unrolled/dual interval/single interval）|
| **公共 API 约束** | Flutter framework 控制渲染流程 | 外部 APP 直接使用 tgfx API |
| **后端覆盖** | Metal/Vulkan/GLES（无 WebGPU） | OpenGL/Vulkan/Metal/WebGPU |
| **Uber Shader 使用** | 有限使用（SDF、drawVertices） | 方案中不采用（附录 D 分析） |

### 10.3 关键参考

1. **Shader 数量规模**：Impeller ~50 个 shader 对应 92 个 pipeline，tgfx 预估 ~151 个原子 shader。考虑到 tgfx 的 GP 多样性（12 种 vs Impeller 的 ~5 种），这一数量级是合理的
2. **变体策略**：Impeller 通过独立 shader 文件处理变体（如渐变的 3 种存储策略各一个 `.frag`），而非通过 `#define` 或 specialization constant。tgfx 可参考此策略
3. **Uniform 绑定**：Impeller 的 reflection 自动生成方案与 tgfx 方案中的"决策点 #6：SPIR-V reflection 自动生成"完全一致
4. **Uber Shader 的使用边界**：Impeller 仅在变体空间小且无纹理采样分支的场景中使用 uber shader（SDF、drawVertices），与 tgfx 附录 D 的结论一致

---

## 11. Entity/Subpass 渲染架构

### 11.1 EntityPass 结构

Impeller 的渲染树由 `EntityPass` 组成，每个 EntityPass 表示一个渲染目标上的一组绘制命令：

```
Root EntityPass (→ onscreen render target)
├── Entity (drawRect)
├── Entity (drawPath)
├── Subpass (saveLayer) → offscreen texture T1
│   ├── Entity (drawImage)
│   └── Entity (drawText)
├── Entity (drawCircle)
└── Subpass (backdrop filter) → offscreen texture T2
    └── Entity (...)
```

EntityPass 内部使用 `std::variant<Entity, SubpassEntry>` 扁平列表存储元素，遍历渲染时：
- `Entity` → 直接记录 draw command 到当前 RenderPass
- `SubpassEntry` → 递归渲染到离屏 texture，再将结果合成回父 Pass

### 11.2 saveLayer 的完整流程

| 步骤 | 操作 |
|:----:|------|
| 1 | `Canvas::SaveLayer()` 推入新的子 `EntityPass` |
| 2 | 后续 draw call 记录到子 Pass |
| 3 | `Canvas::Restore()` 弹出回父 Pass |
| 4 | 渲染时，子 Pass 渲染到离屏 texture |
| 5 | 离屏 texture 通过 `TextureContents` 实体绘回父 Pass |
| 6 | `saveLayer` 的 `Paint`（blend mode、opacity、colorFilter、imageFilter）在**合成步骤**（步骤 5）应用 |

### 11.3 离屏 Texture 管理

- **按需分配**：`MakeSubpass()` 根据子 Pass 的 coverage rect 分配离屏 `RenderTarget`——不是全屏大小，而是最小覆盖区域
- **Texture 池化**：`RenderTargetAllocator` 复用释放的 texture，减少 GPU 内存波动
- **Coverage Hint 裁剪**：与 clip 边界求交，进一步缩小离屏 texture 尺寸
- **翻转缓冲**：Backdrop Filter 需要读取父 Pass 的当前内容，使用两个 texture 交替读写（因为不能同时读写同一 texture）

### 11.4 Inline Pass 优化

近期重构引入了 `InlinePassContext`——当 saveLayer 满足特定条件时（如无 imageFilter、简单 blend mode），可以**折叠**到父 Pass 中执行，避免离屏 texture 分配。这对应 tgfx 中"94.3% 的简单场景"的优化。

### 11.5 MakeSubpass 接口

```cpp
std::shared_ptr<Texture> MakeSubpass(
    const std::string& label,           // 调试标签
    ISize texture_size,                  // 离屏 texture 尺寸
    const std::shared_ptr<CommandBuffer>& command_buffer,
    const SubpassCallback& subpass_callback,  // 渲染回调
    bool msaa_enabled = true,           // 是否启用 MSAA
    bool depth_stencil_enabled = true,  // 是否需要深度/模板
    int32_t mip_count = 1               // Mipmap 级数
);
```

注意 `msaa_enabled` 和 `depth_stencil_enabled` 的默认值——这允许滤镜 Pass 禁用不需要的附件，减少显存占用。例如 `BlendFilterContents` 的 AdvancedBlend 路径创建 subpass 时显式关闭 MSAA 和深度/模板。

---

## 12. 参考资料

1. Flutter Impeller README, https://github.com/flutter/flutter/blob/master/engine/src/flutter/impeller/README.md
2. Flutter Impeller FAQ, https://github.com/flutter-team-archive/engine/blob/main/impeller/docs/faq.md
3. Flutter Impeller API Documentation, https://api.flutter.dev/impeller/
4. Impeller ContentContext::Pipelines, https://api.flutter.dev/impeller/structimpeller_1_1_content_context_1_1_pipelines.html
5. DeepWiki: Impeller Rendering System, https://deepwiki.com/flutter/flutter/4.1-impeller-rendering-system
6. 深入解析 Flutter 下一代渲染引擎 Impeller, 字节跳动终端技术, 2022
7. Flutter 新一代图形渲染器 Impeller, 知乎, 2022
