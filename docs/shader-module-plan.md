# Shader Module 方案：实现设计与执行步骤

状态：已评审修订（2026-11-08：家族级维度裁剪、id 编码、LEAVES 语义、AtlasText 拆分、JIT 术语边界、PerlinNoise 对账；2026-11-09：选择改为 IR 签名查表、writer 只读 IR、新增 P2.0、门禁改为基线清单对照）。基线：`feature/edwardxfshen_chain_spec` @ `dd674fc85`。

进度：P0 `44bba270e`；P1 `2ec10a490`（RRect）；P2 第 1 批 `c8c9c9e6d`（SolidColor、MaskFill、TiledTextureFill）。`c8c9c9e6d` 有 2 个已知 bug 待热修：`MatchTiledTextureFill` 的 values 写成了 `[hasXp,0,0]`（应为 `[1,1,hasXp]`，PRESENT=0 导致 tiled uniform 全部不写）；TiledTextureFill 被误收窄为只收 Rect（应为 Rect、Quad，否则 Quad 的 tiled draw 无主）。
本文是唯一有效的方案文档，取代此前 `docs/` 下全部方案。

已确认的前提：
- JIT 已不存在，AOT 是唯一程序来源（miss 即丢弃 draw）。JIT 特指已删除的"运行时拼装任意 GLSL"路径；GL/HLSL 后端在运行时编译 bundle 内预生成的固定文本（含特化插宏后的文本）不属于 JIT，变体 miss 仍然一律丢弃。
- 声明统一采用"模块组装"形态。
- 发布机制删除：所有构建都在构建期现场生成 bundle，不提交 bundle。
- 特化（spec）必须保留。
- uniform golden（`test/baseline/uniform/`、`AOTUniformABTest`）删除。

---

## 1. 第一性原理

系统只做四件事：

1. 离线：枚举所有可能被请求的程序，为每个后端编译。
2. 运行时：draw → 程序（选择）。
3. 运行时：draw → 参数字节和纹理（写入）。
4. 不变式：写入的接口等于编译出的接口；运行时请求的程序都在离线集合里。

由此推出唯一的设计约束：**接口和变体空间各只写一次，其余全部推导。**

## 2. 概念表（全部，共 10 个）

| 概念 | 定义 |
|---|---|
| `Module` | 一项能力：维度、参数、纹理、varying、顶点属性、两个 GLSL hook、一个运行时 writer |
| `Family` | 一组几何模块 × 一串主体模块。每个（family, geometry）是一个程序模板 |
| `Dim` | 模块的编译期维度：名字 + 取值数 |
| `Variant` | 一个程序模板加全部维度取值。它的整数 id 就是 bundle key |
| `Layout` | 由 Variant 纯函数推导出的 UBO offset、location、binding、spec id |
| `DrawIR` | draw 的唯一描述：几何、颜色图、coverage、xfer、目标 |
| `EffectGraph` | SSA 效果节点（保留） |
| `Plan` | 超预算时切成多个 pass |
| `ChainProgram` | 解释器指令，只属于 Chain 模块 |
| `Bundle` | 变体 id 到各后端二进制 |

以下词从代码中消失：kernel、decl、role、step、selector、contract、permutation、reachable、compose、extract、facts、shape（作为匹配输入）、profile（chain 分档）。

## 3. 声明层（`src/gpu/shaders/`，工具与运行时共享，零运行时依赖）

### 3.1 数据结构

```cpp
// Module.h
enum class ShaderType : uint8_t { Float, Float2, Float3, Float4, Int, Int2, Int3, Int4, Mat3, Mat4 };

struct Element {
  enum class Kind : uint8_t { Attribute, InstanceAttribute, VertexParam, Param, Texture, Varying };
  Kind kind;
  ShaderType type;
  const char* name;            // module-local; emitted as <Module>_<name>
  uint8_t count = 1;           // array length (chain slots, leaves)
  int8_t when = -1;            // index of one of this module's dims; element exists iff value != 0
  bool spec = false;           // Param only: delivered as a specialization constant when active
};

struct Dim {
  const char* name;            // emitted as <Module>_<name>
  uint8_t count;
};

struct Module {
  const char* name;
  const Dim* dims;         uint8_t dimCount;
  const Element* elements; uint8_t elementCount;
  bool hasVertex;              // <Name>.vert.glsl defines void <Name>_vertex(inout Vertex v)
  bool hasFragment;            // <Name>.frag.glsl defines void <Name>_fragment(inout Fragment f)
};

// Family.h
struct FamilyModule {
  const Module* module;
  const int8_t* pinned;        // per-dim of the module: -1 = free (full range), >=0 = fixed value
  constexpr FamilyModule(const Module* m) : module(m), pinned(nullptr) {}
  constexpr FamilyModule(const Module* m, const int8_t* p) : module(m), pinned(p) {}
};

struct Family {
  const char* name;
  const Module* const* geometries; uint8_t geometryCount;   // one program template each
  const FamilyModule* modules;    uint8_t moduleCount;      // body, in order
  bool (*allowed)(const Variant&) = nullptr;                // optional product filter
};

struct Variant {
  uint16_t family;             // index in Families[]
  uint8_t geometry;            // index in family.geometries
  std::array<uint8_t, MaxDims> values;   // flattened over [geometry, modules..., Xfer]; Frame has no dims
  uint32_t id() const;         // family index << 20 | mixed-radix ordinal inside the family
};

std::vector<Variant> EnumerateVariants(const Family& family, uint16_t familyIndex);
```

