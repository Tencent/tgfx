# TGFX Shader 原子化改造方案

**分支**：`feature/henryjpxie_shader_permutation`
**目标**：彻底消除 C++ 中运行时动态拼装 GLSL Shader 代码的逻辑，所有 Shader 算法代码迁移到 `.glsl` 文件中，为适配 UE RHI（不支持运行时 shader 拼装）做准备。

---

## 一、背景与现状

### 1.1 已完成（Phase I–T，详见 `tgfx_shader_modularization.md`）

- FP/XP 算法层：22 个 FP + 1 个 XP（PorterDuff 高级模式）已迁移到 `.glsl`
- FP/XP 的 `emitCode()` 空壳、`emitContainerCode` 机制、`EmitArgs`、`emitChild`、`BuilderInputProvider`、`ProcessorGuard`、`GLSLBlend.cpp`、`FragmentShaderBuilder` legacy 回调全部删除
- `.glsl` 文件通过 `embed_shaders.cmake` 自动生成 `.inc`，单源头

### 1.2 未消除的运行时 GLSL 拼装点（按严重度排序）

| # | 位置 | 性质 | 行数 | 阻塞度 |
|---|------|------|------|-------|
| 1 | **10 个 GP 的 `emitCode()`** | 嵌入 `R"GLSL(...)"` 函数体 + 运行时拼调用语句 + 运行时 `codeAppendf` | ~400 | 🔴 高 |
| 2 | **`GeometryProcessor::emitTransforms()`** | 为每个 FP coord transform 运行时生成 VS 代码 | ~25 | 🔴 高 |
| 3 | **`ShaderBuilder::appendColorGamutXform()`** | 死代码（无调用者） | 162 | ⚪ 零风险 |
| 4 | **`GLSLPorterDuffXferProcessor::buildXferCallStatement()`** | 15 个系数混合模式的表达式运行时拼接 + dst 纹理采样代码拼接 | 67 | 🟡 中 |
| 5 | 所有 FP/XP/GP 的 `buildCallStatement()` / `buildColorCallExpr()` 等 | C++ 字符串拼接函数调用（薄封装） | ~400（20+ 文件） | 🟡 中 |
| 6 | `VertexShaderBuilder::emitNormalizedPosition()` / `emitTransformedPoint()` | 固定参数化 boilerplate | ~20 | 🟢 低 |
| 7 | `FragmentShaderBuilder::emitPerspTextCoord()` | 固定参数化 boilerplate | ~10 | 🟢 低 |
| 8 | `ProgramBuilder::emitFSOutputSwizzle()` / `nameExpression()` | 固定参数化 boilerplate | ~15 | 🟢 低 |
| 9 | `ShaderBuilder::addFeature()` | `#extension` 生成 | ~5 | 🟢 低 |

---

## 二、用户决策（已确认）

| 决策点 | 选择 |
|--------|------|
| 分阶段范围 | **Phase U + V + W 全部**（~2 个月） |
| FS 算法策略 | **复杂 SDF 提取为 `.glsl` 辅助函数** |
| Coord Transform | **固定上限 + `.glsl` 内 `#if` 展开（最彻底）** |
| UE 离线导出 | **需要**（Phase W 必做） |

---

## 三、总体策略

三大阶段，由易到难，每阶段独立验证：

- **Phase U**（快速清理，~3 人日）：ColorGamutXform 死代码删除 + PorterDuff 系数模式模块化
- **Phase V**（核心重构，~4 周）：10 个 GP 全量迁移到 `.glsl` + `emitTransforms` 消除
- **Phase W**（最终收尾，~4 周）：Shader Manifest 数据化 + boilerplate 消除 + 离线变体导出工具

---

## 四、Phase U：死代码清理 + PorterDuff 模块化（~3 人日）

### Phase U-1：删除 ColorGamutXform 死代码

**验证**：`appendColorGamutXform()` 在全代码库无任何调用者。`ColorSpaceXformEffect` 已完整走 `.glsl` 模块化路径（`fragment/color_space_xform.frag.glsl`）。

**删除项**：

