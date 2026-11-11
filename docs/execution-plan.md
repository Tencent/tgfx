# 声明式模块迁移——执行计划

- **日期**：2026-11-10
- **基线提交**：`31bcedd1d`（GlassRefraction 迁移完成，P2 固定家族全部迁毕；GL 全量 24 条截图失败与 `a2e238260` 时逐条一致，coverage gate `noMatchingRule=0 droppedDraws=0 precompiledPrograms=2193`）
- **性质**：本文件是后续执行的唯一依据。与 `docs/shader-module-plan.md` 冲突之处以本文件为准；该文档由用户自管，不随代码提交。
- **工作约定**：每个提交独立可回退、过门禁；出现新增失败立即停下汇报；`version.json` 不由本任务修改。

---

## 一、已确认的修订决定

1. **取消签名表**（原 P2.0 的 `SignatureOf(DrawIR)` + 查表）。它要解决的两个问题分开用小机制解决：变体编号错误 → `EncodeVariant`/`DecodeVariant`（1a）；家族重叠 → DEBUG 互斥绊线（1b）。
   理由：21 个固定家族已全部迁完，“每迁一个家族复制一份谓词”的债已停止增长；签名词汇要建在 `DrawShape` 上，而 `DrawShape` 第 5 步就要删。
2. **“writer 只读 IR”挪到第 5 步**（原 P4 位置），在 `DrawPlan` 最终结构上一次做完，不往要删的结构上投入。
3. **撤回 2026-11-09 的“YUV 精简定稿”**（裁指令尾 + 平面化回退），改为 **YUV 挂 Chain 尾**：算子代码与 uniform 声明进 shader，“跑哪些算子、参数是多少”作为 uniform 值每次上传，裸 YUV `SlotCount=0`。
   理由：广色域目标上引擎自动追加 `ColorSpaceXform`（`OpsCompositor.cpp:595-601`），“libpag 的 YUV 全是裸绘制”的裁尾依据不成立；旧内核 `yuv_texture_fill.frag` 的尾本来就是 chain 解释器（直接 include `pointwise_chain_uniforms.inc` / `pointwise_slot_loop.inc`），挂尾是还原而非新概念。
   变体账：`YUVTexture + Chain(LEAVES=0, RESOURCE=None, MASK=0, CAP=3 槽)` 替换旧内核 12 个与现有裸家族 12 个，总数不变且少一份重复。
4. **平面化回退取消**。chain 拒绝的形状集合与旧内核相同，无新增丢弃。
5. **glass 的 3 条 known gap 不动**：其 coverage 是 `Xfermode(SrcIn)` 套 **tiled** 采样纹理，`IsLocalMaskTexture` 要求 Plain——它不是 device mask，不能用零维模块收掉；去留第 7 步用数据定。
6. **勘误**：P0 基线“Metal bundle 无法构建”是 `dd674fc85` 时的旧状态。chain `NTEX=0 + mask + dst` 双占 texture(0) 的冲突当前已修复（`ModuleDeclTest.cpp:177-180` 断言保护）。Metal 全量测试最后一次运行停在 2026-11-06（`/tmp/a5_metal.log`），声明式家族迁移（11-09 起）从未跑过 Metal，第 0 步补。

## 二、已确认的开工点

1. Chain 先 1:1 移植为独立 `ChainGeometry` 模块（`GP_LAYOUT(5)`、`HAS_COVERAGE`、`HAS_UV_COORD`、`HAS_SUBSET` 四个维度），与固定家族几何模块的去重留到第 7 步。
2. `whenValue` 泛化为 `whenMask`（取值位掩码；机械迁移：`whenValue=k` → `whenMask=1u<<k`，“非 0 即存在” → 全掩码去掉第 0 位）；槽容量用 `CAP(3)` 维度承载（0=16 槽、1=32 槽、2=尾 3 槽），YUV 家族 pin CAP=2，Chain 家族 `allowed` 排除 CAP=2 并绑定 RESOURCE（Noise→32 槽，其余→16 槽）；`MaxDims` 6→10（static_assert 兜底）。
3. 2b 用临时开关 `declaredChainEnabled`（默认关）做新旧 chain 等价对照；2c 删除开关。
4. Chain 内嵌 device mask 第 2 步 1:1 保留，拆 `DeviceMask` 模块留到第 7 步。
5. 第 4 步会增加变体，需单独批准；新截图用例 `ImageRenderTest.YUVImageToDisplayP3`（I420 → Display P3 目标，覆盖“YUV + ColorSpaceXform”路径）的基线由用户执行 `/accept-baseline` 接受。

---

## 三、步骤总览