家族级的维度裁剪用 `pinned` 表达：按模块自己的维度顺序，每个维度给一个约束（-1 = 枚举全量，>=0 = 固定为该值）；不需要裁剪的槽位直接写 `&Module`（隐式转换）。

`MaxDims = 6`（最宽的 Chain 家族：Quad 几何 2 + Chain 2 + DeviceMask 1 + Xfer 1），`MaxModules = 8`（QuadTexture/YUVTexture 的 6 槽是下界，留 2 余量），均以 static_assert 兜底。`id()` 的序号按（geometry、各维度）的实际取值数做混合基数连乘，每家族变体总数 < 2^20 由 static_assert 保证；bundle 与库同一次构建生成，id 没有跨版本稳定的需求。

有两个隐式模块，家族不需要列出：
- `Frame`：永远在最前，提供 `RTAdjust`。
- `Xfer`：永远在最后，负责 porter-duff、dst 纹理/fbf，以及输出 swizzle。

### 3.2 声明写法（纯 constexpr，不用宏）

```cpp
// modules/TiledTexture.h
inline constexpr Dim TiledTextureDims[] = {{"FULL", 2}, {"PRESENT", 2}};   // 4-tap path; module presence
inline constexpr Element TiledTextureElements[] = {
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordMatrix"},
    {Element::Kind::Param, ShaderType::Int2, "ShaderMode"},
    {Element::Kind::Param, ShaderType::Float4, "Subset"},
    {Element::Kind::Param, ShaderType::Float4, "Clamp"},
    {Element::Kind::Param, ShaderType::Float2, "Dimension"},
    {Element::Kind::Param, ShaderType::Int, "ReadSwizzle", 1, /*when FULL*/ 0},
    {Element::Kind::Texture, ShaderType::Float4, "Sampler"},
    {Element::Kind::Varying, ShaderType::Float2, "Coord"},
};
inline constexpr Module TiledTexture = {"TiledTexture", TiledTextureDims, 2,
                                        TiledTextureElements, 8, true, true};

// Families.h  (the only list; its order is the family index)
inline constexpr const Module* RectOnly[] = {&RectGeometry};
inline constexpr int8_t TiledTexturePins[] = {-1, 1};   // FULL free, PRESENT pinned on
inline constexpr FamilyModule TiledBody[] = {{&TiledTexture, TiledTexturePins}, &AnalyticClip};
inline constexpr Family Families[] = {
    {"TiledTextureFill", RectOnly, 1, TiledBody, 2},
    // ...
};
```

"可选模块"不再是一个概念：模块自己声明一个名为 `PRESENT` 的维度并让元素都带 `when`；装配器看到 PRESENT=0 就跳过该模块的 hook 与元素。某个家族要不要保留"缺席"变体，由该家族的 `pinned` 决定——TiledTextureFill 把 PRESENT 钉在 1（纹理必有），RRect 家族不钉（保留纯色圆角矩形变体）。

### 3.3 推导规则（`Layout.h/.cpp`，唯一的编号来源）

```cpp
struct Layout {
  struct Field { uint16_t offset; uint8_t block; uint8_t size; };   // block: 0 vertex, 1 fragment
  std::vector<Field> fields;          // one per (module, element) present in the variant
  std::vector<uint16_t> moduleBase;   // first field index of each module
  uint16_t vertexBlockSize, fragmentBlockSize;
  uint8_t attributeCount, varyingCount, textureCount;
  std::vector<const char*> textureNames;   // GL binds samplers by name
  std::vector<SpecField> specFields;       // id 0 is the master switch
};
Layout DeriveLayout(const Variant& variant);
```

| 编号 | 规则 |
|---|---|
| UBO offset | 按模块顺序、模块内按元素顺序，用 std140 规则排；数组元素步长 16 |
| 顶点属性 location | 按顺序从 0 连续编号，先逐顶点属性，后逐实例属性 |
| varying location | 按顺序连续编号 |
| 纹理 binding | 按顺序连续编号。Xfer 永远在最后，所以 dst 纹理自然排在最后，不需要表达式 |
| spec id | 0 是总开关；之后按顺序，标量占 1 个，ivec4 数组的每个元素占 4 个 |
| 各后端 binding | 逻辑编号经现有的各后端固定映射函数转换（WGSL 的纹理/采样器拆分等），只有一处 |

不存在的元素（`when` 为 0）不占任何编号。这样推导出来的就是常量，不会出现条件 location 表达式。

`Layout` 同时提供 `Slice(moduleIndex)`：`{fields, moduleBase[moduleIndex], 模块元素数}` 的只读视图，即 writer 侧拿到的 `LayoutSlice`；全局 field 下标 = `moduleBase[模块序] + 模块内元素序`。

## 4. GLSL 契约

### 4.1 文件

每个模块对应 `src/gpu/shaders/modules/` 下最多 4 个同名文件：

| 文件 | 内容 | 使用方 |
|---|---|---|
| `<Name>.h` | 声明 | 工具 + 运行时 |
| `<Name>.vert.glsl` | `void <Name>_vertex(inout Vertex v)` | 工具 |
| `<Name>.frag.glsl` | `void <Name>_fragment(inout Fragment f)` | 工具 |
| `<Name>.cpp` | writer | 只有运行时 |