| 文件 | 操作 | 预计行数 |
|------|------|---------|
| `src/gpu/ShaderBuilder.cpp` | 删除 `appendColorGamutXform()` 实现（100-261 行） | -162 |
| `src/gpu/ShaderBuilder.h` | 删除声明 + `ColorSpaceXformHelper` 前向声明 | -3 |
| `src/gpu/ColorSpaceXformHelper.h` | 删除 `emitCode()` + 所有 getter（applyX、xxxUniform、srcTFType、dstTFType、isNoop） | ~-50 |
| `src/gpu/ColorSpaceXformHelper.cpp` | 删除 `emitCode()` 实现 | ~-30 |
| **合计** | | **~-245** |

保留 `ColorSpaceXformHelper::setData()` + 私有字段（`ColorSpaceXFormEffect::onSetData` 仍使用）。

**Commit 数**：1 + 1 文档

### Phase U-2：PorterDuff 系数模式模块化

**现状**：`porter_duff_xfer.frag.glsl` 已有静态 `#if TGFX_BLEND_MODE == N` 模板（modes 0-14），但 `GLSLPorterDuffXferProcessor::buildXferCallStatement()` 仍在 C++ 用 `OutputTypeExpr`/`CoeffExpr`/`OperationExpr` 三个辅助函数运行时生成表达式（~67 行）。

**目标**：C++ 只输出单行 `"vec4 out = TGFX_PorterDuff(src, cov, dst);"`，所有 blend 逻辑在 `.glsl`。

**方案**：

1. 扩展 `porter_duff_xfer.frag.glsl`：
   - 定义 `TGFX_PorterDuff(vec4 colorIn, vec4 coverage, vec4 dst) → vec4`
   - 用 `#if TGFX_BLEND_MODE == 0..14` 覆盖 15 个系数模式的 primary/secondary output + src/dst factor + operation 组合
   - 用 `#if TGFX_BLEND_MODE >= 15` 继续走 `tgfx_blend()` 高级模式
2. 重写 `GLSLPorterDuffXferProcessor::buildXferCallStatement()`：
   - 删除 `OutputTypeExpr`/`CoeffExpr`/`OperationExpr` 辅助函数
   - dst 纹理采样封装为 `.glsl` 辅助函数 `TGFX_SampleDstTexture(topLeft, scale, sampler)`
   - 只输出：
     ```
     vec4 _xpDst = TGFX_SampleDstTexture(...);  // 或者 dstColorExpr
     vec4 _xpOut = TGFX_PorterDuff(_xpColor, _xpCoverage, _xpDst);
     ```
3. 新增 `PorterDuffXferProcessor::onBuildShaderMacros()`：发射 `TGFX_BLEND_MODE=N`、`TGFX_HAS_DST_TEXTURE=0/1`、`TGFX_HAS_SECONDARY_OUTPUT=0/1`

**关键文件**：
- `src/gpu/shaders/xfer/porter_duff_xfer.frag.glsl`
- `src/gpu/glsl/processors/GLSLPorterDuffXferProcessor.cpp`
- `src/gpu/processors/PorterDuffXferProcessor.h`

**Commit 数**：2-3
**验证**：431 测试 + 截图基线（PorterDuff 覆盖 30 种 blend mode）

---

## 五、Phase V：GP 全量模块化（~4 周）

### 5.1 设计模式（参照 FP 迁移经验）

每个 GP 需要：

1. **新建 `.glsl` 文件**：`src/gpu/shaders/geometry/{gp_name}.vert.glsl`（VS 函数）
   - 函数签名：`void TGFX_{GPName}_VS(in ..., out vec2 position, ...)`
   - 用 `#ifdef TGFX_GP_xxx_COVERAGE_AA` 等宏驱动变体
2. **可选 `.frag.glsl`**（复杂 SDF 提取，按用户决策需要）
3. **基类 GP `.h` 添加 override**：
   - `shaderFunctionFile()` → 返回 `.glsl` 路径
   - `onBuildShaderMacros(ShaderMacroSet&)` → 发射变体宏
   - `buildVSCallExpr(uniforms, varyings)` → 返回 `"TGFX_XGP_VS(position, matrix, ...);"` 一行
   - `buildColorCallExpr` / `buildCoverageCallExpr`（已有基础，需改为模块化调用）
   - `declareResources()` → 注册 uniform 和 varying
4. **GLSL* 类变薄**：
   - `emitCode()` 删除（交给 `ModularProgramBuilder` 调度）
   - 保留 `setData()` 和构造函数
