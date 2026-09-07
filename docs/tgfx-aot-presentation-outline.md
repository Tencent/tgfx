# TGFX AOT Shader 技术分享故事板

**总页数：21 页**

## 故事结构

1. **首次成本从哪里产生（1—3）**：一次绘制怎样形成完整 Shader、后端为何仍有首次工作、缓存为何无法覆盖新组合。
2. **AOT 应封闭什么（4—6）**：复用单独 Shader 资产而非穷举完整程序，借行业边界明确 TGFX 的库内答案。
3. **差异如何分层（7—11）**：区分完整程序、单阶段资产和运行时对象，把算法、接口与参数放入正确层级。
4. **构建期与运行时如何闭合（12—15）**：从开发者声明、构建生成、绘制匹配到后端对象创建形成同一套契约。
5. **开放组合如何受控执行（16—19）**：把效果组合变成可检查关系，分别说明分支单 Pass 与长线性链多 Pass。
6. **成本与证据如何限定（20—21）**：同时核算发布成本、运行时成本和中间纹理成本，并标明测试能证明的范围。

## 贯穿案例

- **主线性案例**：`Image Texture → Color Matrix → Alpha Threshold → SrcOver`。
- **静态接口扩展**：增加 DeviceMask，用于说明额外 sampler/binding 为什么必须形成新版本。
- **单 Pass 分支案例**：`Texture A → Matrix` 与 `Texture B → Luma` 汇入 Blend。
- **长链扩展**：在主线性案例中继续增加颜色运算，用于说明分段执行和中间纹理成本。

---

## 1. 一次绘制会把几何、颜色、覆盖与混合共同装配成完整 Shader

**理解目标**：理解原始运行时生成的单位是一次绘制所需的完整 VS/FS，而不是单个颜色效果。

**画面主体**：主线性案例的装配关系；证据形式为调用顺序与数据流关系。

**正文**：

**具体场景**

`Image Texture → Color Matrix → Alpha Threshold → SrcOver`

```text
顶点位置与插值数据 ───────────────→ Vertex Shader
                                      │
纹理采样 → 颜色矩阵 → 单阈值裁剪 ───→ Fragment Shader 输出源颜色
覆盖率 ────────────────────────────→ Fragment Shader 输出覆盖
                                      │
普通 SrcOver ──────────────────────→ 固定功能混合状态
```

**主要证据**

```text
安装几何阶段
→ 依次安装颜色与覆盖阶段
→ 安装传输语义
→ 完成 VS/FS
```

普通 SrcOver 的源因子与目标因子由运行时管线对象的固定功能混合配置承担；只有需要读取目标颜色等特殊语义时，相关逻辑才进入 Fragment Shader。

**页底结论**：在当前绘制路径内，完整 VS/FS 由全部参与阶段共同决定；普通 SrcOver 不应被描述为 Fragment Shader 尾部运算。

**讲者备注**：源码依据：`src/gpu/ProgramBuilder.cpp:41-49,52-90,102-154,203-207`、`src/gpu/ProgramInfo.cpp:302-427`。内部实现依次安装 `GeometryProcessor`、`FragmentProcessor` 与 `XferProcessor`。正文刻意省略这些类名。普通 SrcOver 优先落入 attachment blend；`emitAndInstallXferProc()` 不能被概括为所有 SrcOver 都在 shader 内计算。

---

## 2. 完整 Shader 首次出现时仍要经过后端编译并创建运行时管线对象

**理解目标**：区分 Shader 代码、后端可加载资产和真正可执行的绘制对象。

**画面主体**：三种后端的首次工作对比；证据形式为表格。

**正文**：

**具体场景**

主线性案例第一次以 RGBA8、单采样和普通 SrcOver 绘制。

**主要证据**

| 后端 | Shader 首次工作 | 可绘制对象首次工作 |
| --- | --- | --- |
| OpenGL | driver 编译 VS/FS GLSL | attach + link 形成 GL Program |
| Vulkan | 加载或创建两个 Shader Module | 结合附件、采样数等状态创建图形管线 |
| Metal | 从离线 metallib 加载 runtime library，创建 VS/FS function | 结合附件、混合与采样数创建 `MTLRenderPipelineState` |
| WebGPU | 加载 WGSL，创建两个 Shader Module | 结合附件、混合与采样数创建 Render Pipeline |

“运行时管线对象（Pipeline）”是把 VS、FS、顶点布局、附件格式、混合、采样数和深度模板等状态组合成的后端可执行对象。

**页底结论**：离线 Shader 资产可以前移代码生成和部分后端编译，但运行时仍需按当前绘制状态创建可执行对象；OpenGL 仍保留 driver compile/link。

