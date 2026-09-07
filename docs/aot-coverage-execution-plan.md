# TGFX Shader AOT 覆盖收敛执行计划（WP1–WP5）

> 与 `aot-architecture-analysis.md` 配套的实施层文档：架构与判定规则见彼处，本文只回答"怎么做、做到什么程度算完"。数据基线：2026-09 全量 Metal（686 用例，屏上未命中 73 = RRect 32 + Glass 折射 21 + Glass 帐篷模糊 12 + RectEffect 边角 8；离屏动态编译 98 次全部为 Glass 帐篷模糊）。
>
> 通用纪律（每个 WP 生效）：文档类文件永不提交；runtime uniform 优先于变体维度（新维度需评审给出接口不同的理由）；每 WP 收尾统一执行——`codeformat.sh` → OpenGL + Metal 双构建 → 全量 Metal 三件套验收（测试通过数、fallback 数字、AOTClosureVerifier）→ 仅提交代码文件（`git commit --only`）。

---

## 总览

```
WP1 Glass 帐篷模糊入邻域核族  ──► WP2 Glass 折射入坐标采样族 ──► WP3 coverage 域闭合 ──► WP4 DAG 切分 ──► WP5 门禁阻断化
      │                                │                              │                      │
     M4                              M4                            M3(Coverage)          M5 前置
```

| WP | 消灭缺口 | 当前量级 | 依赖 |
|---|---|---|---|
| WP1 | 离屏动态编译 98 + 屏上帐篷模糊 12 | 中偏重 | 无 |
| WP2 | 屏上折射 21 | 中（前置确认后定） | 无（可与 WP1 并行设计） |
| WP3 | RRect 32 + RectEffect 边角 8 | 中 | 无 |
| WP4 | 超预算组合（当前 0 触发，终态必需） | 最重 | WP1 骨架经验 |
| WP5 | 未来退化防护 | 轻 | WP1–WP3 的 strict 断言素材 |

---

## WP1：Glass 帐篷模糊入邻域核族

### 目标

`GlassUDFTentBlurFragmentProcessor` 的离屏 fill（98 次，全量测试唯一动态编译路径）与屏上 program 查找（12 次）全部命中预编译产物。

### 前置门槛（完成才开工）

1. **radius 分布埋点**：在 `GlassUDFTentBlurFragmentProcessor::Make`（`src/layers/processors/GlassUDFTentBlurFragmentProcessor.cpp:30-50`，普通静态函数，埋点可行）记录主导半径分量的实际取值，跑一次全量 Metal 收数据。判定：若实际半径普遍 ≤ 某档（如 16），评估固定上界分档；否则单档 64（构造默认值）。
2. **编译产物实测**：按选定上界写试验性 GLSL（固定循环 + 早退），走构建链产出 metallib，记录大小与 Metal 反编译寄存器压力。上界 64 对应 129-tap 支撑域（双线性成对合并后约 65 次实际采样，`GlassUDFTentBlurFragmentProcessor.cpp:83-85` 已有此优化），若产物异常回到门槛 1 重议分档。
3. 确认 tent 权重可 CPU 预计算并归一化（预期成立：线性衰减核与高斯同构）。

### 设计要点