`src/gpu/shaders/glsl/` 只放无接口的纯数学库（operator、coverage 数学、blend、采样、gradient、noise），禁止出现 `uniform`、`in`、`out`、`layout`。

### 4.2 Hook 语义

```glsl
struct Vertex   { vec2 position; vec2 localCoord; };   // geometry writes; others read
struct Fragment { vec4 color; vec4 coverage; };        // geometry seeds; modules transform in order
```

- 几何模块：写 `v.position`、`v.localCoord` 和自己的 varying；在片元里给出 `f.color`（顶点色或 uniform 色）和 `f.coverage`。
- 颜色模块：变换 `f.color`。
- coverage 模块：乘到 `f.coverage` 上。
- Chain：读 `f.color`、`f.coverage` 作为几何输入，产生新的 color 和 coverage。
- Xfer：读 color 和 coverage，写 `fragColor`。

### 4.3 装配器输出（逐变体，全部是常量）

```glsl
#version 450
#define TiledTexture_FULL 0
layout(std140, set = 0, binding = 0) uniform VertexBlock { vec4 Frame_RTAdjust; mat3 RectGeometry_Matrix; mat3 TiledTexture_CoordMatrix; };
layout(std140, set = 0, binding = 1) uniform FragmentBlock { vec4 RectGeometry_Color; ivec2 TiledTexture_ShaderMode; /* ... */ };
layout(location = 0) in vec2 RectGeometry_Coverage;
layout(location = 1) in vec2 TiledTexture_Coord;
layout(set = 1, binding = 0) uniform sampler2D TiledTexture_Sampler;
layout(location = 0) out vec4 fragColor;
#include "modules/RectGeometry.frag.glsl"
#include "modules/TiledTexture.frag.glsl"
#include "modules/AnalyticClip.frag.glsl"
#include "modules/Xfer.frag.glsl"
void main() {
  Fragment f;
  RectGeometry_fragment(f);
  TiledTexture_fragment(f);
  AnalyticClip_fragment(f);
  Xfer_fragment(f);
}
```

顶点侧同构：`VertexBlock`、逐顶点/逐实例属性、各模块的 vert include，main 依次调用 `<Geometry>_vertex(v)` 与各主体模块的 `<Name>_vertex(v)`（Frame 无 hook，只贡献 RTAdjust；几何必须最先执行，其他模块读 `v.localCoord`）。

模块内部用自己的维度宏写 `#if`（例如 `#if TiledTexture_FULL`）。除此之外不允许有其他选择器宏。

### 4.4 特化在 GLSL 中的形态

对 `spec = true` 的字段，装配器会：
- 在 UBO 里照常声明它；
- 生成访问函数 `int Chain_SlotCount()`；
- 在 constant_id 方言下生成 `layout(constant_id = N) const int ...`；
- 在 GL/HLSL 文本方言下生成 `#ifndef TGFX_SPEC_N` 形式的默认值宏。

访问函数内部按总开关决定读常量还是读 uniform。模块代码只调用访问函数，不再需要 pragma 扫描和 raw-read 检查。展开（unroll）由装配器直接生成。

## 5. 运行时

### 5.1 流程

```
record : DrawOp -> DrawIR (lowered once)
         -> chain plan computed once, stored on DrawIR; Plan only if budget overflows
prepare: per pass:
         Match m = MatchDraw(ir)                 // {Variant, sources[moduleCount]}
         SpecValues s = ResolveSpec(m)           // only Chain contributes
         Program p = programs.find(m.variant.id(), s) or create from bundle
execute: for i in modules: Writers[module i](ir, m.sources[i], p.layout.slice(i), &buffer, &textures)
```

多 pass 的每个 pass 只有 GP 和 XP，没有 FP（中间纹理在 IR 里是 Device 叶子，没有对应的 Processor）。所以选择和写入只能以 IR 为输入，普通 draw 和 pass 才能共用一个入口。P4.2 删除 `AOTProgramInfo` 之前，pass 仍走 `PointwiseChainShader::MatchProgram`，不经过家族表。

### 5.2 选择

```cpp
// aot/select/FamilyMatch.cpp: signature lookup, no per-family predicate, no table order
struct Signature {
  GeometryKind geometry;
  ColorShape color;                          // GeometryOnly / Texture(Plain) / Texture(Plain,YUV)+ops /
                                             // Texture(Tiled) / ...; anything else has no signature
  CoverageKind coverage[MaxCoverageTerms];   // Analytic / DeviceMask / MaskTexture / Glyph, in order
  uint8_t coverageCount;
  XferKind xfer;
};
std::optional<Signature> SignatureOf(const DrawIR& ir);   // nullopt = not a fixed-family shape
const std::map<Signature, uint16_t>& SignatureTable();    // built from Families(); duplicate = abort
struct Match { Variant variant; std::array<SourceRef, MaxModules> sources; };
std::optional<Match> MatchDraw(const DrawIR& ir);         // table hit, else Chain, else drop
```

