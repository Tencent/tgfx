# GP/XP Modular .glsl Migration Plan

## Goal

Migrate the 10 GP and 2 XP types from legacy `emitCode()` C++ codegen to modular `.glsl` function calls in `ModularProgramBuilder`. The VS declaration logic stays in C++ (attribute/varying/uniform declarations are structural, not shader logic); FS shader **logic** moves to `.glsl` functions.

---

## Architecture Overview

### Current State
- `ModularProgramBuilder::emitAndInstallGeoProc()` (line 234) calls `geometryProcessor->emitCode(args)` -- full legacy path for both VS and FS
- `ModularProgramBuilder::emitAndInstallXferProc()` (line 263) calls `xferProcessor->emitCode(args)` -- full legacy path
- 3 GPs (Default, QuadPerEdgeAA, Ellipse) already override `onBuildShaderMacros()` and `shaderFunctionFile()`, but do NOT yet override `buildColorCallExpr()` / `buildCoverageCallExpr()` / `buildVSCallExpr()`
- All 10 GP `.vert.glsl` + `.frag.glsl` files already exist in `src/gpu/shaders/geometry/`
- Both XP `.frag.glsl` files exist in `src/gpu/shaders/xfer/`
- `porter_duff_xfer.frag.glsl` covers modes 0-14 (coefficient); modes 15-29 (advanced) fall back to SrcOver with a TODO comment

### Key Design Decision: Split emitCode() into VS + FS Phases

GP `emitCode()` does both VS and FS work in one method. The modular approach:

1. **VS phase**: Keep in C++ `emitCode()` -- attribute emission, varying declarations, uniform declarations, vertex transform, `emitTransforms()`, `emitNormalizedPosition()`
2. **FS phase**: Replace inline `fragBuilder->codeAppendf()` calls with modular `.glsl` function calls via `buildColorCallExpr()` / `buildCoverageCallExpr()`

This means we do NOT eliminate `emitCode()` entirely. Instead:
- `emitCode()` is refactored to emit VS logic only (no FS `fragBuilder` calls)
- `ModularProgramBuilder::emitAndInstallGeoProc()` calls `emitCode()` for VS, then calls `buildColorCallExpr()` / `buildCoverageCallExpr()` for FS
- The `.frag.glsl` modules are included as function definitions; the `buildXxxCallExpr()` methods return the call expression with concrete arguments

---

## Phase 1: GP Infrastructure (ModularProgramBuilder + Base Class)

### 1.1 Modify `ModularProgramBuilder::emitAndInstallGeoProc()`

**File**: `src/gpu/ModularProgramBuilder.cpp` (lines 234-259)

Replace the current implementation that blindly calls `emitCode()`:

```
// Current:
geometryProcessor->emitCode(args);

// New:
geometryProcessor->emitCode(args);  // VS-only (after GP refactor)

// Include GP frag module
auto gpFuncFile = geometryProcessor->shaderFunctionFile();
if (!gpFuncFile.empty()) {
    auto fragModuleID = ShaderModuleRegistry::GetModuleID(gpFuncFile);
    includeModule(fragModuleID);

    // Emit GP macros
    ShaderMacroSet macros;
    geometryProcessor->onBuildShaderMacros(macros);
    if (!macros.empty()) {
        fragmentShaderBuilder()->shaderStrings[ShaderBuilder::Type::Definitions] += macros.toPreamble();
    }

    // Build mangled resources for FS call
    MangledUniforms uniforms;
    MangledVaryings varyings;
    geometryProcessor->declareGPResources(uniformHandler(), uniforms, varyings);

    // Color output
    auto colorResult = geometryProcessor->buildColorCallExpr(uniforms, varyings);
    if (!colorResult.statement.empty()) {
        fragBuilder->codeAppend(colorResult.statement);
        fragBuilder->codeAppendf("%s = %s;", outputColor->c_str(), colorResult.outputVarName.c_str());
    }

    // Coverage output
    auto coverageResult = geometryProcessor->buildCoverageCallExpr(uniforms, varyings);
    if (!coverageResult.statement.empty()) {
        fragBuilder->codeAppend(coverageResult.statement);
        fragBuilder->codeAppendf("%s = %s;", outputCoverage->c_str(), coverageResult.outputVarName.c_str());
    }
}
```