- **独立新 shader，不动 `GaussianBlur1DShader`**（现有 136 次命中零回归风险）；族骨架以共享 include（采样循环骨架、`Kernel` 权重数组契约、`Step` 方向、`Subset` 无条件 clamp）固化，第三个邻域成员出现时再评估合并。
- **每个实例单源单半径**（审计修正：构造虽接受 fine/coarse 两个 source，但每实例只注册与 `field` 匹配的那个为 child，另一个必须为 null，`.h:38-39`；`GlassUDFRadius` Float2 上传两组半径、每 pass 只读一个分量）——因此**不需要采样源档维度**，维度只有 `HAS_XP`（3 值），共 3 个变体，vertex 无维度。
- runtime uniform：半径对（两分量）、方向、`inputIsPacked`（两 pass 间 packed 布局的解码开关）、`field`（输出通道选择）、核权重数组（CPU 预算，复用 blur1D 的 `Kernel` vec4 数组上传契约，`GaussianBlur1DShader.h:39` / `gaussian_blur_1d.frag:34-39` 的 `Kernel vec4[17]` 镜像了 `Blur1DFragmentProcessor::KERNEL_VEC4_COUNT`——新 shader 的数组大小需按 64 上界另算并同样以字面量写死，反射提取器不展开宏）。
- child 形态准入：tiled child 仅接受 `TiledModeSupported` 的模式（0/1/2/3/6/7/8，**无 mipmap-repeat 4/5**——比 fill 路径窄，matcher 条件以此为准）。
- 审计归类：`OffscreenFillSource` 增加 `GlassUDF` 枚举值（当前枚举在 `PrecompiledShaderCache.h:112-120`，无此项）；`GlassUDF.cpp:123-124、146-147` 两处 `fillRTWithFP` 调用当前只传 3 参走默认 `Unknown`，补传 source。

### 改动面

| 文件 | 动作 |
|---|---|
| `src/gpu/shaders/level1/GlassUDFTentBlurShader.h` | 新增（照 `GaussianBlur1DShader.h` 结构） |
| `src/gpu/shaders/glsl/level1/glass_udf_tent_blur.frag/.vert` | 新增（含 packed 编解码片段） |
| `src/layers/processors/GlassUDFTentBlurFragmentProcessor.h/.cpp` | FP 类所在位置（Layers 层而非 `src/gpu/processors/`），埋点与 matcher 识别的引用源 |
| `src/gpu/PermutationMatcher.cpp` | 新增 `TryMatchGlassUDFTentBlur`（离屏 fill 经 `RectDrawOp`→`QuadPerEdgeAAGeometryProcessor`，GP 固定；1 colorFP、child 形态、radius ≤ 上界） |
| `src/gpu/shaders/PermutationRules.cpp` | 新增 Compose 编码（维度对齐声明） |
| `src/gpu/PrecompiledShaderCache.h` | `OffscreenFillSource::GlassUDF` 枚举 |
| `src/layers/layerstyles/GlassUDF.cpp` | `fillRTWithFP` 传 source |
| `resources/shaders/shader_bundle.*.bin` | 重建（bundle 构建流程） |
| `test/src/AOTRenderConsistencyTest.cpp` | 新增双路径差分用例（Glass 场景） |

### 实施步骤

1. radius 埋点 → 全量跑 → 出分布数据 → 定上界策略（写入本文档修订记录）。
2. 写 GLSL（frag：双源采样循环 + packed 解码 + field 通道选择；vert：标准 quad）。
3. 声明 + PermutationRules 编码 + bundle 重建。
4. matcher 规则（注意插入顺序的防误吞原则：specialized 在前）。
5. 差分测试：GlassStyle 现有截图基线 + 新增双路径用例（**绘制内容必须伸出效果边界**——coverage 类用例教训）。
6. 收尾三件套。

### 验收标准

- `offscreenFillAudit` 中 GlassUDF 源 `programBuilderPrograms == 0`；
- 屏上 `GlassUDFTentBlurFragmentProcessor` 相关 NoMatchingRule 归零（73 → 61）；
- 全量失败数不高于当前（10）；GlassStyle* 差分 ≤1/255。

### 风险与回退

| 风险 | 应对 |
|---|---|
| 129-tap 循环上界的寄存器/编译产物压力 | 前置门槛 2 实测；分档是后备（但引入维度需评审） |
| packed 布局编解码与 GlassUDF 两 pass 耦合写错 | 差分测试逐像素兜底；packed 片段独立成 include 便于对照 |
| tiled child 模式条件写宽导致误命中 | matcher 条件严格照 `TiledModeSupported` |