**讲者备注**：源码依据：`src/gpu/opengl/GLGPU.cpp:217-298`、`src/gpu/vulkan/VulkanShaderModule.cpp:26-61`、`src/gpu/vulkan/VulkanRenderPipeline.cpp`、`tools/shader_build_tool/main.cpp:450-485`、`src/gpu/PrecompiledProgramCreator.cpp:34-43`、`src/gpu/metal/MetalShaderModule.mm:155-181`、`src/gpu/metal/MetalRenderPipeline.mm:27-42,94-130`。Metal 当前构建产物可包含 `.metallib`，运行时通过 `newLibraryWithData` 加载；library/function 与 `MTLRenderPipelineState` 是不同对象。WebGPU Bundle 可包含 WGSL，但 Pipeline 仍由运行时创建。

---

## 3. 缓存只能复用已经出现过的完整程序，不能消除新组合的首次成本

**理解目标**：理解常规缓存解决重复创建，而不是提前覆盖未来组合。

**画面主体**：同一完整身份的 miss/hit 对比；证据形式为状态表。

**正文**：

**具体场景**

主线性案例连续绘制两次，第二次只修改颜色矩阵系数和 Alpha Threshold 的唯一阈值。

**主要证据**

| 绘制 | 完整程序缓存键 | 结果 |
| --- | --- | --- |
| 首次绘制 | 新键 | cache miss，创建完整程序 |
| 仅修改矩阵系数与阈值 | 同键 | cache hit，更新运行时参数 |
| 增加 DeviceMask | 新键 | cache miss，资源接口已经变化 |

“完整程序缓存键（Program Key）”是一次绘制的静态 Shader 结构与影响可执行对象的状态身份；不改变代码结构和资源接口的 uniform 数值通常不进入该键。

**页底结论**：缓存只复用相同完整程序身份；新的 Shader 结构、资源接口或关键绘制状态仍会产生首次工作。

**讲者备注**：源码依据：`src/gpu/ProgramInfo.cpp:151-215,302-427`。`ProgramLookupMode` 不进入 key。矩阵和 threshold 仅在保持 uniform-only 的前提下不改变 key；若未来实现让某参数改变接口或源码结构，该边界必须重新判断。

---

## 4. AOT 复用单独 VS/FS 资产，而不穷举完整程序与运行时管线对象

**理解目标**：明确预编译资产的复用粒度和边界。

**画面主体**：多个完整程序共享单独 VS/FS 资产；证据形式为关系图。

**正文**：

**具体场景**

主线性案例分别绘制到 RGBA8 单采样目标和 MSAA 目标。

**主要证据**

```text
完整程序身份 A ─┐                    ┌─ 运行时对象 A：RGBA8 / 1×
                 ├─ 同一 VS 资产 ─┐  │
完整程序身份 B ─┘                 ├──┤
                                  │  │
共同的颜色算法 ───── 同一 FS 资产 ┘  └─ 运行时对象 B：RGBA8 / 4×

Bundle：单独 VS/FS blob + 反射信息
Runtime：VS/FS 资产 + 当前绘制状态 → 可执行对象
```

不同绘制状态仍可能形成不同完整程序，但可以共享固定 Shader 代码和有限的单阶段资产。

**页底结论**：TGFX AOT 封闭有限的 VS/FS 资产集合；Bundle 既不是全部完整程序的列表，也不是运行时可执行对象仓库。

**讲者备注**：源码依据：`src/gpu/ShaderKeyHash.cpp:36-75`、`tools/shader_build_tool/BundleWriter.h:33-64`、`src/gpu/PrecompiledProgramCreator.cpp:232-352`。内部 `PrecompiledProgramCreator` 分别查找 vertex/fragment blob，再按当前状态创建 pipeline。关系图不暗示任意两个完整程序都共享两个 stage。

---

## 5. 可预编译范围来自完整性责任，而不是来自编译器自动猜测

**理解目标**：从行业方案理解“有限集合”必须由某一方提供。

**画面主体**：三种范围来源对比；证据形式为表格。

**正文**：

**具体场景**

应用可在运行时传入任意矩阵、阈值和效果组合，但 TGFX 构建时看不到应用的完整内容清单。

**主要证据**

| 方案 | 有限范围来自哪里 | TGFX 无法直接采用的原因 |
| --- | --- | --- |
| Flutter Impeller | 引擎维护的固定绘制能力 | TGFX 不能收窄现有公开输入 |
| Skia Graphite | 调用方申报的候选组合 | TGFX 下游不提供完整清单 |
| Unreal Engine | 项目资产、平台和质量配置 | TGFX 构建时看不到下游项目上下文 |