5. **`ModularProgramBuilder::emitAndInstallGeoProc()`** 改造：
   - 不再调用 `geometryProcessor->emitCode(args)`
   - 改为：发射 VS 模块 include → 调用 `buildVSCallExpr()` 拿到调用语句 → `codeAppend` 到 VS

### 5.2 `emitTransforms` 改造（用户决策：固定上限 + `#if` 展开）

**当前**：`emitTransforms()` 循环 N 次，每次 `codeAppendf` 一行 `varying = matrix * vec3(uv, 1);`

**新方案**：
1. Phase V-1 开始前调研当前项目最大 coord transform 数量
2. 取 2× 作为 `TGFX_MAX_COORD_XFORMS` 常量（如 8）
3. `.vert.glsl` 内写死 N 个分支块：
   ```glsl
   #ifdef TGFX_HAS_COORD_XFORM_0
   #ifdef TGFX_COORD_XFORM_0_PERSPECTIVE
     TransformedCoords_0 = CoordTransformMatrix_0 * vec3(uv, 1.0);
   #else
     TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(uv, 1.0)).xy;
   #endif
   #endif
   // ... 重复到 N-1
   ```
4. `GeometryProcessor::onBuildShaderMacros()` 根据实际 coord transform 数量发射 `TGFX_HAS_COORD_XFORM_k`（k=0..N-1）+ `TGFX_COORD_XFORM_k_PERSPECTIVE`（仅 perspective 情况）
5. C++ 的 `emitTransforms()` 被完全移除，C++ 只负责 `addUniform` 和 `addVarying`

### 5.3 分批实施

**Phase V-1：GP 模块化框架准备**（1 天）
- `ModularProgramBuilder::emitAndInstallGeoProc()` 添加分支：若 GP 有 `shaderFunctionFile()` 非空 + `buildVSCallExpr()` 返回非空 → 模块化路径，否则 fallback 到 `emitCode()`
- 调研 coord transform 最大数量，确定上限 N
- 实现 coord transform 固定上限方案的框架

**Phase V-2：简单 GP 迁移**（3 天，3 个 GP）
每 GP 独立 commit：
- `DefaultGeometryProcessor`（3 行 VS）
- `MeshGeometryProcessor`（简单矩阵变换）
- `QuadPerEdgeAAGeometryProcessor`（透传）

**Phase V-3：中等 GP 迁移**（10 天，5 个 GP）
SDF 逻辑提取为 `.glsl` 辅助函数（`TGFX_{GPName}_Coverage(...)`）：
- `EllipseGeometryProcessor`（10 行 SDF → `.frag.glsl`）
- `RoundStrokeRectGeometryProcessor`（15 行 SDF → `.frag.glsl`）
- `HairlineLineGeometryProcessor`（8 行 FS → `.frag.glsl`）
- `HairlineQuadGeometryProcessor`（15 行 + 导数 → `.frag.glsl`）
- `AtlasTextGeometryProcessor`（含纹理采样，需调整 sampler 绑定）

**Phase V-4：复杂 GP 迁移**（6 天，2 个 GP）
- `NonAARRectGeometryProcessor`（28 行 SDF → `.frag.glsl`）
- `ShapeInstancedGeometryProcessor`（双矩阵 + 实例属性）

**Phase V-5：GP Legacy 路径清理**（1 天）
1. 删除 `emitAndInstallGeoProc()` 的 `emitCode()` fallback 分支
2. `GeometryProcessor::emitCode()` 虚方法改为默认空实现（或删除）
3. 删除 `GeometryProcessor::EmitArgs` struct
4. 删除 `emitTransforms()`（已被 `.vert.glsl` + 宏完全替代）
5. 删除 10 个 `GLSL{GPName}GeometryProcessor.cpp` 中的 `emitCode()` 实现

**Commit 数**：~15（框架 1 + 迁移 10 + 清理 2 + 文档 2）
**验证**：431 测试 + 所有 GP 相关截图基线

---

## 六、Phase W：Shader Manifest + 离线导出（~4 周）

### Phase W-1：Shader Manifest 数据化（~1 周）

**问题**：当前 `buildCallStatement()` 用 C++ 字符串拼接生成 GLSL 调用语句。UE 离线编译需要结构化 shader 描述。