过渡期安全网：任何不命中自动回退 ProgramBuilder，不会画错。

---

## WP2：Glass 折射入程序化坐标采样族

### 目标

`GlassRefractionFragmentProcessor`（SDF/UDF 两种几何 child）的屏上 21 次未命中全部归零；Glass 族整体零回退。

### 前置门槛

**GP varying 依赖确认**：读 `GlassSDFGeometryFragmentProcessor` / `GlassUDFGeometryFragmentProcessor` 的 `emitCode`，确认折射求值是否消费 EllipseGP 的顶点 varying。预期：形状参数走 fragment uniform（工作量小）；若确有 varying 依赖，vertex 侧工作量上调，本 WP 量级重估。

### 设计要点

- 骨架 = 1 背景纹理 + offset 场 + 合成参数全 uniform。该骨架同时是未来 displacement/lighting 的接入位。
- `GEOMETRY_KIND` 编译期维度（3 值：SDF 无纹理 / UDF 1 纹理 / UDF+EdgeLight 2 纹理）——sampler 布局不同是维度理由；`GlassShapeType`、折射强度、色散、高光参数全 uniform。
- matcher 接 `EllipseGeometryProcessor`（19 次主力）与 `QuadPerEdgeAAGeometryProcessor`（2 次）两个 GP；coverage/XP 走既有共享函数。
- **注意**：chain 路线对 EllipseGP 是 `GPIncompatible`，本 WP 只能走 L1 直挂——matcher 是唯一路径。

### 改动面

`GlassRefractionShader.h`（新）、`glass_refraction.frag/.vert`（新，vert 按 GP 依赖结论定复杂度）、matcher、PermutationRules、bundle 重建、差分用例。

### 实施步骤

1. 前置门槛确认（输出：varying 依赖结论与 vertex 工作量结论）。
2. GLSL：SDF/UDF 求值片段（对照 `GlassSDF/UDFGeometryFragmentProcessor::emitCode` 逐行翻译）+ 偏移采样 + 高光合成。
3. 声明 + 编码 + bundle + matcher（GEOMETRY_KIND 与 child 类型对应）。
4. 差分：GlassStyle 系列现有基线 + 双路径用例。
5. 收尾三件套。

### 验收标准

- 屏上 `GlassRefractionFragmentProcessor` NoMatchingRule 归零（73 → 52，若 WP1 已完成则 61 → 40）；
- Glass 族（折射 + 帐篷模糊 + 离屏）整体零回退、零动态编译；
- GlassStyle* 全部差分 ≤1/255。

### 风险与回退

SDF 数学逐行翻译的 LSB 偏移（差分兜底）；UDF+Edge 的 3 sampler（背景 1 + mask 1 + edgeMask 1）叠加 device mask 后到 4——贴预算上限，若超需物化 mask（差分验证）。

---

## WP3：coverage 域闭合（RRect）

### 目标

`RRectEffect` 作为 coverage 的 32 次未命中归零；`RectEffect` 边角 8 次按判定结果处理。

### 前置门槛

**RectEffect 边角判定**：对 8 次 fallback 逐个取结构签名，确认被 `IsFoldableAARectEffect` 拒绝的具体条件（非 device-space？非 AA？）。产出：若属"local-space rect"或"非 AA"，评估是否值得扩准入（收益 8 次）还是维持回退（记录为已知边界）。

### 设计要点