TGFX 只能依据公开 API 语义和库内支持规则建立有限范围。

**页底结论**：在下游不改库且输入保持开放的约束下，TGFX 必须在库内定义可检查、可构建的有限能力边界。

**讲者备注**：行业参照只证明范围来源，不比较性能或覆盖率。Graphite 候选维度不等于最终 pipeline 列表；UE Shader Cook 与 PSO precache 不是同一枚举集合。

---

## 6. TGFX 用固定 Shader、有限接口版本和运行时数据构成执行基底

**理解目标**：先获得整体答案，再分别理解每层职责。

**画面主体**：三层方案映射到主案例；证据形式为关系图。

**正文**：

**具体场景**

两个互补案例说明同一方案的不同职责：线性颜色链改变矩阵和阈值；独立的普通纹理填充案例切换有/无 DeviceMask。

**主要证据**

```text
固定 Shader 代码
  负责纹理采样和颜色运算算法
          ↓
有限接口版本
  固定 sampler、binding 与阶段间接口
          ↓
运行时数据
  携带矩阵、唯一阈值和受限组合记录
```

```text
线性颜色链
  Image Texture      → 固定采样代码
  Color Matrix       → 固定运算代码 + 运行时矩阵
  Alpha Threshold    → 固定运算代码 + 运行时单阈值
  普通 SrcOver       → 固定功能混合状态

纹理填充接口扩展示例
  DeviceMask         → 增加 sampler/binding 的静态接口版本
```

**页底结论**：TGFX 不枚举开放参数，也不把所有能力塞进一个 Shader；有限性来自代码、资源接口和运行时数据的明确分工。该执行基底的覆盖范围仍需由后续规则与测试验证。

**讲者备注**：内部实现由具体 `PrecompiledShader`、有限 permutation、runtime uniforms 和有界 `Executor` 共同完成。主线性案例真实优先进入 `PointwiseTail` 路径，不得归因于 `PointwiseChain`。终态要求预编译资产独立闭合，不能依赖 JIT。

---

## 7. 完整程序、单阶段资产和运行时管线对象是三种不同身份

**理解目标**：避免把一次资产命中误解为完整绘制对象已经存在。

**画面主体**：主线性案例的三层身份表；证据形式为表格。

**正文**：

**具体场景**

同一颜色算法分别搭配不同的附件格式与采样数。

**主要证据**

| 层级 | 人话定义 | 主案例中的差异 |
| --- | --- | --- |
| 完整程序缓存键 | 一次绘制静态 Shader 结构与关键状态的完整身份 | DeviceMask、附件和采样状态都可能改变它 |
| 单阶段 Shader 资产键（Stage asset key） | 某个 VS 或 FS 预编译资产的独立身份 | 由 Shader 名称、该阶段版本号和后端规格确定 |
| 运行时管线对象 | 后端把 VS、FS 与当前绘制状态组合出的可执行对象 | 附件、混合、采样、顶点布局和深度模板参与创建 |

VS 与 FS 分别查找；命中资产后，运行时仍要结合当前绘制状态创建可执行对象。

**页底结论**：单阶段资产命中不等于完整程序缓存命中，也不等于后端可执行对象已经创建。

**讲者备注**：源码依据：`src/gpu/ProgramInfo.cpp:151-174`、`src/gpu/ShaderKeyHash.cpp:36-75`、`src/gpu/PrecompiledProgramCreator.cpp:232-352`。内部键由 shader name、单 stage permutation index 与 profile tag 计算；不得拼成虚构的完整 Program asset key。

---

## 8. 算法差异、资源接口差异和数值差异必须进入不同层级

**理解目标**：建立选择专用 Shader、静态版本或运行时参数的判据。

**画面主体**：Texture、DeviceMask、AlphaOnly 与 YUV 的分类；证据形式为代码与判定表。

**正文**：

**具体场景**

普通纹理绘制可选 DeviceMask，并可能只取 alpha；输入也可能来自 YUV 图像。

**主要证据**

```glsl
#if HAS_DEVICE_MASK
layout(binding = 1) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING 2
#else
#define XP_DST_TEX_BINDING 1
#endif

if (AlphaOnly != 0) color = vec4(color.r);
if (HasRgbaaa != 0) color = sampleRGBAAA(color);
```

| 差异 | 表达方式 | 原因 |
| --- | --- | --- |
| YUV 与普通纹理 | 专用 Shader | 采样算法和资源语义不同 |
| 是否带 DeviceMask | 静态接口版本 | 增加 sampler 并移动 binding |
| AlphaOnly、HasRgbaaa | 运行时参数 | 不改变资源接口 |