- 输入只有 `DrawIR`，不读 FP 树，不读 ProgramInfo。
- 签名取自 IR，不取自 Processor。lowering 已经做了规范化：同一个 Processor 类会按状态降成不同节点（TextureEffect 的 Plain/YUV/RGBAAA，TiledTextureEffect 的 FULL 0/1，非 AA 的 RectEffect 降成零半径 RRect，Xfermode 的三种子图）；多个 Processor 也会合成一个节点（ClampedGradient 加子 FP 合成 GradientSource，Compose 透明，连续的 Rect/RRect 合成一个 Analytic 项）。所以 Processor 和模块不是一一对应，判别字段在 IR 节点参数里（`samplingKind`、`isYUV`、`hasRGBAAA`、`subset`、节点 kind）。
- 一个签名只对应一个家族，建表时检查，重复即 abort，报出两个家族名。表内没有优先级。
- 维度取值（FULL、STROKE、HAS_SUBSET 等）和值域约束（tile mode 支持集、透视拒绝）由各模块读 IR 参数决定，不进签名。值域外就交给 Chain。
- Chain 不在表里：签名为空或查不到时才由它兜底，接受条件是 IR 能编译。复合树（Compose 嵌套、Xfermode、color filter 链、多叶子）都走这一层；超出 Chain 预算的由 Plan 切 pass；都失败就丢弃 draw 并记账。
- `SourceRef` 是模块数据来源的句柄：EffectGraph 节点、coverage term、几何、xfer。
- alpha-only 只在 `DrawIR.target` 上判断一次。

过渡期（P3.5 删除旧表之前）的查找顺序是：**旧专用表 → 签名表 → Chain**。旧表只删不改，所以这个顺序能保证逐 draw 与迁移前等价；Chain 必须放在签名表后面，否则已删家族的流量会被 Chain 接走。DEBUG 下加一条绊线：旧表命中时签名表也命中，就打印出来，用来暴露迁移期的重叠。

### 5.3 写入

```cpp
// write/ParamBuffer.h
class ParamBuffer {
 public:
  template <typename T> void set(const Layout::Field& field, const T& value);   // memcpy at offset
};

// modules/TiledTexture.cpp
void WriteTiledTexture(const DrawIR& ir, SourceRef source, const LayoutSlice& slice,
                       ParamBuffer* buffer, TextureList* textures) {
  const auto& texture = ir.textureAt(source);
  buffer->set(slice[FieldIndex(TiledTexture, "Subset")], texture.subset);
  // ...
  textures->push(texture.proxy, texture.sampler);
}
```

- writer 只读 `DrawIR` 和 `SourceRef`，禁止读 FP 树（`CoordTransformIter`、`IsProcessor<…>`、`info.getFragmentProcessor` 一律不许用）。writer 需要的参数都由 lowering 记到 IR 上：纹理叶子记录 total coord matrix（含纹理归一化，并已乘上 GP 的 uvMatrix）和 read swizzle；Analytic 项记录 clip 参数（rect/rrect、radii、deviceToLocal、AA、是否设备空间）。
- `FieldIndex` 是 constexpr 查找，名字写错在编译期报错。运行时没有任何按名字的查找。
- 纹理按模块顺序 push，数量必然等于 `layout.textureCount`，不需要 dummy 纹理和启发式。
- 顶点属性：debug 下断言几何的属性名序列等于 layout 中的属性序列（一行断言），取代 115 行的对账逻辑。

### 5.4 特化

```cpp
// The one function both consumers call: prepare (key + injection) and the writer (uniform copy).
ChainStructure ChainStructureOf(const ChainProgram& program);
SpecValues ResolveSpec(const Match& match);   // empty unless the variant has spec fields and spec is on
```

- 程序 key：`(variant id, SpecValues)`。
- 各后端送值方式不变：GL/HLSL 插宏后编译；Vulkan 用 `VkSpecializationInfo`；Metal 用 function constants；WebGPU 用 override。
- `FallbackToGeneric` 保留。
- 总开关从环境变量改成 cache 上的一个 bool，供 `AOTSpecTest` 对比特化和普通两种结果。

### 5.5 Chain 编译（解释器）

| 阶段 | 内容 |
|---|---|
| Schedule | 叶子在前，其余按拓扑序 |
| Encode | 节点转成 slot，每个 op 只存自己的参数，不再是胖联合体 |
| Registers | 按最后一次使用回收寄存器 |
| Bindings | 叶子、LUT、噪声表、padding 到 4 |

- 容量常量只在 Chain 模块声明里出现一次（数组 count），编译器从 Variant 读取：noise 是 32 个 slot，其余 16 个（依据 libpag 的 slot 分布数据）。
- 删除：profile 三档、`AOTChainLayout`、`AOTChainOp` 镜像、`deviceLeaves`/`samplerChildCount`/`hasMaskTexture`/`lutLeafIndex`/`noiseSamplerBase` 这些可推导字段。

## 6. 离线工具

```
for (familyIndex, family) in Families:
  for variant in EnumerateVariants(family):
    layout = DeriveLayout(variant)
    text   = Assemble(variant, layout)                     // the only generator
    for backend in backends(variant):                      // fbf values only where supported
      spirv = Compile(text, dialect(backend))
      require Reflect(spirv) == layout                     // the only interface check
      blobs += Emit(backend, spirv)
WriteBundle(blobs)                                         // Bundle.h, same code the runtime reads
```