- **扩 runtime clip contract，零新变体维度**（红线）：
  - `coverage_uniforms.inc`：uniform 块增加 `RRectRect`（vec4）+ `RadiiX`（vec4）+ `RadiiY`（vec4）+ `DeviceToLocal`（mat3，RRect 解析求值在 local 空间算距离再 scale）；
  - `HasClip` 从 0/1 扩为枚举（0 无 / 1 rect / 2 rrect）——现有 int 上传路径兼容；
  - 新增 `rrect_clip_coverage.inc`：**逐行复用 `GLSLRRectEffect::emitCode` 的解析数学**（1px AA ramp、scale 归一化、透视拒绝条件与 matcher 侧一致）；
  - 所有持有运行时裁剪契约的 shader（11 个）自动获得能力；注意其中 5 个经 `coverage_uniforms.inc` include，**另 6 个在各自 uniform 块直接声明 `Rect`/`HasClip`（solid_color_fill、hairline_quad、quad_texture_fill、yuv_texture_fill、hairline_line、quad_color_fill）**，扩展契约时两类都必须同步修改。
- `ClassifyCoverageFP` 增加 `CoverageKind::RRect`；参数上传照 Rect 修复模式（`GLSLRRectEffect::onSetData` 写预编译字段——注意带 `hasField` 守卫与 `isDeviceSpace` 类似条件，RRectEffect 的对应判定）。
- 多重裁剪（`Compose(DSTE, RRect×N)`，RRect 32 的子集）：预计随 RRect 入链自动收敛（chain coverage 折叠已支持 `Compose(mask, rect)`，`AOTPlanExecutor.h:46-48`）；一期完成后实测，仍有残余才做二期。
- **bundle 重建纪律**：uniform block 布局变化 → Reflection 指纹变化 → **合入时全平台 bundle 必须同步重建**并过 Context 加载校验。

### 改动面

`coverage_uniforms.inc`、`rrect_clip_coverage.inc`（新）、`PermutationMatcher.cpp`（ClassifyCoverageFP + 各 TryMatch 的 coverage 分支）、`GLSLRRectEffect.cpp`（onSetData 预编译字段）、`RRectEffect` 头（如需暴露 device-space 判定）、bundle 全平台重建、差分用例（ClipTest.Overview、CanvasTest.Picture、DropShadow/InnerShadow 的 RRect clip 场景）。

### 实施步骤

1. 前置门槛判定（8 次边角的归因与决策）。
2. `rrect_clip_coverage.inc` 数学（对照 GLSLRRectEffect 逐行）+ `coverage_uniforms.inc` 扩展。
3. bundle 重建 + 加载校验（指纹链路演练）。
4. matcher：ClassifyCoverageFP 加 kind + 各 TryMatch 放行 + 参数上传。
5. 差分：**内容伸出裁剪边界的用例**（Rect bug 教训）覆盖 RRect 单元素、Compose 场景、嵌套 layer。
6. 收尾三件套。

### 验收标准

- RRect 相关 NoMatchingRule 归零（若边角也处理：73 → 12 或 61 → 0，视 WP1/WP2 完成情况）；
- 差分用例全绿；全量失败数不升。

### 风险与回退

AA ramp 数学的 LSB 对齐（差分兜底）；uniform 块膨胀后接近后端上限（Metal 4KB 档，当前余量充足，监控即可）；维度红线（评审时任何"加一维"的提议默认拒绝）。

---

## WP4：DAG 切分降级（终态完备性）

### 目标

超预算效果树（>4 纹理叶 / >16 槽的 blend-DAG）从"整体回退"变为"自动拆分为多个链 pass"；非 rect GP 的超预算组合获得绘制入口物化的终态承接路径。

### 前置门槛

1. **测试夹具先行**：构造 >4 sampler 的 `Xfermode-two` 嵌套组合（如 `Blend(Blend(Blend(A,B),Blend(C,D)),E)`）——没有夹具不开发。
2. **物化安全判据就位**：激活 `EffectTraits` 安全字段（当前全库零读取）作为规划器自选物化点的准入判据，或给出经评审的替代判据；物化安全清单（坐标偏移/apron/premultiplied/affectsTransparentBlack）逐类过检。
3. 切分最优性：第一版**只求可行不求最优**（合法切分点三条件：pointwise 节点处切、blend 不跨 pass、子 DAG ≤4 叶），取舍写入设计评审记录。