普通纹理 Shader 不承接 YUV；满足专用规则时由专用 YUV Shader 处理。

**页底结论**：算法职责决定专用 Shader，资源接口决定静态版本，接口不变的数值或离散行为保留为运行时数据。

**讲者备注**：源码依据：`src/gpu/shaders/level1/TextureFillShader.h:25-86`、`src/gpu/shaders/glsl/level1/texture_fill.frag:12-73`。内部 `TryMatchTextureFill` 对 YUV 返回不匹配，另有 `YUVTextureFillShader`；不能表述为 TextureFill 自身承接 YUV。

---

## 9. 每增加一个静态版本都会增加构建、发布、加载和验证成本

**理解目标**：把静态版本视为全生命周期预算，而不是免费分支。

**画面主体**：当前 Metal 构建报告的收敛漏斗；证据形式为数字链和成本公式。

**正文**：

**具体场景**

如果把矩阵系数或阈值静态展开，候选数量会随每个新增取值继续相乘。

**主要证据**

```text
4164 个原始 VS/FS 组合
      ↓ 支持规则筛选
 308 个可达组合
      ↓ VS/FS 分别投影
 101 个逻辑 VS + 274 个逻辑 FS
      ↓ 内容去重
  78 个唯一 VS + 250 个唯一 FS blob
```

```text
新增静态维度
→ 更多编译任务
→ 更多 Bundle 条目与包体
→ 更多加载、索引和后端模块组合
→ 更大的验证矩阵
```

这些数字来自同一份本地 Metal 构建报告快照，分别代表候选组合、可达组合、逻辑引用和唯一 blob，不是运行时对象数量。

**页底结论**：只有资源接口或代码结构必须静态固定时才值得增加版本；纯运行时参数不应展开为资产乘积。

**讲者备注**：数据源：`cmake-build-debug-metal/generated/shaders/shader_build_report.json`。快照记录 30 个声明、Metal profile、101/274 logical stages 与 78/250 unique blobs；4164/308 采用同一报告生成口径。数字可能随代码变化，二次制作前应重新读取报告，不能宣称跨版本稳定。

---

## 10. 一个覆盖全部能力的万能 Shader 会把资产问题变成接口与执行负担

**理解目标**：理解减少静态版本不等于把所有算法动态塞入同一个 Shader。

**画面主体**：全局动态化与局部数据化对比；证据形式为资源增长表。

**正文**：

**具体场景**

主线性案例只需要一张图、两个颜色运算和普通 SrcOver，却被迫携带文本、噪声、邻域采样与多纹理混合能力。

**主要证据**

| 方案 | Shader 指令与分支 | sampler/binding | 每次绘制携带的数据 |
| --- | --- | --- | --- |
| 全局万能 Shader | 随全部功能持续增长 | 必须容纳所有资源形态 | 包含大量无关选择 |
| 按算法职责拆分 | 只保留当前类别代码 | 接口版本数量有限 | 只上传当前类别数据 |

```text
纹理 | 渐变 | 噪声 | 文本 | 邻域采样 | 混合 | 遮罩 | …
                    ↓
       无法稳定预算的统一接口与分支体积
```

**页底结论**：TGFX 选择局部可泛化而非全局动态化；每个通用机制都必须有明确的算法范围、资源上限和验证边界。

**讲者备注**：概念对比不对应仓库中的单个类。内部实现以具体 `PrecompiledShader`、有限 permutation 和局部 `PointwiseChain` kernel 组合，不应把后者扩写为跨领域 Uber Shader。

---

## 11. 有界混合方案让开放参数与有限资产同时成立

**理解目标**：理解固定代码、静态接口版本和运行时记录如何协作，而非互相替代。

**画面主体**：主线性案例的职责分解；证据形式为映射表。

**正文**：

**具体场景**

线性颜色链不断改变矩阵和阈值；独立的普通纹理填充案例切换有/无 DeviceMask。

**主要证据**

| 变化 | 是否新建 Shader 算法 | 是否需要新静态接口版本 | 是否仅更新运行时数据 |
| --- | ---: | ---: | ---: |
| 矩阵系数变化 | 否 | 否 | 是 |
| Alpha Threshold 的唯一阈值变化 | 否 | 否 | 是 |
| 增加 DeviceMask | 否 | 是 | 同时绑定新纹理 |
| 改为 YUV 输入 | 是，使用专用实现 | 按专用实现规则 | 参数仍可运行时更新 |
| 普通 SrcOver | 否 | 由混合状态表达 | 随当前绘制状态创建对象 |