Bundle 格式：`header{magic, version}` 加按 id 排序的 `{variantId, vertexBlob, fragmentBlob}`。不存反射、spec 表、manifest、ABI、identity、sourceDigest：这些都能由同一份声明推导，或者因为 bundle 和库在同一次构建里生成而不可能失配。

工具保留的文件：

| 文件 | 内容 |
|---|---|
| `main.cpp` | 入口 |
| `Assembler.cpp` | 装配器 |
| `Compiler.cpp` | shaderc + SPIRV-Cross + tint + metallib/dxc |
| `GLDialect.cpp` | GL 文本方言 |
| `WGSLDialect.cpp` | WGSL 方言 |
| `StageCache.cpp` | 编译缓存 |
| `Report.cpp` | 一种报告 |

## 7. 模块与家族目录（初稿，P2 逐个确认）

### 7.1 模块

| 模块 | 角色 | 维度 | 来源（现有 GLSL） |
|---|---|---|---|
| Frame（隐式） | 引擎 | — | RTAdjust |
| RectGeometry | 几何（DefaultGP） | — | `solid_color_fill.vert`、`plain_texture.vert`、`blur_texture.vert`。coverage 属性无条件存在（非 AA 路径填 1.0），颜色走 `Color` uniform；位置经 `Matrix` 变换 |
| QuadGeometry | 几何（QuadPerEdgeAA） | 当前无（UV、SUBSET 随 QuadTexture 迁移再加） | `quad_*_fill.vert`、chain L0。位置已在设备空间，不经 `Matrix`；颜色走 `Color` uniform |
| MeshGeometry | 几何 | TEX_COORDS | `mesh_fill.vert` |
| EllipseGeometry | 几何 | — | `gp_ellipse_layout.inc`、`ellipse_coverage.inc` |
| ComplexEllipseGeometry | 几何 | STROKE | `gp_complex_ellipse_layout.inc` |
| HairlineLineGeometry | 几何 | — | `gp_hairline_layout.inc` |
| HairlineQuadGeometry | 几何 | — | `hairline_quad.vert` |
| RRectGeometry | 几何（non-AA） | STROKE | `non_aa_rrect_fill.*`、`rrect_coverage.inc` |
| ComplexRRectGeometry | 几何 | STROKE | `complex_non_aa_rrect_fill.vert` |
| RoundStrokeRectGeometry | 几何 | UV_MATRIX | `round_stroke_rect_fill.vert` |
| ShapeInstancedGeometry | 几何（实例化） | — | `shape_instanced_fill.vert` |
| AtlasTextGeometry | 几何 | COVERAGE, COLOR_ATLAS | `atlas_text_fill.*` |
| Texture | 颜色 | SUBSET | `texture_sampling.inc` |
| TiledTexture | 颜色 | FULL, PRESENT | `tiled_sample.inc`、`tiled_full_sample.inc`。`ReadSwizzle` 只在 FULL=1 时存在 |
| YUVTexture | 颜色 | FORMAT | `yuv_texture_fill.frag`。精简定稿：只做裸三平面采样与 YUV→RGB 换算；原 frag 内嵌的 chain 指令尾巴（SlotCount + pointwise_chain_uniforms）随家族裁掉，不迁移 |
| OpTail | 颜色（≤3 个一元 op） | COUNT | `operator/` |
| MaskTexture | coverage | — | `mask_fill` |
| DeviceMask | coverage | PRESENT | `FILL_DEVICE_MASK` |
| LocalMask | coverage | PRESENT | `FILL_LOCAL_MASK` |
| AnalyticClip | coverage | — | `clip_coverage.inc` |
| Chain | 颜色 + coverage | LEAVES(0/1), RESOURCE(None,Lut,Noise) | `pipeline/chain/*`，spec 字段 5 个 |
| GaussianBlur1D | 邻域 | — | `gaussian_blur_1d.frag` |
| GlassTentBlur | 邻域 | — | `glass_udf_tent_blur.frag` |
| GlassRefraction | 邻域 | DISPERSION, SHAPE | `glass_refraction.frag` |
| Xfer（隐式） | 输出 | XP{None,Dst,Fbf} | `xp/*`、`output_swizzle.inc`。`OutputAlphaSwizzle` 无条件存在（HAS_XP=0 也要写输出） |

Chain 的叶子是定长 4 槽数组：元素 `count = 4`、`when = LEAVES`，LEAVES 取 0/1（0 = 无叶子，1 = 4 槽数组；libpag 叶子上限为 4，对应现有 `TransformedCoords_0..3`）。slot 容量（noise 32、其余 16）是 RESOURCE 各取值下 slot 数组自己的 `count`，见 5.5。

### 7.2 家族