| 步骤 | 内容 | 提交 | 估时 |
|---|---|---|---|
| 0 | 补跑 Metal，建立 Metal 失败清单基线 | 0 | 0.5 天 |
| 1 | 安全网：EncodeVariant（1a）、互斥绊线（1b）、写入路径（1c） | 3 | 1.5 天 |
| 2 | Chain 模块化 + 声明式特化 + 收编 YUV + 删旧表（2a–2d） | 4 | 5 天 |
| 3 | Chain 编译器收敛 4 阶段、容量常量收成一处 | 1–2 | 2 天 |
| 3a 阶段合并 | 完成 | `0bb38f087` | MET：AOT 套件 125 项（含编译快照）全过；24 条一致；gate MET 2194；工具 211/211 | 8 阶段 → Schedule/Encode/Registers/Bindings 四个入口，子步骤转文件内 static（内部头 8 声明 → 4）；`AOTChainAllocate.cpp` 拆分：形状/覆盖根/分析路由搬入 Encode，收尾步骤搬到驱动旁，剩余改名 `AOTChainRegisters.cpp` |
| 3b 容量单一来源 | 完成 | `251527dc6` | MET：`.bin` 逐字节不变（`03e90f1b…`）；摘要一次性变化（`0x94e2…`→`0x0559…`）；24 条一致；gate MET 2194；AOT 73 项全过；工具 211/211 | 容量宏改由**工具从声明读取**（`ChainSlotPackedCounts[0]/[6]`，新增 `core/CapacityDefines.h`）；合约头保留规划预算 + `ChainInputs.cpp` 的 `static_assert` 钉住“预算==声明长度”（分层规则禁止合约头包含声明层，故不能照计划直接读 count）；YUV 尾部容量归 `YUVChainTailSlotCapacity`，删 `MaxPointwiseSlots`/`TGFX_TAIL_SLOT_CAPACITY` 与死文件 `pointwise_chain_uniforms.inc`；`chainResults[16]` 改注入宏。**实测否决一条路**：GLSL `ChainData.length()` 会改变 SPIR-V → `.bin` 变化，按字节门禁回退 |
| 4 | Chain 新增 ShapeInstanced / RoundStrokeRect 几何 | 1 | 1 天 |
| 5 | prepare 只规划一次、writer 只读 IR、programKey 改 IR 键 | 3 | 4 天 |
| 6 | 工具瘦身（6a–6d） | 4 | 3 天 |
| 7 | 数据决定家族去留 | 按需 | 2 天 |
| 8 | 删处理器层（可选，先给逐文件映射） | — | — |

---

## 四、通用门禁

每个提交：

```
./codeformat.sh 2>/dev/null; true
cmake -G Ninja -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -B cmake-build-debug
cmake --build cmake-build-debug --target TGFXFullTest_OpenGL ShaderBuildToolTest
./cmake-build-debug/TGFXFullTest_OpenGL > <log>
./cmake-build-debug/ShaderBuildToolTest
```

- **截图**：GL 失败清单与上一提交**逐条相同**（当前 24 条，基线存 `.codebuddy/gates/gl_fails_base.txt`）。清单从完整日志提取（`grep "\[  FAILED  \]"` 后清洗排序），不得用 `| tail` 截断的输出判断。
- **Coverage gate**：`noMatchingRule=0`、`droppedDraws=0`、`staleKnownGaps=0`；known gaps 只减不增；`precompiledPrograms` 的变化必须可解释（变体守恒的步骤必须不变，当前 2193）。
- **审计**：`AOTL2AuditTest.ServedShapesMatchPlainPathWithinOneLSB` 通过。
- **阶段收尾**（第 1/2/3/5 步）：加跑 Metal 全量（对照 `.codebuddy/gates/step0_metal_fails.txt`）+ libpag 性能矩阵（对照 `.codebuddy/m0_perf_baseline_gl.log`）。
- 出现新增失败立即停下汇报。

---

## 五、各步骤详情

### 第 0 步：补跑 Metal（不提交）

- `cmake --build cmake-build-metal --target TGFXFullTest_Metal`（bundle 随构建再生，含 glass 两家族的首次 Metal 编译），构建日志 `/tmp/step0_metal_build.log`。
- 运行 `./cmake-build-metal/TGFXFullTest_Metal`，日志 `/tmp/step0_metal.log`。
- 提取失败清单存 `.codebuddy/gates/step0_metal_fails.txt`；同时把当前 GL 清单（取自 `/tmp/gl_full.log`）存 `.codebuddy/gates/gl_fails_base.txt`。
- 对照两处：`docs/shader-module-plan.md:433` 记录的“Metal 14 条”，以及 `/tmp/a5_metal.log`（2026-11-06，声明式迁移开始前的最后一次 Metal 全量）。差异逐条归因：已知截图基线差异（GL 24 条的 Metal 对应）/ 迁移引入（**必须停下**）/ Metal 既有。
- 结果只记录，作为后续每阶段 Metal 门禁基线。

### 第 1 步：安全网（3 个提交）

#### 1a. EncodeVariant / DecodeVariant / FamilyIndexOf

**问题**：`FamilyMatch.cpp` 有 19 处手写 `variant->family = N`、19 处手算 ordinal；`c8c9c9e6d` 里 TiledTextureFill 把 values 写成 `[hasXp,0,0]`（应为 `[1,1,hasXp]`）就是这一类错误，编译器查不出。

**接口**（`Family.h`）：

```cpp
/// 自由维度按展平声明序（geometry → body → Xfer，pinned 维跳过）组 variant。
/// pinned 值由声明补齐，ordinal 用与 EnumerateVariants 相同的进制规则合成。
/// 值越界或 allowed 拒绝 → nullopt。
std::optional<Variant> EncodeVariant(uint16_t familyIndex, uint8_t geometry,
                                     std::initializer_list<uint8_t> freeValues);
/// EncodeVariant 的逆：从 ordinal 重建完整 variant（含 pinned 值）。
std::optional<Variant> DecodeVariant(uint16_t familyIndex, uint32_t ordinal);
```

`Families.h`：

```cpp
/// 家族在表中的下标；常量求值中传入了不在表内的名字 → 编译失败
/// （C++17 无 consteval，用"未找到时调用非 constexpr 函数”实现）。
constexpr uint16_t FamilyIndexOf(const char* name);
```

**实现**：把 `Family.cpp` 里 `EnumerateVariants` 的 seed 构造与 ordinal 累加抽成内部函数 `CollectFreeDims` / `ComposeOrdinal`，三个入口共用，进制规则只此一份。`allowed` 过滤收进 `EncodeVariant`（拒绝 → nullopt → 匹配函数返回 false）。