**Critical**: The `emitCode()` method must still run for VS work. The `shaderFunctionFile()` being non-empty signals "use modular FS path" vs empty means "emitCode() handles everything" (fallback).

### 1.2 Add GP `.frag.glsl` Module IDs to ShaderModuleRegistry

**File**: `src/gpu/ShaderModuleRegistry.h` -- Add enum values:

```cpp
// GP fragment modules
DefaultGP_Frag,
QuadPerEdgeAAGP_Frag,
EllipseGP_Frag,
RoundStrokeRectGP_Frag,
NonAARRectGP_Frag,
MeshGP_Frag,
ShapeInstancedGP_Frag,
AtlasTextGP_Frag,
HairlineLineGP_Frag,
HairlineQuadGP_Frag,
```

**File**: `src/gpu/ShaderModuleRegistry.cpp` -- Embed the 10 `.frag.glsl` files as `kXxxGPFrag` string constants, add to `GetModule()` switch and `kProcessorModuleMap`.

Note: Map keys use `shaderFunctionFile()` return values (e.g., `"geometry/default_geometry"`), not processor names.

### 1.3 Add `declareGPResources()` virtual method to GeometryProcessor

**File**: `src/gpu/processors/GeometryProcessor.h`

Add a new protected virtual method that declares uniforms/varyings needed by the FS `.glsl` function and populates the `MangledUniforms` / `MangledVaryings` maps. This is analogous to FP's `declareResources()`.

```cpp
virtual void declareGPResources(UniformHandler* uniformHandler,
                                MangledUniforms& uniforms,
                                MangledVaryings& varyings) const {}
```

---

## Phase 2: Migrate 3 Already-Prepared GPs

These already have `onBuildShaderMacros()` and `shaderFunctionFile()` overrides.

### 2.1 DefaultGeometryProcessor

**Files to modify**:
- `src/gpu/glsl/processors/GLSLDefaultGeometryProcessor.cpp` -- Refactor `emitCode()` to VS-only (remove lines 56-64: the FS `fragBuilder->codeAppendf` calls for outputColor and outputCoverage)
- `src/gpu/processors/DefaultGeometryProcessor.h` -- Override `buildColorCallExpr()`, `buildCoverageCallExpr()`, `declareGPResources()`

**buildColorCallExpr()** returns: `"outputColor = Color_Pn;"` (uniform reference)
**buildCoverageCallExpr()** returns: `"outputCoverage = vec4(vCoverage_Pn);"` (with AA) or `"outputCoverage = vec4(1.0);"` (without)

**Validation**: The `.frag.glsl` file (`default_geometry.frag.glsl`) already has the correct function `TGFX_DefaultGP_FS` that takes color uniform and optional vCoverage varying.

### 2.2 QuadPerEdgeAAGeometryProcessor

**Files to modify**:
- `src/gpu/glsl/processors/GLSLQuadPerEdgeAAGeometryProcessor.cpp` -- Refactor `emitCode()` to VS-only (remove FS color/coverage lines 49-63)
- `src/gpu/processors/QuadPerEdgeAAGeometryProcessor.h` -- Override `buildColorCallExpr()`, `buildCoverageCallExpr()`, `declareGPResources()`

**Complexity**: Medium. Has `commonColor` (uniform) vs per-vertex color (varying), plus coverage AA.

### 2.3 EllipseGeometryProcessor

**Files to modify**:
- `src/gpu/glsl/processors/GLSLEllipseGeometryProcessor.cpp` -- Refactor `emitCode()` to VS-only (remove FS lines 49-98)
- `src/gpu/processors/EllipseGeometryProcessor.h` -- Override `buildColorCallExpr()`, `buildCoverageCallExpr()`, `declareGPResources()`

**Complexity**: Medium-high. FS has SDF distance calculation with stroke variant. All logic already in `.frag.glsl`.

---

## Phase 3: Migrate 7 Remaining GPs