| 家族 | 几何 | 主体模块 | 备注 |
|---|---|---|---|
| SolidColor | Rect | AnalyticClip | 已迁移（`c8c9c9e6d`） |
| MaskFill | Rect | MaskTexture | 已迁移。旧家族只接受单个 fragment，没有 clip，按旧能力不带 AnalyticClip |
| TiledTextureFill | Rect, Quad | TiledTexture（钉 FULL=1、PRESENT=1） | 已迁移。旧家族接受 `positionOnlyRect`，包含 Quad；只接受单个 fragment，不带 AnalyticClip |
| QuadColor | Quad | DeviceMask | 已迁移 |
| QuadTexture | Quad | Texture, LocalMask, DeviceMaskRuntime, AnalyticClip | libpag 命中最高 |
| YUVTexture | Quad | YUVTexture（裸源，无 OpTail） | 精简定稿（2026-11-09）：裁掉内嵌指令尾巴，ChainTail 模块取消。依据：libpag 对 YUV 的全部三条路径（Android 硬解 SurfaceTexture、Android 软解 libyuv 转 RGBA、其他平台软解）均为裸绘制，滤镜全部走快照两段式（`DropShadowFilter.cpp:80` 模式），YUV 家族永远只见到 colorFPCount==1；单测零覆盖（全 test/src 无 YUV×filter/blend/matrix 组合）；超容量（ops > MaxPointwiseSlots）今天即被 `CollectYUVTail` 拒绝并级联丢弃（chain 的 CollectTextureTail 不查 isYUV，但 YUV 视图无法绑定为单采样器，绑定阶段失败）。配套：新增 YUV+算子 draw 的平面化回退（先转 RGBA 再走正常路径，与 libpag Android 的 libyuv 转换同构），约 30–50 行；无此回退则裁尾会把"能画"变"丢弃" |
| Mesh | Mesh | AnalyticClip | |
| Ellipse | Ellipse | DeviceMask, AnalyticClip | |
| ComplexEllipse | ComplexEllipse | AnalyticClip | |
| RRect | RRect | TiledTexture(不钉 PRESENT), AnalyticClip | 原 NonAARRect；纯色圆角矩形即 PRESENT=0 变体 |
| ComplexRRect | ComplexRRect | AnalyticClip | |
| RoundStrokeRect | RoundStrokeRect | AnalyticClip | |
| HairlineLine / HairlineQuad | 各自几何 | AnalyticClip | |
| ShapeInstanced | ShapeInstanced | — | |
| AtlasTextClip | AtlasText | AnalyticClip | 原 AtlasText 的两种 coverage 拆为两个家族（模块序列固定，不能写"/"） |
| AtlasTextMask | AtlasText | DeviceMask | 同上 |
| GaussianBlur1D | Rect | GaussianBlur1D | |
| GlassTentBlur | Rect | GlassTentBlur | |
| GlassRefraction | Ellipse, Quad | GlassRefraction | 双几何，删除 `glass_refraction.vert` |
| Chain | Rect, Quad, Mesh, AtlasText, Ellipse, HairlineLine, ComplexEllipse, ShapeInstanced, RoundStrokeRect | Chain, DeviceMask | 几何列表按需扩展 |

删除的家族：
- TexturedEffect：与解释器完全重叠，libpag 命中 0。
- ShapeInstancedTextureCoverage：改由 `Chain` 加 ShapeInstanced 几何服务，命中 3。它的 GRADIENT 维度由 Chain 的 LUT 承担。
- PerlinNoise：已在基线之前的提交删除，噪声由 Chain 的 RESOURCE=Noise（OP_PERLIN_NOISE，共享 `perlin_noise_math.inc`）承担；libpag hits 数据里的 `PerlinNoiseFillShader`（命中 0）是历史 bundle 的记录，不是现存家族。

Chain 新增 ShapeInstanced 和 RoundStrokeRect 两种几何，正好覆盖 libpag 记录到的 NoMatchingRule 形状。ComplexEllipse 加 RRectEffect / DeviceSpaceTexture 的 58 次，通过 ComplexEllipse 进 Chain 解决。

## 8. 执行步骤

### 8.0 每个 commit 的门禁

```bash
./codeformat.sh 2>/dev/null; true
cmake -G Ninja -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -B cmake-build-debug
cmake --build cmake-build-debug --target TGFXFullTest_OpenGL ShaderBuildToolTest
./cmake-build-debug/TGFXFullTest_OpenGL
./cmake-build-debug/ShaderBuildToolTest
```

- 截图：失败清单与 P0 基线逐条一致（本机 GL 24 条、Metal 14 条，清单在 `.codebuddy/`）。出现新增失败就停下汇报，不修改 `version.json`。
- 判断以测试进程的退出码和失败清单 diff 为准，不能看经过 `| tail`、`| grep` 过滤后的输出：P0 曾因管道吃掉退出码，把 24 条既有失败误判成全绿。
- 已知缺口（`AOTKnownGaps`，当前 6 条）只减不增。
- 每完成一个阶段，额外跑一次 Metal 全量测试和 libpag 性能对比（与 `.codebuddy/m0_perf_baseline_gl.log` 对照）。

### P0 基线与清理（约 1 天）

| commit | 内容 |
|---|---|
| P0.1 | 跑全量测试，记录当前通过和失败的清单（不提交，写入 `.codebuddy/`） |
| P0.2 | 删除发布机制：`resources/shaders/*`、`publish_bundles.sh`、`verify_published_bundles.sh`、`test_bundle_freshness.sh`、`PublishedBundleCheck.cmake`、`TGFX_WEB_PREBUILT_BUNDLES`/`TGFX_SHADER_BUNDLES_PREBUILT` 分支、CI 的两个步骤、`tgfx_publish_shader_bundles` target；Web 构建强制开启 WebGPU bundle |
| P0.3 | 删除 uniform golden：`test/baseline/uniform/`、`test/src/AOTUniformABTest.cpp`、`test/src/utils/UniformABCompare.h/.cpp` |
| P0.4 | 删除 JIT 残留：`ShaderCaps` 的 4 个死字段、`UsesRuntimeFrameBufferFetch`、`nameSuffix`/`skipSuffix`/`getMangledSuffix`/`updateUniformDataSuffix`、`programKeyForDiagnostics`、CMake 中的死 glob、测试里的 JIT 措辞和 `runtimeCompiles` 指标、过时注释 |