**改动**：`FamilyMatch.cpp` 每个匹配函数只算自由维值，如 glass：`EncodeVariant(FamilyIndexOf("GlassRefraction"), 0, {dispersion, shapeKind, hasXp})`。

**行为变化**（写进提交说明）：匹配出未枚举变体时，以前被 `ComputeMatch` 后置检查记为 `PermutationNotEnumerated` 并丢弃；改后匹配函数直接 false、draw 落 Chain。正常路径不可达，GL 全量应无变化。

**测试**：新增 `FamilyTest` 往返测试——每家族每个枚举变体 `DecodeVariant(EncodeVariant(...))` 还原；越界值返回 nullopt。

#### 1b. 匹配函数表 + 互斥绊线

**接口**（`FamilyMatch.cpp`，运行时层，不进声明层——声明层不得依赖 `DrawShape`）：

```cpp
using FamilyMatchFunction = bool (*)(const DrawShape& shape, FamilyMatchResult* result);
const FamilyMatchFunction* FamilyMatchers();  // 按 FamilyIndexOf 定位；size == FamilyTableCount()（static_assert）
```

- 21 个匹配函数统一签名；Hairline 两个、Glass 两个用一行薄封装（不用 lambda）。
- DEBUG 启动断言：每行非空、名字与家族表一致。

**`MatchAssembledFamily`**：Release 首个接受者即返回；DEBUG 跑完全部，出现第二个接受者 → `LOGE("[family-conflict] %s and %s ...")` + `DEBUG_ASSERT(false)`。删除“表序即优先级”的注释。

**门禁补充**：GL 全量日志不得出现 `[family-conflict]`；`FamilyExclusivityTest` 枚举字段空间自动触发绊线。

#### 1c. 写入路径：只在建 program 时推导一次；字段按元素序号访问；去 lambda

**问题**：
- `DeclaredFamilyWriter.cpp:1011-1020` 每个 draw 重跑 `ExtractDrawShape` + `MatchAssembledFamily` + `DeriveLayout`（构造 string 向量）；
- `FieldIndexInSlice`（`Layout.cpp:151`）每查一个字段拼一次字符串；
- 字段缺失时有的断言、有的静默跳过；
- 文件内 17 个 lambda（其中 2 个是 glass 提交引入）。

**改动 1：program 持有 variant + Layout**
- `MakeProgramWriter(shaderName)` → `MakeProgramWriter(shaderName, PermutationVariant)`（调用点 `PrecompiledProgramCreator.cpp:714`）。
- 声明家族：`DecodeVariant(FamilyIndexOf(name), ordinal)` 构造 `DeclaredFamilyProgramWriter(variant)`，构造时 `DeriveLayout` 一次存成员；顶点属性配对检查挪到创建期只跑一次。
- 每个 draw 只调本家族那一个匹配函数 `FamilyMatchers()[variant.family]`（取叶子指针等数据来源）。
- DEBUG 断言：本次 draw 匹配出的 ordinal == program 持有 ordinal。若触发（同一 programKey 对应两个变体）停下汇报，不放宽。

**改动 2：元素序号访问**（`Layout.h` / `Module.h`）：

```cpp
constexpr uint8_t ElementIndexOf(const Module& module, const char* name);  // 名字写错 → 编译错误

struct Layout {
  // ...existing...
  /// [slot][elementIndex] → 字段下标；NoField 表示该元素在此变体缺席（when 排除）或非 uniform。
  std::vector<std::vector<uint16_t>> elementField;
  static constexpr uint16_t NoField = 0xFFFF;
};

class SlotWriter {  /// 一个模块槽的写入，按元素序号寻址
 public:
  SlotWriter(const Layout& layout, uint8_t slot, ParamBuffer* buffer);
  template <typename T> void set(uint8_t elementIndex, const T& value);          // 无条件元素缺席 → DEBUG_ASSERT
  template <typename T> void setIfPresent(uint8_t elementIndex, const T& value); // 仅 when 元素允许静默跳过
};
```

失败语义统一：名字写错 = 编译错误；无条件元素缺席 = 断言；只有 `when` 元素允许 `setIfPresent` 跳过。`FieldIndexInSlice` 与 `Layout::fieldNames` 保留给工具侧反射比对与诊断，运行时写入路径不再调用。

**改动 3**：17 个 lambda 全部去除（`put` 类 → `SlotWriter`；`std::any_of` 类 → 普通循环）。

**门禁补充**：perf 矩阵测试时间应下降；上升则停下分析。

### 第 2 步：Chain 模块化 + 声明式特化 + 收编 YUV（4 个提交）

动手前接口与伪代码再给用户过一次。

**现状（勘察结论）**：
- chain 的 frag 不是磁盘源文件，由 `StageEmitter.cpp:243 EmitFragmentEntry` 从 `decls/PointwiseChainDecl.h` 生成；vert 是 `kernels/pointwise_chain.vert` → `pipeline/chain/pointwise_vertex.inc`。
- 主循环在 `pipeline/chain/pointwise_fragment_main.inc`（355 行），求值在 `pointwise_chain_eval.inc`（176 行）；`slot/pointwise_slot_loop.inc` 只属于 YUV 尾，不属于 chain。
- 特化字段 5 个：`SlotCount`、`SlotPacked`、`RootIndex`、`ClipCoverageRegister`、`CoverageRootIndex`（C++ 名在 `ChainParams.h:15-27`）；id 分配：0=总开关，其后按扫描首用顺序，ivec4 数组每元素 4 个 id（`SpecCodegen.cpp:270-319`）。
- 容量：chain 16 槽（`ChainResourceCapacity`，`AOTResourceProfile.h:63`）、noise 32 槽（:64）、YUV 尾 3 槽（`MaxPointwiseSlots`，`KernelContract.h:39`）。
- GLSL 硬编码：`chainResults[16]`、`chainLeafTex[4]`、`NTEX` 0/4、`CHAIN_TILED_RECIPES 2/3`、`TiledRecipe*[3]`、`DeviceLeafCoordMatrix[4]`。