For each, the pattern is identical:
1. Refactor `GLSL*GeometryProcessor.cpp::emitCode()` to remove FS `fragBuilder` calls
2. Add `buildColorCallExpr()`, `buildCoverageCallExpr()`, `declareGPResources()` overrides in the base `*GeometryProcessor.h`
3. Register `.frag.glsl` module in ShaderModuleRegistry

### GP Migration Order (by complexity, easiest first):

| # | GP Class | Files | Complexity | Notes |
|---|----------|-------|-----------|-------|
| 1 | NonAARRectGP | `GLSLNonAARRectGeometryProcessor.cpp` | Low | Simple step() edge test |
| 2 | MeshGP | `GLSLMeshGeometryProcessor.cpp` | Low | Just passes color, no AA |
| 3 | HairlineLineGP | `GLSLHairlineLineGeometryProcessor.cpp` | Medium | Line SDF AA |
| 4 | HairlineQuadGP | `GLSLHairlineQuadGeometryProcessor.cpp` | Medium | Quad curve SDF AA |
| 5 | AtlasTextGP | `GLSLAtlasTextGeometryProcessor.cpp` | Medium | Texture sampling + color modes |
| 6 | RoundStrokeRectGP | `GLSLRoundStrokeRectGeometryProcessor.cpp` | Medium | Ellipse SDF for rounded corners |
| 7 | ShapeInstancedGP | `GLSLShapeInstancedGeometryProcessor.cpp` | Medium | Instance offset + per-instance color |

Each GP follows the same 3-file pattern:
- `src/gpu/processors/XxxGeometryProcessor.h` -- add virtual method overrides
- `src/gpu/glsl/processors/GLSLXxxGeometryProcessor.cpp` -- strip FS code from emitCode()
- `src/gpu/ShaderModuleRegistry.cpp` -- embed `.frag.glsl` content

---

## Phase 4: XP Infrastructure

### 4.1 Modify `ModularProgramBuilder::emitAndInstallXferProc()`

**File**: `src/gpu/ModularProgramBuilder.cpp` (lines 263-286)

Replace blind `emitCode()` call with modular path:

```
auto xpFuncFile = xferProcessor->shaderFunctionFile();
if (!xpFuncFile.empty()) {
    // Include XP module
    auto xpModuleID = ShaderModuleRegistry::GetModuleID(xpFuncFile);
    includeModule(xpModuleID);

    // Emit macros
    ShaderMacroSet macros;
    xferProcessor->onBuildShaderMacros(macros);
    if (!macros.empty()) {
        fragmentShaderBuilder()->shaderStrings[ShaderBuilder::Type::Definitions] += macros.toPreamble();
    }

    // Build call
    MangledUniforms uniforms;
    MangledSamplers samplers;
    // ... declare XP resources ...
    auto result = xferProcessor->buildXferCallStatement(inputColor, inputCoverage,
                                                         outputColorName, uniforms, samplers);
    fragmentShaderBuilder()->codeAppend(result.statement);
} else {
    // Legacy fallback
    xferProcessor->emitCode(args);
}
```

### 4.2 Add XP Module IDs to ShaderModuleRegistry

**File**: `src/gpu/ShaderModuleRegistry.h` -- Add:
```cpp
EmptyXP_Frag,
PorterDuffXP_Frag,
```

**File**: `src/gpu/ShaderModuleRegistry.cpp` -- Embed `empty_xfer.frag.glsl` and `porter_duff_xfer.frag.glsl` content.

---

## Phase 5: Migrate EmptyXferProcessor

**Files**:
- `src/gpu/processors/EmptyXferProcessor.h` -- Override `buildXferCallStatement()`
- `src/gpu/glsl/processors/GLSLEmptyXferProcessor.cpp` -- Keep `emitCode()` for legacy ProgramBuilder fallback

**buildXferCallStatement()** returns:
```
statement: "outputColor = inputColor * inputCoverage;"
```

Trivial -- the `.frag.glsl` function `TGFX_EmptyXP_FS` is a one-liner.

---

## Phase 6: Migrate PorterDuffXferProcessor

This is the most complex piece of the entire migration.

### 6.1 Complete `porter_duff_xfer.frag.glsl` for Advanced Blend Modes