### P1 新骨架加首个家族（约 3 天）

| commit | 内容 |
|---|---|
| P1.1 | `Module.h`、`Family.h`、`Layout.h/.cpp`（std140 从 `UniformData.cpp` 移入），以及 `LayoutTest` |
| P1.2 | 工具新增 `Assembler.cpp`，`Families.h` 进入构建；bundle 同时容纳旧 key 和新 variant id（过渡，P3 结束时删除） |
| P1.3 | 运行时新增 `FamilyMatch`、`ParamBuffer`、writer 循环、按 `Layout` 建 Program；查找顺序为旧专用表 → 声明表 → Chain（过渡，P3 结束时删除旧表；理由见 5.2） |
| P1.4 | 首个家族 `RRect`（原 NonAARRect）：模块 RRectGeometry、TiledTexture、AnalyticClip、Frame、Xfer；同一个 commit 删除它的旧 `.h`/`.cpp`/`.frag`/`.vert` |

P1 结束时要确认三件事：
- 反射等于推导布局，在 GL、Vulkan、Metal、WGSL、HLSL 上都成立。
- D3D cbuffer 的打包规则：如有差异，在 HLSL 后端强制 std140 兼容，不在运行时补偿。
- 首个家族的截图零差异。

### P2 固定家族逐个迁移（约 6 天）

**P2.0（在继续迁移之前做）**：把 P4.1 的一部分提前，否则每迁一个家族都会复制一遍读 FP 树的写法。
- `DrawIR` 带上几何描述、xfer、target（alpha-only）。
- 纹理叶子记录 total coord matrix（含归一化，并已乘上 GP 的 uvMatrix）和 read swizzle；Analytic 项记录 clip 参数。
- `FamilyMatch` 的手写谓词改成 `SignatureOf(DrawIR)` 加签名表查找（5.2）。
- `DeclaredFamilyWriter` 改成只读 IR（5.3），删掉其中的 `CoordTransformIter` 和 `IsProcessor<…>`。
- 门禁：已迁移的 4 个家族截图与基线一致。

之后每个家族一个 commit，内容是：模块声明 + hook GLSL + 签名表一行（模块需要值域约束时再加 `accepts`），同一个 commit 删除它的旧产物。writer 不按家族写，由通用的模块分发 writer 处理，新模块只加自己的写函数。每个家族动手前先对照现状 hitPairs 做一次能力矩阵核对，确认新家族覆盖旧家族的全部命中组合（QuadColor 优先：现表无 AnalyticClip，需确认不丢 coverage 能力）。

每个家族的额外门禁：签名唯一（建表时检查）；DEBUG 绊线没有新增的旧表/签名表重叠。顺序按几何复用：

1. Rect 系：SolidColor、MaskFill、TiledTextureFill、GaussianBlur1D、GlassTentBlur
2. Quad 系：QuadColor、QuadTexture、YUVTexture
3. Ellipse 系：Ellipse、ComplexEllipse、GlassRefraction
4. 其余：Mesh、ComplexRRect、RoundStrokeRect、HairlineLine、HairlineQuad、ShapeInstanced、AtlasText

TexturedEffect 和 ShapeInstancedTextureCoverage 不迁移，P3 里直接删除。

### P3 解释器迁移与旧系统删除（约 5 天）

| commit | 内容 |
|---|---|
| P3.1 | Chain 模块：hook GLSL 由 `pipeline/chain/*` 改写；5 个 spec 字段声明为 `spec = true`；装配器生成访问函数和展开 |
| P3.2 | Chain 编译收敛到 4 个阶段，ChainProgram 精简，容量常量收成一处 |
| P3.3 | Chain 的几何列表扩展到第 7.2 节的范围；删除 TexturedEffect 和 ShapeInstancedTextureCoverage；更新 `AOTKnownGaps` |
| P3.4 | 特化改为基于声明：`ResolveSpec` 和 `ChainStructureOf`；删除 `SpecCapture`、`SpecResolver`、`SpecSource`、scratch 缓存、gate 框架、spec pragma 扫描、`TGFX_AOT_SPECIALIZE` |
| P3.5 | 删除旧系统，包括：`ShaderFamily.h`、`FillKernel.h`、`KernelContract.h`、`ShaderPermutation.*`、`PrecompiledShader.*`、`ProgramWriterFactory.*`、`kernels/`、`decls/`、旧的 `modules/*Module.h`/`*Params.h`/`ParamWriter`/`ParamHandle`、`contract/DrawFacts.h`、`KernelTable.*`、`PermutationMatcher.cpp`、`PermutationMatch.h`、`DrawShape.cpp`、`ShaderMatchHelpers.h`、`aot/bind/`（Registry、Catalog、Layout、Binder 中按名字写的部分）、`glsl/pipeline/`、`glsl/kernels/`、`glsl/level1/`、`StageEmitter.*`、bundle 反射及其解析、`UniformData` 的 Field 表和按名写入、4 个 ProgramWriter 合并为 1 个、属性对账、sampler 启发式；新旧 key 的过渡层 |