#### 2a. 声明层与装配器支持特化与展开（无运行时行为变化）

- `Module.h`：`whenValue` → `whenMask`；启用 `Element::spec`；新增 `UnrollSite { function, capacityElement, countElement }`（`Module` 增 `const UnrollSite* unroll`）。
- `Layout` 增 `std::vector<SpecField> specFields`：id 0=总开关；之后按模块顺序、模块内元素顺序；标量 1 个 id，ivec4 数组元素 4 个 id——与 `SpecCodegen.cpp:270-319` 现行规则一致，bundle spec 表不变。
- `Assembler.cpp`：为 spec 字段生成访问函数（`int Chain_SlotCount()`、`ivec4 Chain_SlotPacked(int)`），内部按总开关选常量或 uniform；constant_id 方言生成 `layout(constant_id = N) const int TGFX_SPEC_N = 0;`，GL/HLSL 文本方言生成 `#ifndef TGFX_SPEC_N` 默认宏（与 `GLSLDialect.cpp:227-240` 现行改写一致，各后端送值方式不变）；按 `UnrollSite` 生成展开调用点，文本与 `ExpandUnrollPragmas` 输出一致（性能与像素不变）。
- 测试：`LayoutTest`（spec id 分配与旧表一致）、`AssemblerTest`（带 spec+unroll 的测试模块，关键片段比对）。本提交无家族使用新能力，bundle 字节不变。

#### 2b. Chain 改声明式模块（新旧并存，临时开关默认关）

原则：1:1 移植，先不重构语义。

**`ChainGeometry` 模块**（承接 `pointwise_vertex.inc` + `pointwise_interface_layout.inc`）：
- dims：`GP_LAYOUT(5)`、`HAS_COVERAGE(2)`、`HAS_UV_COORD(2)`、`HAS_SUBSET(2)`。
- 元素：各布局顶点属性与 varying（`vColor`、`vCoverage`、`vTexSubset`、椭圆 offsets/radii、hairline `vEdgeDistance`、ComplexEllipse 组）、`Matrix`、`Color`、`HasCommonColor`、`StrokeEnabled`（layout 1）、`Coverage`/`AAEnabled`（layout 2）。
- 片元 hook：种下 `f.color`（`vColor` 或 `HasCommonColor` 选 `Color`）与 `f.coverage`（`gpPixelCoverage`）。
- 与固定家族几何模块不去重（chain 的 color 属性无条件存在等差异），留第 7 步。

**`Chain` 模块**：
- dims：`LEAVES(2)`（0/4，原 TEXTURE_COUNT）、`RESOURCE(3)`（None/Lut/Noise）、`MASK(2)`（原 HAS_MASK_TEXTURE）、`CAP(3)`（0=16 槽、1=32 槽、2=尾 3 槽）。
- 元素：`pointwise_fragment_uniforms.inc` 系列的全部 UBO 成员逐项声明；`SlotPacked`、`ChainData` 按 CAP 三组 `whenMask` 互斥；5 个 spec 字段 `spec=true`；叶子 `CoordMatrix[4]`、`TransformedCoords` varying、`TextureSampler_0..3`、mask 采样器、LUT/noise 采样器各带 `when`。
- 片元 hook：`pointwise_fragment_main.inc` 拆分——几何部分归 ChainGeometry，其余归 Chain；Chain 从 `f.color`/`f.coverage` 读输入，不再直读 `vColor`/`vCoverage`（这是能挂在 YUVTexture 后面的前提）。
- device mask（`HasMaskTexture` runtime 分支 + NTEX=0 编译期 mask）1:1 留在 Chain 内。

**GLSL 归属**：`pipeline/chain/*.inc` 中含 uniform 的部分搬进声明；其余成为 `modules/Chain.{vert,frag}.glsl`、`modules/ChainGeometry.{vert,frag}.glsl`；纯数学（`operator/`、gradient、noise、tiled sampling）留在 `glsl/` 库，库文件不得出现 uniform/in/out/layout。表达式逐字照搬。

**家族与变体守恒**：
- 新家族 `Chain`：几何 `ChainGeometry`，body `Chain`，后接 Xfer。自由维度 = ChainGeometry 4 + Chain 4 + Xfer 1 = 9；自由维取值数乘积 4320 ≪ 2^20。
- `allowed` 逐条复刻 `PointwiseChainShader` 的 Compose 与 `FragValuesAllowed`；CAP 绑定：RESOURCE=Noise → CAP=1，其余 → CAP=0；排除 CAP=2。
- **`ChainEquivalenceTest`**：每后端 profile 建立旧 (vert, frag) ↔ 新 variant id 双射；数量相等（GL 现为 111）且每对 define 值一致。

**匹配与写入**：
- `MatchChain` 复用 `AOTChainBuilder::ResolveChainProgram`（结果 memo 在 `ir->program`），从 `AOTProgram` 推各维值。
- 新增 `ChainModuleWriter`：数据来自 `AOTProgram` + `BuildChainCoordTransforms`；采样器按 `program->bindings` 顺序（移植 `ChainProgramWriter::writeSamplers`）。
- 新增 `ResolveSpec(const AOTProgram&)`：5 个特化值直接读编译结果，**不再走 SpecCapture 的 scratch 写入再读回**。