### 设计要点

- 规划器：`DecomposePointwiseDAG` 超预算时不再整体失败，按"叶数 ≤4 的极大子树"自底向上切分（可解性已证明：递归物化纹理数严格不增、树深严格变浅，任何二叉 DAG 可终止）。
- 执行器：`PointwiseChain` kernel 支持中间 pass 形态——复用 `AOTPlanExecutor` 的 `intermediatePasses`/`materializesOutput` 执行器，适配中间纹理 uv 换算、槽位/leaf 绑定顺序、sampler padding 约束（OpenGL texture-object 状态）；`ValidateLinearPlan` 不变量向 DAG 切分形态扩展。
- **终态边界承接**（§7 边界 1）：非 rect GP（椭圆/文字/网格）的超预算组合，物化决策管辖范围从 blend 子树扩展到 draw 入口——效果树先烘成纹理、形状照常 L1 直挂。
- 量化级数标定：≥4 级物化深度的差分断言，确定可接受级数并写死。

### 验收标准

- 构造夹具全部命中预编译（零回退）；
- 多级物化的量化级数差分在标定阈值内；
- 全量无回归；`fusableButSamplerBudgetExceeded` 场景零回退。

### 风险

最重的 WP：中间 pass 的绑定/坐标/padding 适配是主要工作量（不是"补一个分支"）；pass 数增长的性能账（极端组合带宽高于 main 单 shader，记录为已知边界）；规划器切分的语义红线由 Planning 后 pass 图不可变的不变量守护。

---

## WP5：效果覆盖门禁

### 目标

把"新效果类型必须带预编译支持合入"从约定变成机器闸门，防止无回退终态出现裸奔效果。

### 前置门槛

**FP 类型枚举机制选型**：审计确认 ClassID 体系**没有中央注册表**（`UniqueID::Next()` 只是惰性自增计数器，`Processor.h:27-31`，无法反查或枚举全集）；有注册表的是 PrecompiledShader（`ShaderRegistry::All()`），属 shader 维度而非 FP 维度。因此 WP5 开工前必须先定枚举机制：新增 FP 类型注册设施（评审）或基于 `name()`/手工清单的保守实现。

### 设计要点

- **FP 覆盖静态检查**：按前置门槛选定的枚举机制遍历 FP 类型全集，逐一确认被至少一条 matcher 规则或 `lowerToAOT` 认领；未认领类型构建期报错。实现落点：测试侧（跑在 CI 的 closure 测试）或 `AOTClosureVerifier` 的姊妹检查。
- **strict 全量断言**：全量报告断言 `NoMatchingRule == 0`（除 1 个故意用例）与离屏 `programBuilderPrograms == 0`；过渡期以监测模式运行（数字不达标仅告警），M5 升级为阻断。
- 注意 `AOTClosureVerifier` 本身只验证"已注册 shader 的可达集 ⊆ bundle"（产物完整性），覆盖门禁是它管不到的另一层——两者并存。

### 验收标准

门禁在 CI 生效；用一个故意不接入的测试 FP 验证拦截（红）；`ScalePictureImage` 剩余归因（嵌套光栅化历史问题）在此期间单独排查，不阻塞本 WP。

---

## 明确不做（Non-goals）

- 不为单档 radius 分档引入维度（除非前置实测数据支持并过评审）；
- 不在本轮做切分最优性（多刀合法时选哪刀）；
- 不合并 GaussianBlur1D 与新帐篷模糊为统一 shader（第三成员出现再评估）;
- 不承诺 bit-exact（一致性口径始终为差分 ≤1/255）；
- 不动 L3（已闭合）与 GP 轴（12 种已闭合）。

## 执行记录