### P4 运行时只认 IR（约 4 天）

| commit | 内容 |
|---|---|
| P4.1 | `DrawIR` 纳入剩余的几何参数；sampler 从 IR 获取（照 `ChainProgramWriter` 用 `program->bindings` 的做法）；`programKey` 改成 IR 键（variant id + spec + GP/XP key），不再哈希 FP。几何描述、xfer、target 和 writer 所需参数已在 P2.0 完成 |
| P4.2 | 每个 draw 只规划一次，结果存在 `DrawIR` 上；合并 `routeDraw`/`routeFill` 的重复部分；删除 `AOTProgramInfo` 这个第二入口 |
| P4.3 | 删除：`colorFPCount`/`coverageFPCount` 守卫、`AssertSingleLeafFacts`、`writeProcessorsByTree`、FP/XP 的 `setData`、`decomposition` 开关、3 个 dump 环境变量、`injectedPassFailure`、`Analyze` 等诊断路径、重复的 `Validate`、`AOTKernelMatch`、`XferKind` 映射副本、`AOTTextureTailShape`/`AOTYUVTailShape` 合一、`AOTTextureParameters` 的可推导字段 |

`dedicatedKernels` 开关保留，改名为 `fixedFamiliesEnabled`，用于"固定家族 vs Chain"的等价测试。

### P5 工具瘦身（约 3 天）

| commit | 内容 |
|---|---|
| P5.1 | bundle 读写共用 `Bundle.h`；删除 `BundleVerifier`、`VerifyCommand`、manifest、ABI/identity/sourceDigest |
| P5.2 | 校验只保留"反射等于推导布局"这一项；删除 `ContractRules` 注册表、Source/Variant 的各项检查、`BindingLayoutRule`、`InlineExpansion`、多余的诊断码；4 种报告并成 1 种 |
| P5.3 | 试验 GL 改走 SPIRV-Cross：像素一致就删除 `GLSLDialect` 的手写改写，否则只删 leak 扫描。过渡期 GLDialect 输出前做一次文本级 `#include` 展开（驱动不支持 include），改走 SPIRV-Cross 后自然删除 |
| P5.4 | `test/shader_tool` 只保留 Assembler、Layout、Compile、StageCache 的测试 |

### P6 删除 FP/GP/XP 处理器层（约 8 天，开始前单独确认）

- 效果的生产方（41 处构造点）直接构建 EffectGraph。
- GP 只保留"顶点数据生成"，并入 VertexProvider 一侧。
- XP 只保留 blend 参数，并入 `DrawIR`。
- 删除 `src/gpu/processors/` 和 `src/gpu/glsl/processors/`，约 9,900 行。
- 开始前先给出逐文件映射，请你确认。

### P7 用数据精简家族（约 2 天）

- 用 `fixedFamiliesEnabled=false` 跑等价测试，确认 Chain 能服务哪些固定家族。
- 结合 libpag 命中数据（`.codebuddy/data/`）和性能对比，逐个决定去留。

## 9. 规模预估

| 区域 | 现在 | 目标 |
|---|---|---|
| `src/gpu/aot` + `src/gpu/shaders`（C++） | 约 20.4k | 约 8k |
| 工具 + 工具测试 | 约 15.5k | 约 5k |
| GLSL | 约 5.1k | 约 3.5k |
| processors（P6） | 约 9.9k | 约 1.5k |
| 合计 | 约 51k | 约 18k |
| 概念 | 约 40 | 10 |
| 新增一个家族要改的地方 | 11 处 | `Families.h` 约 3 行 + 签名表 1 行（新模块另加 hook GLSL 和写函数） |

## 10. 风险

| 风险 | 应对 |
|---|---|
| hook 化重排浮点运算顺序，导致 GL 像素出现 1 LSB 差异 | hook 内部照搬现有表达式；如有差异，停下汇报，由你决定是否接受基线 |
| GL/HLSL 每个新 (variant, SpecValues) 首次命中要在运行时编译固定文本 | 与现状一致，不回退；进程内按程序 key 缓存。不做 glProgramBinary 持久缓存（WebGL 不支持、bundle 文本随构建变化会失效）；若 profile 显示启动卡顿再评估 |
| Chain 几何扩展让 bundle 变大 | P3.3 实测每个几何增加的字节；超出预期就缩减几何列表 |
| D3D cbuffer 打包与 std140 不同 | P1 验证，在 HLSL 后端统一 |
| P1–P3 期间新旧两套并存 | 过渡层只有两处（bundle key 和 MatchDraw 的回退），都在 P3.5 删除。查找顺序固定为旧专用表 → 签名表 → Chain，保证逐 draw 等价 |
| IR 缺少 writer 需要的参数 | P2.0 补齐（coord matrix、clip 参数、read swizzle、target）；补齐前 writer 不得读 FP 树 |
| Processor 和模块不是一一对应 | 签名只从 IR 取（5.2）；判别字段在 IR 节点参数里 |
| Web 构建需要 tint | 已确认接受 |
| Metal 本机能否跑 | P0.1 确认；不能跑就记为已知验证缺口 |