**临时开关** `PrecompiledShaderCache::declaredChainEnabled`（默认关）：开时声明式 Chain 排在旧 chain 之前。本提交用开关开的状态跑一次 GL 全量，失败清单与关时**逐条相同**，`precompiledPrograms` 不变。

#### 2c. 切新 Chain，删旧 chain 与旧特化机制

- 删开关，声明式 Chain 生效。
- 删旧 chain：`kernels/PointwiseChainShader.{h,cpp}`、`kernels/pointwise_chain.vert`、`decls/`（`PointwiseChainDecl.h`、`AllFragmentDecls.h`、`ModuleDecl.h`）、`StageEmitter.*`、`glsl/pipeline/chain/*.inc`、`ChainProgramWriter`。
- 删旧特化机制：`spec/SpecCapture.*`、`spec/SpecResolver.*`、`spec/SpecSource.h`、`_specScratch` 与 `findOrCreateSpecScratch`、gate 框架（`SpecGateInfo` + `ApplyTextSpecGates`；src 下 GLSL 无一 gate）、工具侧 `ScanSpecUsage`、`GenerateSpecStubInclude`、`ExpandUnrollPragmas`、spec.raw-read 检查、环境变量 `TGFX_AOT_SPECIALIZE`（总开关改用 cache 已有的 `setSpecializationEnabled`）。
- 保留：`SpecInjector`（各后端送值）、`SpecValues`（key 拼接）、`FallbackToGeneric`。
- 测试：`AOTSpecTest` 改用 cache 开关对比特化/非特化；`ModuleDeclTest`、`ReachabilityTest`、`ShaderSourcesTest` 中旧 chain 引用删除或改写。

#### 2d. YUV 挂尾，删旧内核与旧表

**家族声明**：

```cpp
inline constexpr int8_t YUVChainTailPins[] = {/*LEAVES*/ 0, /*RESOURCE*/ 0, /*MASK*/ 0, /*CAP*/ 2};
inline constexpr FamilyModule YUVTextureBody[] = {&YUVTexture, {&Chain, YUVChainTailPins},
                                                  &AnalyticClip};
```

Chain 维度全 pin → 变体仍 12（`HAS_UV_COORD 2 × FORMAT 2 × HAS_XP 3`）；CAP=2 钉 3 槽，UBO 不膨胀。

**匹配**：`MatchYUVTexture` 接受“YUV 叶子 + 0..3 个算子”（复用 `CollectYUVTail`、`CompileYUVProgramForInfo`），`ops.empty()` 限制取消；`SlotCount` = 算子数，裸 YUV 为 0、循环不执行（与旧 byte-identical）。

**删除**：`YUVTextureFillShader.{h,cpp}`、`yuv_texture_fill.{vert,frag}`、`YUVProgramWriter`、`BindYUVProgram`、`ResolveYUVProgram`、`slot/pointwise_slot_loop.inc`、`slot/pointwise_op.inc`（另一使用者 `fill/fill_fragment_main.inc` 已无调用方，一并删）、`fill/fill_fragment_main.inc`。

**旧表清空后一并删**：`KernelTable.{h,cpp}`、`ShaderFamily.h`、`FillKernel.h`、`kernels/` 目录、CMake `AllShaderFamilies.h` 生成段（`CMakeLists.txt:775-816`）、`ProgramWriterFactory` 特判、`FamilyExclusivityTest` 旧表部分、`DEBUG_OVERLAP_REPORT`。运行时匹配顺序变为**声明表单层**（Chain 即声明家族之一），miss 即丢弃并计数。

**暂不删**（第 6 步 bundle 改 id 索引时删）：`ShaderPermutation.*`、`PrecompiledShaderInfo`、`ShaderRegistry`、`FamilyDomains`。

**新增截图用例** `ImageRenderTest.YUVImageToDisplayP3`（I420 → Display P3 surface，覆盖 YUV+ColorSpaceXform 路径），基线由用户 `/accept-baseline` 接受。

**已知差异**：YUV+算子由 `slot/pointwise_op.inc` 换成 chain 执行器，可能 1 LSB；无旧基线覆盖此组合，以新用例基线为准。

**删除范围以 grep + 编译通过为界**，不靠记忆。

### 第 3 步：Chain 编译器收敛（1–2 个提交）

阶段合并（现 8 个）：

| 新阶段 | 合并现有 |
|---|---|
| Schedule | `MergeNodeSpaces` + `ScheduleChainSlots` |
| Encode | `EncodeChainSlots` + `ResolveFinalChainProfile` + `ResolveCoverageRoot` + `RouteAnalyticChains` |
| Registers | `AssignChainRegisters` |
| Bindings | `FinalizeChainLayout` + `CompileChainLayout` |

删除概念：
- `AOTResourceProfile` 三档：RESOURCE 由 Encode 直接推导（有 gradient→Lut、有 noise→Noise、否则 None），维度值即原“档”；
- `AOTChainLayout` 并入 `AOTProgram`；
- `AOTChainOp` 镜像：直接用 `AOTEffectKind` + `ChainOps.h`；
- 可推导字段 `deviceLeaves`、`samplerChildCount`、`hasMaskTexture`、`lutLeafIndex`、`noiseSamplerBase`：改从 `bindings` 推导，调用点改调推导函数。