有限资产只固定必须稳定的代码与资源接口；开放数值继续作为数据传入。

**页底结论**：通用性来自受约束的数据化，而不是取消 Shader 边界、静态接口边界或后端对象边界。

**讲者备注**：主线性案例由 `DecomposeLinearPointwiseTail` 优先识别，内部 `PointwiseTail` 每阶段最多两个颜色运算；不能用 `PointwiseChain` 解释该主线。SrcOver 的具体承载由 XP 类型与 attachment blend 能力共同决定，正文只陈述普通 SrcOver。

---

## 12. 开发者声明稳定 Shader 源码和有限接口版本，不手写最终构建清单

**理解目标**：理解声明负责“允许哪些静态值”，支持规则负责“实际生成哪些资产”。

**画面主体**：纹理 Shader 的真实精简声明；证据形式为代码。

**正文**：

**具体场景**

普通纹理绘制需要覆盖三类终端传输接口和“有/无 DeviceMask”两种资源布局。

**主要证据**

```cpp
struct FragDims {
  enum : uint32_t { HAS_XP, HAS_DEVICE_MASK, COUNT };

  static /* finite static values */ auto values() {
    return {"HAS_XP: 3 values", "HAS_DEVICE_MASK: 2 values"};
  }
};

ShaderInfo info() const {
  return {"TextureFillShader",
          "level1/texture_fill.vert",
          "level1/texture_fill.frag",
          FragDims::values()};
}
```

- 声明给出稳定名称、VS/FS 源码和合法静态取值范围。
- 顶点资源接口不变，因此该案例只有一个顶点阶段版本号。
- 合法取值范围不是最终构建清单；不可达组合仍由支持规则排除。

**页底结论**：开发者声明可生成资产的静态边界，构建系统只生成支持规则确认可达的子集。

**讲者备注**：源码依据：`src/gpu/shaders/PrecompiledShader.h:28-60`、`src/gpu/shaders/level1/TextureFillShader.h:25-92`。真实类名 `PrecompiledShader`、`TextureFillShader` 与类型 `PermutationDomain` 可作为源码证据口述；正文代码为去内部术语后的结构化摘录，不应复制回工程。

---

## 13. 构建系统枚举支持规则，只生成真实可达的单阶段资产

**理解目标**：理解构建过程如何避免对合法取值做无条件全排列。

**画面主体**：DeviceMask 与传输接口的有限枚举；证据形式为伪代码和结果关系。

**正文**：

**具体场景**

TextureFill 允许 2 种 DeviceMask 状态和 3 种受支持传输接口，但拒绝无有效传输类型的输入。

**主要证据**

```cpp
for (deviceMask : {false, true}) {
  for (transferType : supportedTransferTypes) {
    staticValues = mapToStaticValues(deviceMask, transferType);
    fragmentVersion = encode(staticValues);
    reachableAssets.insert({0, fragmentVersion});
  }
}
```

```text
规则输入：纹理路径 + 传输接口 + DeviceMask
→ 校验支持条件
→ 编码 VS/FS 版本号
→ 分别编译、反射、去重
→ 写入独立资产池
```

颜色矩阵系数和 Alpha Threshold 的唯一阈值不进入静态枚举。

**页底结论**：构建清单来自有限支持规则，而不是来自静态取值范围的无条件笛卡尔积。

**讲者备注**：源码依据：`src/gpu/shaders/PermutationRules.cpp:749-776`、`tools/shader_build_tool/BundleWriter.h:33-64`。内部共享函数为 `ComposeTextureFill`，编码范围由 `PermutationDomain` 校验；这两个名称只作为源码定位。`xpType=-1` 会被拒绝，不得计入资产。

---

## 14. 运行时从绘制中提取同类接口需求，并定位构建期生成的相同资产

**理解目标**：理解构建期与运行时依靠共享规则闭合，而不是维护两份人工映射表。

**画面主体**：Build 与 Runtime 汇合到相同版本号；证据形式为双泳道关系。

**正文**：

**具体场景**

同一纹理绘制从“无 DeviceMask”切换为“有 DeviceMask”，运行时应定位到另一个 FS 资产，而不是近似复用原接口。

**主要证据**

```text
Build
支持规则输入
→ 共享映射
→ {vertexVersion, fragmentVersion}
→ 生成资产

Runtime
当前绘制的几何、颜色、覆盖、传输与纹理条件
→ 结构检查
→ 提取同类规则输入
→ 共享映射
→ 相同 {vertexVersion, fragmentVersion}
→ 查找资产
```

DeviceMask 从 0 变为 1 后，fragmentVersion 随资源接口变化；矩阵与阈值仍只更新运行时数据。