| 日期 | 事项 |
|---|---|
| 2026-09-07 | 计划制定；Rect uniform bug 已修复（`d95d1f876`），HighZoomWithMask 转绿 |
| 2026-09-07 | **WP1 前置门槛 1 完成（radius 分布埋点）**：98 次调用与离屏 fill 数精确闭合；来源仅 3 个测试（GlassStyleEllipticalCorner / SingleCell / ExtremeAspectRatioUDF）。结论：**maxRadius 恒为 64（98/98），单档 64 上界即现状，无需分档**；实际半径跨度 fine 10.67~64（64 被实际用到 8 次，顶上界）、coarse 恒 4——**radius 必须 uniform 化，不可档位化**；field 两种都出现（Refraction 读 fine 分量 / EdgeLight 读 coarse 分量）；packed 严格按方向配对（dir=0 输入未 packed / dir=1 输入 packed），两模式都要在 shader 内支持（runtime uniform 分支）。变体终确认为 **HAS_XP × 1 = 3 个**。埋点已移除（无净代码变更） |
| 2026-09-07 | **WP1 实施完成并验收通过**：新增 `GlassUDFTentBlurShader`（3 变体，权重在 shader 内解析计算与 JIT 逐表达式一致——设计修正：不采用 blur1D 的 CPU 权重表，因 tent 线性核 GPU 算更优且 radius 为 float）；接线 PermutationRules（Compose/Enumerate/dispatch）、matcher（plain/tiled 双 child、上界 64）、`onSetData` 补 Field/InputIsPacked 预编译字段、`OffscreenFillSource::GlassUDF` 审计归类、四后端 bundle 重新生成发布（`tgfx_publish_shader_bundles`）、条目断言更新（96/264→97/267，WebGPU 94/177→95/180）。**验收数字**：GlassUDF 离屏 98 次 fill `programBuilderPrograms=0`（原 98 次动态编译全灭）；屏上 tent blur 未命中 0（原 12）；全量 Metal 676/10 与基线持平零回归；命中率 96.32%→96.93%；ClosureVerifier 绿。过程中被门禁抓住一次（新 shader 未发布 bundle 时 `AOTClosureTest` 报 4 个洞——门禁有效性实证）。遗留：Glass 场景的显式双路径差分用例并入 WP2 与折射一起补（现有截图基线已提供等效验证） |
| 2026-09-07 | **WP2 实施完成并验收通过**：前置门槛确认折射坐标走标准 coordTransform varying（无 GP 专属 varying 依赖，vertex 零额外工作量——修正了此前"vertex 侧是主要工作"的预判）；新增 `GlassRefractionShader`（**GEOMETRY_KIND 4 值 × HAS_XP 3 值 = 12 变体**，SDF_RRect/SDF_Ellipse/UDF/UDF_Edge 四档几何逐行翻译 JIT 数学；dispersion/lighting 折叠为 DispersionOn/LightingOn 运行时分支替代 JIT 的编译期剪枝；椭圆 AA coverage 复用 `ellipse_coverage.inc` 走 `TGFX_INITIAL_COVERAGE`）；绑定 EllipseGP 的 commonColor 属性形态（`gpClassName` 声明绑定）；matcher 加 shapeType/enableEdgeLighting 访问器判定维度档。GLSL 编译期踩坑一次：`#if/#else` 交错花括号不平衡（JIT 的 if/else 结构翻译成预处理分支时多了一个裸块）——kind1 变体过而 kind0 崩，提醒后续翻译注意分支括号配对。**验收数字**：折射未命中 21→**2**（EllipseGP 的 SDF 14 + UDF 5 全部命中；剩余 2 次为 QuadGP 形态，第一期有意留界——matcher 显式拒绝并注释）；3 个 GlassStyle 截图测试全绿（像素正确性由基线背书）；全量 676/10 零回归；命中率 96.93%→**97.89%**；四后端 bundle 重发布（断言 98/279，WebGPU 96/183）；ClosureVerifier 绿。**Glass 族整体状态：离屏动态编译 0、屏上未命中仅剩 QuadGP 折射 2 次（计划内边界）** |