**方案**：引入 `ShaderCallManifest`：

```cpp
struct ShaderCallManifest {
    std::string functionName;                    // "TGFX_TextureEffect"
    std::string outputVarName;                   // "color_fp1"
    std::string outputType = "vec4";
    std::vector<std::string> argExpressions;     // ["input", "coord", "sampler_mangled"]
    std::vector<std::string> includeModules;     // [".glsl" dependencies]
    std::string preamble;                        // 额外 #define 指令
};
```

改造：
1. 所有 FP/GP/XP 的 `buildCallStatement()` 返回 `ShaderCallManifest` 而非 `ShaderCallResult`
2. `ModularProgramBuilder` 新增 `ManifestRenderer`：输入 Manifest，输出 GLSL 字符串（**确定性模板渲染，无算法分支**）
3. `ManifestRenderer` 逻辑足够简单，可被 UE 离线工具完整复现

### Phase W-2：Boilerplate 消除（~1 周）

消除所有运行时 `codeAppend`/`codeAppendf`（除 `ManifestRenderer` 外）：
- `emitNormalizedPosition()` → `.vert.glsl` 辅助函数 `TGFX_NormalizePosition(devPos, RTAdjust)`
- `emitTransformedPoint()` → `.vert.glsl` 辅助函数 `TGFX_TransformPoint` / `TGFX_TransformPointPersp`
- `emitPerspTextCoord()` → `.frag.glsl` 宏或辅助函数
- `emitFSOutputSwizzle()` → variant 宏（`TGFX_OUT_SWIZZLE_BGRA` 等）
- `nameExpression()` / `nameVariable()` → mangling 逻辑保留（不产生 shader 代码，属于元数据）

### Phase W-3：离线变体枚举器（~2 周）

**输出**：Python 脚本或独立 C++ 可执行文件，功能：
1. 枚举所有 `(GP, FP chain, XP)` 组合
2. 对每个组合：
   - 构造 ProgramInfo → 运行 `ModularProgramBuilder` → 输出完整 GLSL（VS + FS）
   - 解析 mangled uniform/sampler 名称 → 生成 binding table
3. 输出格式：
   - `.usf` 文件（UE ShaderSource 格式）
   - `variant_table.json`：variant key → `.usf` 路径 + binding table
   - 可选：直接调用 `glslangValidator` → HLSL

**集成**：
- CMake target `TGFXGenerateShaderVariants`
- UE 侧 import `variant_table.json` + `.usf`

### Phase W-4：最终清理（~3 天）

- 删除所有运行时 `codeAppend`/`codeAppendf`/`addFunction`（除确定性模板渲染外）
- 更新文档：`docs/tgfx_shader_modularization.md` 总结 + UE 集成指南
- 性能回归测试

**Commit 数**：~10

---

## 七、时间线

| Phase | 内容 | 工作量 | 累计 |
|-------|------|--------|------|
| U-1 | ColorGamutXform 死代码 | 0.5 d | 0.5 d |
| U-2 | PorterDuff 系数模式 | 2.5 d | 3 d |
| V-1 | GP 框架 + coord transform 设计 | 1 d | 4 d |
| V-2 | 3 个简单 GP | 3 d | 7 d |
| V-3 | 5 个中等 GP + FS SDF 提取 | 10 d | 17 d |
| V-4 | 2 个复杂 GP | 6 d | 23 d |
| V-5 | GP legacy 路径清理 | 1 d | 24 d |
| W-1 | Shader Manifest 数据化 | 5 d | 29 d |
| W-2 | Boilerplate 消除 | 5 d | 34 d |
| W-3 | 离线变体枚举器 | 10 d | 44 d |
| W-4 | 最终清理 | 3 d | 47 d |
| **合计** | | **~47 人日（~2.5 个月）** | |

每个 Phase 独立 commit + 验证，用户可随时暂停或调整方向。

---

## 八、关键文件清单

### 基础设施
- `src/gpu/ModularProgramBuilder.{cpp,h}`
- `src/gpu/ShaderModuleRegistry.{cpp,h}`
- `src/gpu/ShaderBuilder.{cpp,h}`
- `src/gpu/FragmentShaderBuilder.{cpp,h}`
- `src/gpu/VertexShaderBuilder.h`
- `src/gpu/glsl/GLSLVertexShaderBuilder.cpp`
- `src/gpu/glsl/GLSLFragmentShaderBuilder.cpp`
- `src/gpu/ShaderCallResult.h` → 演变为 `ShaderCallManifest.h`