**页底结论**：运行时只在结构和接口满足同一支持规则时定位资产；不支持的绘制不会被强行归入一个近似版本。

**讲者备注**：源码依据：`src/gpu/PermutationMatcher.cpp:197-239`、`src/gpu/shaders/PermutationRules.cpp:749-776`。内部 `PermutationMatcher` 与 `ComposeTextureFill` 共享映射语义。TextureFill 明确拒绝 YUV，结构检查失败不得生成猜测 index。

---

## 15. 命中 VS/FS 资产后仍须按当前绘制状态创建后端可执行对象

**理解目标**：完成从资产查找到真正可绘制对象的闭环。

**画面主体**：运行时创建过程；证据形式为真实精简代码。

**正文**：

**具体场景**

主线性案例命中相同 VS/FS 资产，但绘制目标从单采样切换到 4× MSAA。

**主要证据**

```cpp
vertexAsset = bundle.findVertex(vertexAssetKey);
fragmentAsset = bundle.findFragment(fragmentAssetKey);

vertexModule = gpu->createShaderModule(vertexAsset->blob);
fragmentModule = gpu->createShaderModule(fragmentAsset->blob);

descriptor.vertex.module = vertexModule;
descriptor.fragment.module = fragmentModule;
descriptor.fragment.colorAttachments = currentAttachment;
descriptor.multisample.count = currentSampleCount;
renderObject = gpu->createRenderPipeline(descriptor);
```

反射信息恢复 uniform block 与 sampler 布局；顶点布局、附件与混合、采样数和深度模板进入运行时管线对象，裁剪区域在绘制执行时动态设置。

**页底结论**：Bundle 提供可复用的 VS/FS 输入和反射信息；后端可执行对象仍由运行时状态决定并创建。

**讲者备注**：源码依据：`src/gpu/PrecompiledProgramCreator.cpp:232-352,400-405`、`src/gpu/ProgramInfo.cpp:302-427`。内部 `PrecompiledProgramCreator` 完成查找、module 创建和 pipeline descriptor 组装。OpenGL 资产是离线 GLSL，仍由 driver compile/link；Metal metallib 先形成 runtime library/function，再创建 `MTLRenderPipelineState`。

---

## 16. TGFX 先把开放效果组合转换为可检查的采样与颜色处理关系

**理解目标**：理解开放 API 组合必须先显式化，才能判断是否可由有限资产安全执行。

**画面主体**：主线性案例从对象组合变成有向关系；证据形式为输入输出关系与拒绝条件。

**正文**：

**具体场景**

`Image Texture → Color Matrix → Alpha Threshold` 需要判断是否是逐像素线性颜色链；SrcOver 留在终端混合层。

**主要证据**

```text
几何颜色 ─→ 纹理采样 ─→ 颜色矩阵 ─→ 单阈值裁剪
                                      │
                                      └─ 输出颜色交给普通 SrcOver
```

转换后逐项检查：

1. 每个运算是否能表达为显式输入、输出和参数。
2. 颜色矩阵是否满足当前透明黑语义约束。
3. 采样方式、连接形状和数量是否在固定 Shader 能力内。
4. 整个关系是否能映射到已有预编译资产。

**页底结论**：只有语义、结构、采样和容量全部满足约束的效果组合才进入 AOT 执行；转换成功本身不等于可执行。

**讲者备注**：源码依据：`src/gpu/AOTEffectDecomposer.cpp:43-127`、`src/gpu/AOTPlanExecutor.cpp:1047-1097`。内部过程包含 `lowerToAOT`、`AOTEffectGraph`、`AOTEffectPlan`、`Planner` 与 `Executor` 能力检查。主线性案例由 `DecomposeLinearPointwiseTail` 优先识别；SrcOver 位于颜色图之外。颜色矩阵 `matrix[19] != 0` 时影响透明黑，当前路径拒绝融合。

---

## 17. 受支持的分支颜色组合可以作为运行时关系在一个 Shader 中执行

**理解目标**：理解分支拓扑可以数据化，但前提是所有节点都属于固定能力集合。

**画面主体**：双纹理分支汇入 Blend；证据形式为运行时记录表。

**正文**：

**具体场景**

`Texture A → Matrix` 与 `Texture B → Luma` 汇入 Blend，并在单 Pass 内得到结果。

**主要证据**

| 记录序号 | 运算 | 输入记录 | 运行时数据 |
| ---: | --- | --- | --- |
| 0 | Texture A | 几何颜色 | 调制标志与纹理绑定 |
| 1 | Matrix | 0 | 4×5 矩阵 |
| 2 | Texture B | 几何颜色 | 调制标志与纹理绑定 |
| 3 | Luma | 2 | 运算类型 |
| 4 | Blend | 1、3 | blend mode |