容量常量收成一处：Chain 模块元素的 `count` 是唯一定义；C++ 经 constexpr `ElementOf(Chain, "SlotPacked").count` 读取；删 `MaxPointwiseSlots`、`ChainResourceCapacity`、`NoiseGraphResourceCapacity`、`SlotCapacityDefines()`；GLSL 硬编码 `chainResults[16]`/`[4]`/`TiledRecipe*[3]` 等改用装配器生成宏。

门禁：`AOTEffectTest` 编译结果快照不变（slot 编码、寄存器分配逐字节一致）；GL 全量照常。

### 第 4 步：Chain 新增两种几何（需单独批准变体增加）

1. 先用 `.codebuddy/data/libpag-aot-hits-*.json` 核实 ShapeInstanced、RoundStrokeRect 进 Chain 的真实需求（NoMatchingRule 形状、ComplexEllipse 58 次命中）；数据不支持则跳过。
2. `ChainGeometry.GP_LAYOUT` 5→7，新增两个顶点布局。
3. 先实测各几何的 bundle 字节增量，交用户批准后再做。

### 第 5 步：prepare 只规划一次，writer 只读 IR（3 个提交）

#### 5a. DrawPlan

```cpp
/// prepare 期构建一次，存在 op 上，是 program 查找与参数写入的唯一输入。
struct DrawPlan {
  Variant variant;
  std::array<SourceRef, MaxModules> sources;  // 每槽位数据来源：IR 节点 / coverage 项 / 几何 / xfer
  GeometryRecord geometry;  // view、uv 矩阵、common color、aa、stroke、coverage、atlas proxy
  XferRecord xfer;          // blend mode、dst proxy 与 offset、framebuffer fetch
  bool alphaOnlyTarget = false;
};
```

- `GeometryRecord`/`XferRecord` prepare 期从处理器提取一次——这是全系统**唯一**允许读 GP/XP 的地方（即现 `DescribeGeometryProcessor`、`ClassifyXferForMatch` 的位置）。
- 匹配函数入参改为 `(const DrawIR&, const DrawPlanInputs&)`；删 `DrawFacts.h`、`DrawShape.cpp`。

#### 5b. writer 合一

```cpp
using ModuleWriter = void (*)(const DrawPlan& plan, const DrawIR& ir, SourceRef source,
                              SlotWriter* writer, TextureList* textures);
```

- 按模块分文件 `modules/<Name>.cpp`，函数表分发，无 if 链。
- 叶子坐标矩阵：IR 叶子 `uvMatrix` × view 归一化 × 几何 uv（逐点对齐 `CoordTransform::getTotalMatrix`），**删 `CoordTransformIter`**。
- glass 背景变换：IR 增 `glassCoordMatrix`。
- 采样器：`TextureList` 按模块顺序 push，数量必等于 `layout.textures.size()`；“声明了但本次不用”的槽由模块显式 push dummy——删 `writeSamplers` 补洞启发式（`ProgramWriter.cpp:746-759`）。
- 4 个 `ProgramWriter` 子类合 1。

#### 5c. programKey 与入口合并

- `programKey` = variant id + SpecValues + 顶点属性布局 key + pipeline 状态（blend、xfer、格式）；删 `ProgramInfo::buildProgramKey` 中各 FP `computeProcessorKey` 部分。
- 删 `AOTProgramInfo`：多 pass 的每个 pass 也构造 `DrawPlan` 走同一入口。
- 合并 `routeDraw`/`routeFill` 重复。
- 删：FP/XP `setData`、`writeProcessorsByTree`、`ParamCatalog`、`AOTParamRegistry`（字段核对由“反射==推导布局”承担，该检查已在 `BuildCommand.cpp:664-669`）、`UniformData` 按名表、`colorFPCount`/`coverageFPCount` 守卫、`AssertSingleLeafFacts`、decomposition 开关、3 个 dump 环境变量、`injectedPassFailure`；`dedicatedKernels` 改名 `fixedFamiliesEnabled`。

### 第 6 步：工具瘦身（4 个提交）

| 提交 | 内容 |
|---|---|
| 6a | bundle 以 `{variantId, vertexBlob, fragmentBlob}` 按 id 排序；读写共用 `Bundle.h`；删 manifest、ABI、identity、sourceDigest、`BundleVerifier`、`VerifyCommand`；同删 `ShaderPermutation.*`、`PrecompiledShaderInfo`、`ShaderRegistry`、`FamilyDomains` |
| 6b | 校验只留“反射==推导布局”；删 `ContractRules`、`BindingLayoutRule`、Source/Variant 各项检查；4 种报告并 1 |
| 6c | 试验 GL 走 SPIRV-Cross：像素一致 → 删 `GLSLDialect` 手写改写；不一致 → 只删 leak 扫描 |
| 6d | `test/shader_tool` 只留 Assembler、Layout、Compile、StageCache 四项 |

### 第 7 步：数据决定去留

`fixedFamiliesEnabled=false` 跑等价 + libpag 命中/性能对照，逐项定：
- ShapeInstancedTextureCoverage 是否交 Chain；
- glass 3 条 gap（tiled mask）交 Chain 还是补家族；
- `ChainGeometry` 与固定家族几何模块去重；
- Chain 内嵌 mask 拆 `DeviceMask` 模块。

### 第 8 步：删处理器层（可选）

第 5 步后处理器只剩“数据 + `lowerToAOT`”，已是 IR 生产者；第 7 步后按收益（去一层间接）与成本（41 处构造点、约 8 天）定。要做先给逐文件映射。

---

## 六、风险