### GP 相关（Phase V 核心）
- `src/gpu/processors/GeometryProcessor.{h,cpp}`
- `src/gpu/processors/*GeometryProcessor.h`（10 个）
- `src/gpu/glsl/processors/GLSL*GeometryProcessor.cpp`（10 个）
- `src/gpu/shaders/geometry/*.vert.glsl`（新建 10 个）
- `src/gpu/shaders/geometry/*.frag.glsl`（新建 2-4 个，用于复杂 SDF）

### XP 相关（Phase U-2）
- `src/gpu/processors/PorterDuffXferProcessor.h`
- `src/gpu/glsl/processors/GLSLPorterDuffXferProcessor.cpp`
- `src/gpu/shaders/xfer/porter_duff_xfer.frag.glsl`
- `src/gpu/BlendFormula.{h,cpp}`

### Color Space（Phase U-1）
- `src/gpu/ShaderBuilder.{cpp,h}`
- `src/gpu/ColorSpaceXformHelper.{h,cpp}`

### 离线工具（Phase W-3）
- 新建 `tools/shader_variant_exporter/`
- 新建 `variant_exporter.py` 或 C++ 可执行文件
- 修改根 `CMakeLists.txt` 添加 `TGFXGenerateShaderVariants` target

---

## 九、实施原则

1. **每次 commit 对应单一变更**：单 GP 迁移、单 Phase 清理分别 commit
2. **431 测试必须通过**：失败立即回滚
3. **截图基线**：GP 迁移可能引入细微像素差异，需用户 `/accept-baseline` 确认
4. **技术文档同步**：每 Phase 完成后在本文档追加"实施结果"章节
5. **不 `--amend`、不 force-push**
6. **提交前自动**：`./codeformat.sh` + 编译测试
7. **Phase U 完成后暂停**，与用户核对效果再进入 V
8. **Phase V 完成后暂停**，与用户核对是否继续进入 W（W 涉及 UE 对接，可能需调整设计）

---

## 十、风险与对策

| 风险 | 对策 |
|------|------|
| GP 迁移破坏截图 | 每 GP 独立 commit，失败独立回滚；逐批 accept baseline |
| coord transform 固定上限不够 | Phase V-1 开始前调研当前最大值，取 2× 保险值 |
| Shader Manifest 性能回归 | `ManifestRenderer` 保持简单字符串拼接，基准测试对比 |
| 离线工具与运行时 GLSL 不一致 | CI 集成：运行时 GLSL vs 工具 GLSL 做 diff 断言 |
| UE 对接方案变动 | Phase W-3 留 hook 接口，不绑死 UE 特定格式 |

---

## 十一、实施结果记录

本章节将在各 Phase 完成后填充具体 commit 列表、行数变化、验证结果。

### Phase U-1 实施结果

> 2026-04-17 完成，共 1 个 commit。

#### 改动汇总

| 文件 | 操作 | 行数变化 |
|------|------|---------|
| `src/gpu/ShaderBuilder.cpp` | 删除 `appendColorGamutXform()` 实现 | -163 |
| `src/gpu/ShaderBuilder.h` | 删除声明 + `ColorSpaceXformHelper.h` include | -3 |
| `src/gpu/ColorSpaceXformHelper.h` | 删除 `emitCode()`、`isNoop()`、TF Type/Uniform getter，把 `applyX()` 系列改为 private（仅 `setData` 内部使用） | -49 |
| `src/gpu/ColorSpaceXformHelper.cpp` | 删除 `emitCode()` 实现 | -32 |
| `src/core/shaders/GradientShader.cpp` | 补充 `#include "core/ColorSpaceXformSteps.h"`（原由 `ShaderBuilder.h` 间接传递） | +1 |
| `src/gpu/processors/ColorSpaceXFormEffect.cpp` | 补充 `#include <skcms.h>`（同上） | +1 |
| **合计** | | **-244** |

#### 完成的 Commit

| Commit | 说明 |
|--------|------|
| `698eef1a` | 删除 ColorGamutXform 死代码 |