```text
Texture A ─→ Matrix ─┐
                     ├─ Blend ─→ 输出
Texture B ─→ Luma ───┘
```

固定 Shader 读取运算类型、输入序号、矩阵和 blend mode；纹理按静态绑定顺序提供，不使用虚构的运行时 sampler index 字段。

**页底结论**：在受支持节点、采样方式和容量内，不同分支拓扑可共享有限 Shader 资产；运行时完整程序仍可能因资源接口或绘制状态而不同。

**讲者备注**：源码依据：`src/gpu/processors/AOTPointwiseChainProcessor.h:28-100`、`src/gpu/AOTEffectDecomposer.cpp:275-347`、`src/gpu/AOTPlanExecutor.cpp:1047-1095`。内部 `PointwiseChain` 把节点展平为 `AOTChainSlot` 数组，texture children 按记录顺序注册，因此正文明确不写 runtime sampler index。此案例用于分支 DAG，不替代主线性案例的 `PointwiseTail` 路由。

---

## 18. 单 Pass 的开放组合受 16 条运算记录和 4 个纹理叶上限约束

**理解目标**：理解单 Pass 机制是有限能力，不是任意效果图的通用解释器。

**画面主体**：能力边界表；证据形式为源码常量与检查关系。

**正文**：

**具体场景**

双纹理 Blend 案例继续增加颜色运算或更多纹理分支，直到触及固定容量。

**主要证据**

| 检查项 | 当前能力边界 | 超界结果 |
| --- | --- | --- |
| 运算记录总数 | 最多 16 条 | 不进入该单 Pass 路径 |
| 纹理叶数量 | 最多 4 个 | 不进入该单 Pass 路径 |
| 真实纹理叶数量 | 当前执行检查接受 0、1、2 或 4；3 个被拒绝 | 非空路径使用固定四 sampler 接口，1—2 个纹理叶会填充未使用位置 |
| 需要 Shader 模拟寻址的纹理叶 | 最多 1 个 | 多于 1 个无法由共享参数表达 |
| 运算类别 | 纹理、常量色、渐变、矩阵、亮度、单阈值、颜色空间转换、Blend、矩形覆盖 | 邻域采样或外部效果不进入 |

这些上限同时受静态资源接口和运行时结构检查约束。真实纹理叶数量与资产声明的 sampler 数量不是同一口径。

**页底结论**：单 Pass 只覆盖已声明的逐像素节点与固定容量；超出边界意味着需要其他专用路径，而不是扩大为无界万能 Shader。

**讲者备注**：源码依据：`src/gpu/AOTEffect.h:37,73-100`、`src/gpu/AOTEffectDecomposer.cpp:275-347`、`src/gpu/AOTPlanExecutor.cpp:1047-1095`、`src/gpu/processors/AOTPointwiseChainProcessor.h:91-100`。内部常量为 `MaxSlots = 16`、`MaxFusedAOTSamplers = 4`；正文用“运算记录”替代 `Slot`。四个 sampler 不是任意四种纹理能力，采样模式仍受 `Executor` 检查。

---

## 19. 长线性颜色链按顺序分段执行，并用中间纹理连接多个 Pass

**理解目标**：理解主线性案例的真实执行路径、分段上限与精度代价。

**画面主体**：延长后的主线性案例分为两个 Pass；证据形式为前后对比。

**正文**：

**具体场景**

`Image → Matrix A → Alpha Threshold → Luma → Matrix B → SrcOver`

**主要证据**

```text
原始顺序
Image → Matrix A → Alpha Threshold → Luma → Matrix B → SrcOver

当前分段
Pass 0：Image + Matrix A + Alpha Threshold
        ↓ RGBA8 中间纹理
Pass 1：中间纹理 + Luma + Matrix B
        ↓
普通 SrcOver 固定功能混合
```

- 当前每阶段最多容纳两个颜色运算。
- 每个非末端阶段写入与绘制边界匹配的 RGBA8 中间纹理。
- 所有阶段在执行前必须确认能够完整使用预编译资产。
- 准备阶段无法完整使用预编译资产时，迁移期回放原始绘制。

**页底结论**：该路径只按顺序切分逐像素线性颜色链；它用额外 Pass 和 RGBA8 量化边界换取有限表达能力，终态不能依赖 JIT。