**File**: `src/gpu/shaders/xfer/porter_duff_xfer.frag.glsl`

Currently modes 0-14 are implemented; mode 15+ falls back to SrcOver. Need to add GLSL implementations for all 15 advanced blend modes (Overlay, Darken, Lighten, ColorDodge, ColorBurn, HardLight, SoftLight, Difference, Exclusion, Multiply, Hue, Saturation, Color, Luminosity, PlusDarker).

This involves translating `GLSLBlend.cpp`'s `BlendHandler_*` functions into static GLSL. The HSL modes (Hue, Saturation, Color, Luminosity) need `luminance()`, `saturation()`, `set_luminance()`, `set_saturation()`, `set_saturation_helper()` as helper functions within the `.glsl` file.

The coefficient-based path in `GLSLBlend.cpp` (`AppendCoeffBlend`) generates dynamic GLSL based on `BlendFormula` -- this is replaced by the `#if TGFX_BLEND_MODE` compile-time dispatch already in the `.glsl` file. The `.glsl` approach is correct because `BlendMode` is a compile-time constant per shader variant.

### 6.2 Add TGFX_BLEND_MODE macro emission

**File**: `src/gpu/processors/PorterDuffXferProcessor.h`

Add to `onBuildShaderMacros()`:
```cpp
macros.define("TGFX_BLEND_MODE", static_cast<int>(blendMode));
```

### 6.3 Implement `buildXferCallStatement()`

**File**: `src/gpu/processors/PorterDuffXferProcessor.h` or a new `.cpp`

The method must:
1. If `dstTextureInfo.textureProxy` exists: generate call with sampler + uniforms (DstTextureUpperLeft, DstTextureCoordScale)
2. Otherwise: generate call with dstColor variable from `fragBuilder->dstColor()`

---

## Phase 7: Cleanup (After All Migrations Verified)

### 7.1 Remove Dead `.glsl` Files

The `.vert.glsl` files in `src/gpu/shaders/geometry/` are NOT used by the modular path (VS logic stays in C++). They serve as documentation/reference only. Decide whether to keep as documentation or remove.

### 7.2 Remove FS Code from Legacy GLSL*Processor.cpp

Once the modular path is verified, the legacy `emitCode()` methods can be simplified to VS-only. The `GLSL*Processor` subclass pattern (inheritance for `emitCode()`) could eventually be eliminated in favor of the modular virtual methods, but this is a separate refactor.

### 7.3 Remove `GLSLBlend.cpp` Usage from XP

Once `porter_duff_xfer.frag.glsl` handles all 30 modes, the `AppendMode()` call in `GLSLPorterDuffXferProcessor::emitCode()` is only needed for the legacy `ProgramBuilder` path. `GLSLBlend.cpp` remains until legacy path is fully removed.

---

## File Change Summary

### New Enum Values (ShaderModuleRegistry.h)
- 10 GP frag module IDs
- 2 XP frag module IDs

### Modified Files