#### 验证结果

| 构建命令 | 测试数 | 结果 |
|---------|--------|------|
| `cmake -G Ninja -DTGFX_BUILD_TESTS=ON` | 431 | 全部通过 |

#### 经验总结

- 死代码虽已确认无调用者，但其 header 通过 include 链为下游提供间接依赖。删除时需补齐下游的直接 include（2 处）。
- `ColorSpaceXformHelper::setData()` 仍在 `ColorSpaceXFormEffect::onSetData()` 中使用，保留；但 `emitCode()` 和全部 shader-generation 相关 getter 已消除。

### Phase U-2 实施结果

> 2026-04-17 完成，共 1 个 commit。

#### 改动汇总

| 文件 | 操作 | 行数变化 |
|------|------|---------|
| `src/gpu/shaders/xfer/porter_duff_xfer.frag.glsl` | 重写为 `TGFX_PorterDuffXP_Blend()` + `TGFX_PorterDuffXP_FS()` 三重载（sampler2D、sampler2DRect、无 dst 纹理），覆盖全部 30 种 blend mode 的 coverage 处理 | +112 / -79 |
| `src/gpu/glsl/processors/GLSLPorterDuffXferProcessor.cpp` | 删除 `OutputTypeExpr` / `CoeffExpr` / `OperationExpr` 三个辅助函数（~72 行）；`buildXferCallStatement()` 简化为生成单行 `TGFX_PorterDuffXP_FS(...)` 调用 | -95 |
| `src/gpu/ShaderModuleEmbedded.inc` | CMake 自动重新生成 | 同步变化 |
| **净变化** | | **-2 行（代码）/ +33 行（注释与文档）** |

#### 设计要点

1. **精确复刻 C++ `BlendFormula(hasCoverage=true)` 语义**：在 `.glsl` 中为 15 个系数模式（0-14）逐一写出考虑 coverage 的完整公式。复刻包括 C++ 原行为中 `primary=None` 时 `dstTerm = dst * (1 - 0.a) = dst` 的特殊情况（mode 2 Dst / mode 8 DstOut）。
2. **sampler2D + sampler2DRect 重载**：参照 TextureEffect 的 Phase H 改造模式。将 blending 逻辑提取为 `TGFX_PorterDuffXP_Blend()` 辅助函数，`TGFX_PorterDuffXP_FS()` 为两种 sampler 类型各提供一个重载，GLSL 在链接时自动选择。矩形纹理（sampler2DRect）的非归一化坐标由 C++ 通过 `DstTextureCoordScale=(1,1)` 编码，`.glsl` 中无需感知。
3. **C++ 仅保留调用语句拼装**：`buildXferCallStatement()` 只生成一行函数调用（含 uniform/sampler mangled 名），所有算法都在 `.glsl` 中。

#### 完成的 Commit

| Commit | 说明 |
|--------|------|
| `95d783a8` | PorterDuff 系数混合公式迁移到 .glsl，移除 3 个运行时 shader 生成辅助函数 |

#### 验证结果

| 构建命令 | 测试数 | 结果 |
|---------|--------|------|
| `cmake -G Ninja -DTGFX_BUILD_TESTS=ON` | 431 | 全部通过 |

#### 关键成果

**XferProcessor 的 C++ 运行时 shader 拼装完全消除**：
- ✅ dst 纹理采样：`.glsl` 辅助函数（两个 sampler 类型重载）
- ✅ coverage discard：`.glsl` 内
- ✅ 15 个系数模式的 coverage-aware 公式：`.glsl` 内
- ✅ 15 个高级模式 + coverage lerp：`.glsl` 内（沿用既有实现）
- ✅ C++ 只发 macros（TGFX_BLEND_MODE、TGFX_PDXP_DST_TEXTURE_READ、TGFX_PDXP_NON_COEFF）+ 单行函数调用

#### 经验总结

- 初次在 `.glsl` 中实现时漏掉了 sampler2DRect 的重载（测试 `GLRenderTest.rectangleTextureAsBlendDst` 失败），按 TextureEffect 模式添加重载后通过。
- agent 初步估计 2-3 人日，实际因需要精确复刻 BlendFormula 的 9 个系数模式公式，耗时略多。

（后续 Phase 依次追加）