**讲者备注**：源码依据：`src/gpu/AOTEffectDecomposer.cpp:130-193`、`src/gpu/processors/AOTPointwiseTailProcessor.h:28-76`、`src/gpu/AOTPlanExecutor.cpp:950-1043,1200-1280`。内部实现名为 `PointwiseTail`，容量为两个 `Slot`。fallback 仅能描述 prepare 阶段的迁移行为；执行已开始后的失败只会返回，不能宣称“任一执行失败都原子 fallback”。

---

## 20. AOT 前移 Shader 资产成本，多 Pass 仍要支付中间纹理与额外绘制成本

**理解目标**：用同一账本同时观察构建、加载、运行时对象和 GPU 工作。

**画面主体**：单 Pass 与多 Pass 成本公式；证据形式为成本表与公式。

**正文**：

**具体场景**

比较双纹理分支单 Pass 与长线性链两 Pass。

**主要证据**

| 成本项 | AOT 单 Pass | AOT 多 Pass |
| --- | ---: | ---: |
| 构建期 Shader 编译与 Bundle 存储 | 有 | 有 |
| 运行时加载、索引和 Shader Module 创建 | 有；仅压缩 Bundle 需要解压 | 有，可能涉及多个阶段 |
| 运行时可执行对象首次创建 | 有 | 每个 Pass 都可能有 |
| 中间纹理 | 无 | 每条物化边一个 |
| GPU kernel invocation | 1 | Pass 数量 |
| render-target switch | 无额外切换 | 随中间 Pass 增加 |

若中间纹理集合为 `i`，尺寸为 `Wi × Hi`，格式为 RGBA8：

```text
临时逻辑字节 = 4Σ(WiHi)
显式逻辑读写 = 8Σ(WiHi)
```

OpenGL 即使命中离线 GLSL，运行时仍有 driver compile/link。

**页底结论**：AOT 主要前移 Shader 生成与后端编译工作；公式只描述 RGBA8 逻辑字节，不能直接等同于实测带宽、显存峰值、能耗或 GPU 时间。

**讲者备注**：统计实现：`src/gpu/AOTPlanExecutor.cpp:988-999`。内部 `PointwiseChain` 单 Pass 不产生中间物化项，`PointwiseTail` 多 Pass 产生。真实分配受尺寸对齐、资源复用、驱动策略和硬件压缩影响。Metal metallib 加载、function 获取与 runtime pipeline 创建应分别核算。

---

## 21. 源码门禁能证明特定机制存在，产品收益必须绑定实际运行报告

**理解目标**：区分测试定义、一次执行结果和产品级结论的证据强度。

**画面主体**：源码中已有门禁及其证明边界；证据形式为表格。

**正文**：

**具体场景**

评审者需要判断主线性单 Pass、线性多 Pass、资产闭合与像素一致性分别有什么证据。

**主要证据**

| 源码中存在的门禁 | 断言内容 | 若在指定环境通过，可支持 | 不能外推 |
| --- | --- | --- | --- |
| `AllRegisteredShadersAreClosed` | 期望资产数等于 Bundle 条目数，缺失数为 0 | 所加载 Bundle 对当前声明和规则闭合 | 产品输入覆盖率 |
| `AlphaThresholdChainFusesByteExact` | 无运行时源码创建、无规则 miss、无离屏目标、AOT/JIT 位图一致 | 指定 Image + Matrix + AlphaThreshold case 的单 Pass 路径 | 全部设备与全部颜色组合 |
| `LinearChainMultiPass` | 2 次执行、1 个离屏目标、RGBA8 逻辑读写与位图一致 | 指定 128×128 Matrix + Luma + Matrix case 的分段与账本 | 实测带宽、GPU 时间、能耗与真实峰值 |
| Metal 一致性 cases | 预编译路径计数与逐字节图像比较 | 列明 Metal case、设备和语料的一致性 | 其他 Backend 或产品全量语料 |

故事板只陈述源码中存在这些门禁，不宣称当前工作树已经运行通过。

**页底结论**：现有源码门禁可验证限定 case 的资产闭合、路径选择和像素结果；覆盖率与产品收益仍需记录版本、Backend、设备、语料和统计口径的运行报告。

**讲者备注**：源码依据：`test/src/AOTClosureTest.cpp:42-57`、`test/src/AOTRenderConsistencyTest.cpp:948-1044`。`AlphaThresholdChainFusesByteExact` 的阈值只有一个参数 0.25；其断言未显式检查 kernel invocation 数，只检查无 offscreen 等条件，因此正文不增加不存在的断言。`LinearChainMultiPass` 的语料是 128×128 mandrill、Matrix + Luma + Matrix。测试源码存在不等于当前机器已运行；Backend 结论必须逐 case 限定。