| File | Change |
|------|--------|
| `src/gpu/ShaderModuleRegistry.h` | Add 12 new ShaderModuleID enum values |
| `src/gpu/ShaderModuleRegistry.cpp` | Embed 10 GP + 2 XP .glsl sources, add to maps |
| `src/gpu/processors/GeometryProcessor.h` | Add `declareGPResources()` virtual method |
| `src/gpu/ModularProgramBuilder.h` | Add GP/XP resource helpers if needed |
| `src/gpu/ModularProgramBuilder.cpp` | Rewrite `emitAndInstallGeoProc()` and `emitAndInstallXferProc()` |
| `src/gpu/processors/DefaultGeometryProcessor.h` | Add `buildColorCallExpr()`, `buildCoverageCallExpr()`, `declareGPResources()` |
| `src/gpu/processors/EllipseGeometryProcessor.h` | Same |
| `src/gpu/processors/QuadPerEdgeAAGeometryProcessor.h` | Same |
| `src/gpu/processors/RoundStrokeRectGeometryProcessor.h` | Same |
| `src/gpu/processors/NonAARRectGeometryProcessor.h` | Same |
| `src/gpu/processors/MeshGeometryProcessor.h` | Same |
| `src/gpu/processors/ShapeInstancedGeometryProcessor.h` | Same |
| `src/gpu/processors/AtlasTextGeometryProcessor.h` | Same |
| `src/gpu/processors/HairlineLineGeometryProcessor.h` | Same |
| `src/gpu/processors/HairlineQuadGeometryProcessor.h` | Same |
| `src/gpu/processors/EmptyXferProcessor.h` | Add `buildXferCallStatement()` |
| `src/gpu/processors/PorterDuffXferProcessor.h` | Add `buildXferCallStatement()`, extend `onBuildShaderMacros()` |
| `src/gpu/glsl/processors/GLSLDefaultGeometryProcessor.cpp` | Strip FS code from emitCode() |
| `src/gpu/glsl/processors/GLSLQuadPerEdgeAAGeometryProcessor.cpp` | Strip FS code from emitCode() |
| `src/gpu/glsl/processors/GLSLEllipseGeometryProcessor.cpp` | Strip FS code from emitCode() |
| `src/gpu/glsl/processors/GLSLRoundStrokeRectGeometryProcessor.cpp` | Strip FS code from emitCode() |
| `src/gpu/glsl/processors/GLSLNonAARRectGeometryProcessor.cpp` | Strip FS code from emitCode() |
| `src/gpu/glsl/processors/GLSLMeshGeometryProcessor.cpp` | Strip FS code from emitCode() |
| `src/gpu/glsl/processors/GLSLShapeInstancedGeometryProcessor.cpp` | Strip FS code from emitCode() |
| `src/gpu/glsl/processors/GLSLAtlasTextGeometryProcessor.cpp` | Strip FS code from emitCode() |
| `src/gpu/glsl/processors/GLSLHairlineLineGeometryProcessor.cpp` | Strip FS code from emitCode() |
| `src/gpu/glsl/processors/GLSLHairlineQuadGeometryProcessor.cpp` | Strip FS code from emitCode() |
| `src/gpu/shaders/xfer/porter_duff_xfer.frag.glsl` | Complete advanced blend modes 15-29 |

---

## Implementation Order

1. **Phase 1**: GP infrastructure (ModularProgramBuilder + GeometryProcessor base class changes)
2. **Phase 2**: Migrate DefaultGP (simplest, validates architecture)
3. **Test**: Compile + run all tests to verify DefaultGP modular path
4. **Phase 2 cont.**: Migrate QuadPerEdgeAAGP, EllipseGP
5. **Test**: Compile + run tests
6. **Phase 3**: Migrate remaining 7 GPs (can be batched)
7. **Test**: Full test suite
8. **Phase 4**: XP infrastructure
9. **Phase 5**: Migrate EmptyXP (trivial)
10. **Phase 6.1**: Complete `porter_duff_xfer.frag.glsl` with all 30 blend modes
11. **Phase 6.2-6.3**: Implement PorterDuffXP modular path
12. **Test**: Full test suite with blend mode verification
13. **Phase 7**: Cleanup (separate PR)

---

## Risk Areas

1. **QuadPerEdgeAAGP `onEmitTransform()` subset logic**: Lines 87-122 of `GLSLQuadPerEdgeAAGeometryProcessor.cpp` contain complex VS subset transform emission. This stays in `emitCode()` but must not break when FS is separated.

2. **PorterDuff advanced blend HSL functions**: The `set_luminance()` / `set_saturation()` helpers in `GLSLBlend.cpp` use `addFunction()` to avoid duplicate emission. In the modular `.glsl` file, these become static helpers within the file -- no dedup concern since the file is included once.

3. **emitTransforms() coupling**: GP `emitTransforms()` populates `transformedCoordVars` which FP later uses. This must continue to work correctly. Since `emitCode()` still runs for VS (which calls `emitTransforms()`), this should be fine.

4. **GP FS code that reads varyings**: The FS `.glsl` functions reference varyings by logical names (e.g., `vEllipseOffsets`). The `declareGPResources()` + `buildCoverageCallExpr()` must map these to the actual mangled varying names produced during VS emission. This is the key integration challenge.