| 风险 | 应对 |
|---|---|
| Chain 改装配器生成后 GL 出现 1 LSB 差异 | 表达式逐字照搬；2b 开关做新旧对照，失败清单必须逐条相同；有差异停下汇报 |
| 同一 programKey 对应不同变体（1c 断言暴露） | 停下定位根因，不放宽断言 |
| `MaxDims` 6→10 影响既有家族编码 | ordinal 只由自由维决定，已迁家族不变；1a 往返测试兜底 |
| YUV+算子换 chain 执行器的结果差异 | 新用例定基线；无旧基线可比，提交说明写明 |
| 第 5 步 IR 参数不足 | 由 `GeometryRecord` 补齐；补齐前 writer 不许读处理器 |
| Metal 基线与 GL 不一致导致误报 | 第 0 步先建 Metal 清单；差异逐条归因后再判 |

---

## 七、进度记录

| 步骤 | 状态 | 提交 | 门禁 | 备注 |
|---|---|---|---|---|
| 0 Metal 基线 | 完成 | — | MET：`noMatchingRule=0 droppedDraws=0 precompiledPrograms=2196` | 2026-11-10 @ `31bcedd1d`：934 测试 912 过，14 条失败与计划文档 433 行记录一致（基线存 `.codebuddy/gates/step0_metal_fails.txt`）；全部为截图比对失败，glass/YUV 测试全绿；`ImageFilterShader` 为 Metal 独有差异（mipmap+Repeat，11045 像素），归因留第 2 步等价对照 |
| 1a EncodeVariant | 完成 | `f4811bf13` | MET：`precompiledPrograms=2193` 不变，24 条 GL 失败与基线逐条一致；ShaderBuildToolTest 222/222；FamilyTest 3/3 | 顺带修复潜在 bug：`MatchShapeInstanced` 手写 ordinal（HAS_XP）对 blended xfer 落在可达集之外，现按枚举规则编码 `2*HAS_XP`；`Family.cpp` 用 `<cassert>`（声明层库无 Log.h 依赖） |
| 1b 互斥绊线 | 完成 | `01f8e3e53` | MET：绊线零触发（全量+字段空间枚举）；24 条 GL 失败与基线逐条一致；工具 222/222；L2 审计通过 | `MatcherFor` 按名字定位（switch 以 FamilyIndexOf 常量为 case），漏配规则编译期失败；21 个匹配函数统一签名进匿名命名空间 |
| 1c 写入路径 | 完成 | `509963980` | MET：24 条 GL 失败与基线逐条一致；`precompiledPrograms=2193` 不变；工具 222/222；L2 审计通过；写入热路径 `DedicatedVsChainHotFamilies` −16%（5198→4345ms） | Layout 构造期推导一次 + `elementField` 表 + `SlotWriter`（set=断言 / setIfPresent=when 元素）+ `UniformElementIndexOf` 编译期查名；首个战果：`QuadColorGeometry` 的 `Color` varying 排在 param 前，原始首匹配查找踩中 varying，断言当场拦截；17 个 lambda 全删 |
| 1 阶段收尾 | 完成 | — | Metal 全量：14 条失败与第 0 步基线逐条一致，coverage gate MET（2196） | 2026-11-10 @ `509963980` |
| 2a 特化/展开支持 | 完成 | `c00d279fb` | MET：`.bin` 与 manifest 逐字节一致（`55d362…`/`b16619…`）；24 条 GL 失败与基线逐条一致；`precompiledPrograms=2193` 不变；工具 230/230 | `whenMask` 迁移 48 处（48 处门控元素、15 个模块头）；`countWhen/countByValue` + `Layout::Field::arraySize`→uint16（噪声档 `32*7+32=256` 恰超一字节，断言钉在 Layout.cpp）；`DeriveSpecFields` 单一 id 分配（0=总开关、模块序→元素序、Int 1 个/Int4 每元素 4 个）；装配器生成 constant_id 前导 + 访问函数 + 逐槽展开宏；GLSLDialect 去 `constant_id`；**偏差**：spec id↔SPIRV 检查随首个消费者（2b）落地——只有死代码可跑的检查无法验证；**备忘**：`modules/*.h` 不在 sourceDigest 覆盖内（字节门禁是 whenMask 迁移的唯一防线），2b 顺手纳入 |
| 2b Chain 模块化 | 完成 | `ce62453e6` + `3d2085685` + `c8127df45` | 2b-3：开关 ON 24 条失败逐条一致、gate MET | 等价测试 111 双射 + spec id 序；开关 ON 时 AOT 多 1 次程序创建（见 2c-1） |
| 2c-1 删旧 Chain | 完成 | `aa4b061c2` | MET：bundle 262→**188** 条目；24 条 GL 失败与基线逐条一致；gate MET（noMatchingRule=0 droppedDraws=0，2194 程序）；工具 221/221 | 家族壳/声明/入口 GLSL/uniform 片段删除；接受逻辑活为 `ChainInputs`（DeriveInputs+Compose+MatchVariant+MatchProgram+Reachable 111 冻结）；**拦下两个真问题**：① 直接删解释器块会让 decomposition 策略路径丢 105 个 draw（已用 `MatchChainProgram` 恢复该路由）② `Chain.h` 六个 CTM 门控跨行书写、`when` 仍为 -1（2b 批量替换漏掉，布局全无条件；已修为 0x7C/0x64/0x60，ordinal 0 顶点块只剩 RTAdjust+Matrix）；lint 补扫 `modules/*.glsl` 与 pinned 维名 |
| 2c-2 删旧特化管线 | 完成 | `433413794` | MET：bundle `.bin` 与 manifest **逐字节一致**（删除前后同哈希）；24 条 GL 失败与基线逐条一致；gate MET（2194）；工具 221/221 | 删工具侧 scan/stub/双编译/漂移比对/gate 求值/unroll 展开（−580 行）；**新增 spec id ↔ SPIRV 校验**（读回 OpDecorate 的 SpecId 与 `DeriveSpecFields` 对比，复用 `spec.layout-drift` 码）；三个只被删除扫描器使用的诊断码随之退休，`spec.reader` 保留。**遗留**：`aot/spec/*`（SpecCapture/Resolver）仍有活消费者（声明链的特化值），直接推导 `ResolveSpec(AOTProgram)` 与 Metal 开关 ON 验证留待 2d 一并做 |
| 2d YUV 收进声明层 | 完成 | `ed193a552` | MET：YUV 像素基线（I420 + RGBAAA）通过；24 条 GL 失败与基线逐条一致；gate MET（noMatchingRule=0，2194 程序不变）；工具 210/210；bundle 180 条目 | `YUVChainTail` 声明模块（SlotCount/SlotPacked[3]/ChainData[53]/CS 块/裁剪契约）+ 旧 main 逐字进 hook + `bodyOwnsOutput` + 匹配沿用旧 CollectYUVFacts 边界 + 写入复用 `BindYUVProgram`；**黑屏根因**：规则未回填 `tiledLeaf`，写入器整体跳过 YUV 参数。旧 `YUVTextureFillShader` 删除，legacy 注册表归零。**遗留**：`FillKernel/ShaderFamily/KernelTable/StageEmitter/SourceLoader` 已成死代码（2e 删） |
| 2e-1 删旧框架 | 完成 | `aaedd9871` | MET：24 条 GL 失败与基线一致；gate MET（2194 不变）；工具 209/209 | 删 `ShaderFamily`/`FillKernel`/`KernelTable`/CMake 家族冒泡生成/注册表工厂/工具 per-factory 编译循环（**−1,396 行**）；`MatchDeclaredKernel` 承接内核表最后的职责；两个死诊断码退休 |
| 2e-2 特化直推 | 完成 | `2932db413` | MET：`AOTSpecTest` 两项通过；24 条 GL 失败一致；gate MET（**2194 程序不变** = 推导值与旧捕获逐字相同）；工具 209/209 | `ResolveChainSpec` 直接读 `AOTProgram`（槽计数/打包/三寄存器）；槽位 selector 抽成 `ChainSlotSelector` 供 Binder 与推导共用，杜绝双份打包漂移；删 `SpecCapture` 与 scratch 回读；未知字段 fail-closed |
| 2e-3 删 scratch/写入器壳 | 完成 | `86b7d979c` | MET：24 条一致；gate MET（2194 不变）；工具 209/209 | 删 `SpecScratch`/缓存 scratch 存储与 4 个虚函数、`YUVProgramWriter`/`bindYUVTail`（−98 行）。**遗留**：工具侧 `StageEmitter`/`decls`/源加载器仍承担 manifest 的 sourceDigest，需先把摘要来源改为直接扫描 glsl 目录才能删 |
| 2e-4 收口：摘要修复+删残壳 | 完成 | `0dc2cabe3` + `c519ad6a1` | A：`.bin` 逐字节不变（`03e90f1b…`）、摘要一次性变化（`0xdaf6…`→`0x94e2…`）、24 条一致、gate MET 2194、工具 212/212（5 个新摘要用例全过）。B：`.bin` 与摘要**与 A 后逐字节一致**（删除零产物影响）、24 条一致、gate MET 2194、工具 211/211 | **审计实证两个已存在的洞并修复**：① 摘要自 2d 起不覆盖任何 GLSL（hook 加注释重建摘要纹丝不动）→ `ComputeSourceDigest` 改为直扫目录（白名单 .glsl/.inc/.vert/.frag，路径+字节，v2 种子）② CMake glob 漏 `*.glsl`（67 个 hook 不触发 bundle 重建）→ 补上。verifier 的 sources/stageOverrides 管道随之删除；旧审计的"`SourceDigestTest` 不存在"说法有误（64 行存在但只测契约敏感性，不测 GLSL 敏感性——已补齐两个方向的用例）。B 删 `StageEmitter`/`decls`/源加载器（−872 行）：`EmitDeclaredFragmentEntries` 自 2c 起返回空映射，overrideHits 检查是 0==0 空转；`ShaderSources` 保留 `ExpandStageIncludes` 与 `ReadGeneratorContract`（有活调用方）；StageDump 去 sources 参数；CMake decls glob 删除（声明头编进工具，改头必然重链触发重建）。**注意**：GL 门禁的失败清单管道会混入一行"24 tests, listed below"汇总残影，比基线多一行属正常，比对前需剔除。流程遗留已清除：早前超时取消损坏的 `.ninja_log` 已删（曾致工具每次重链→stage cache 全 miss→每次 bundle 构建全量价） |
| 2c 删旧 chain/特化 | 未开始 | | | |
| 2d YUV 收编+删旧表 | 未开始 | | | 新用例基线需 /accept-baseline |
| 3 编译器收敛 | 未开始 | | | |
| 4 两种新几何 | 未开始 | | | 变体增加需单独批准 |
| 5a DrawPlan | 未开始 | | | |
| 5b writer 合一 | 未开始 | | | |
| 5c key/入口合并 | 未开始 | | | |
| 6a–6d 工具瘦身 | 未开始 | | | |
| 7 数据去留 | 未开始 | | | |
| 8 删处理器层 | 未开始 | | | 可选 |
