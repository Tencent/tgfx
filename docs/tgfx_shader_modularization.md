# tgfx Shader 模块化重构：原子化 Processor 文件 + 跨后端适配层 + 胶水代码生成

## 1. 概述与目标

### 1.1 背景

tgfx 当前的 Shader 系统采用运行时 C++ `emitCode()` 拼接完整 GLSL 字符串的方式。每个 Processor 子类（如 `GLSLTextureEffect`、`GLSLLinearGradientLayout`）在 `emitCode()` 中通过 `FragmentShaderBuilder::codeAppendf()` 逐行拼接 Shader 代码，容器 FP 通过 `emitChild()` 递归展开子 FP。`ProgramBuilder` 负责协调整个拼接过程，管理变量名 mangle、Uniform 声明和 Sampler 分配。

这种设计存在以下问题：

1. **Shader 代码不可见**：Shader 逻辑分散在几十个 C++ 文件中，无法直接阅读、调试或用 Shader 工具分析
2. **跨后端重复**：每个后端（GL、Metal、UE RHI）都需要维护一套 C++ emitCode 实现，或依赖 GLSL→SPIRV→MSL/HLSL 转换管线
3. **UE 适配困难**：UE 要求 Shader 以 `.usf` 文件形式存在并离线预编译，当前动态拼接模型需要离线枚举所有变体生成完整 GLSL，工程复杂度高
4. **可维护性差**：修改一个 FP 的 Shader 逻辑需要同时理解 C++ 拼接逻辑和最终 GLSL 输出

### 1.2 重构目标

将 Shader 系统从"运行时 C++ emitCode() 拼接完整 GLSL 字符串"重构为"预定义的 Shader 模块文件 + 运行时生成 main() 胶水代码"：

- **Shader 代码可见化**：每个叶子 FP 对应一个独立的 `.glsl` 文件，可直接阅读和调试
- **跨后端统一**：通过 Layer 0 适配层宏，同一份 Shader 源码适配 GLSL/MSL/HLSL/UE RHI
- **UE 适配简化**：模块化后，离线枚举只需生成十几行 `main()` 胶水代码，不再是完整的几百行 Shader
- **可维护性提升**：修改 FP 的 Shader 逻辑只需编辑对应的 `.glsl` 文件

### 1.3 与现有方案的关系

| 方案 | 特征 | 适用场景 |
|------|------|----------|
| **当前方案** | 运行时 C++ emitCode() 拼接完整 GLSL | GL/Metal 后端 |
| **离线枚举方案** (ue_shader_restructuring.md) | 构建时枚举变体 → 完整 GLSL → SPIRV → HLSL → .usf | UE RHI 后端 |
| **本方案（模块化重构）** | 预定义 .glsl 模块 + 运行时/离线生成 main() | 全平台统一 |

本方案完成后，离线枚举方案将大幅简化：不再需要枚举生成完整 GLSL，只需枚举生成不同的 `main()` 函数体（十几行），然后 `#include` 已有的模块文件。

### 1.4 预期收益

- **代码复用**：17 个叶子 FP 的 Shader 逻辑只写一次，所有后端共享
- **可维护性**：Shader 修改集中在 `.glsl` 文件，不需要修改 C++ 代码
- **UE 适配简化**：离线工具只需生成 main() 函数体，复杂度降低一个数量级
- **可调试性**：`.glsl` 文件可用标准 Shader 工具（RenderDoc、Nsight）直接分析

---

## 2. 三层架构总览

### 2.1 三层职责

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Layer 2: Glue Code Generation (ProgramBuilder)                        │
│   - Generates main() function body (10-20 lines)                      │
│   - Emits #include list, uniform declarations, function call chain    │
│   - Runtime for GL/Metal/Vulkan; Offline for UE RHI                   │
├─────────────────────────────────────────────────────────────────────────┤
│ Layer 1: Atomic Processor Modules (.glsl files)                       │
│   - 17 leaf FP modules (TextureEffect, LinearGradientLayout, ...)     │
│   - 10 GP vertex shader templates                                     │
│   - 2 XP modules (PorterDuff, Empty)                                  │
│   - Blend mode library (advanced_blend.glsl)                          │
│   - Each module = one function with standard signature                │
├─────────────────────────────────────────────────────────────────────────┤
│ Layer 0: Backend Adaptation Layer (macro headers)                     │
│   - tgfx_types.glsl    — type mapping (float2→vec2, half4→mediump)    │
│   - tgfx_sampling.glsl — texture sampling abstraction                 │
│   - tgfx_uniform.glsl  — uniform declaration abstraction              │
│   - tgfx_varying.glsl  — varying declaration abstraction              │
│   - Per-backend macro definitions (GL, Metal, Vulkan, UE RHI)        │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 各后端影响

| 后端 | Layer 0 头文件 | Layer 1 模块 | Layer 2 生成 |
|------|--------------|-------------|-------------|
| OpenGL ES / WebGL | `tgfx_types_glsl.glsl` | 直接 `#include` | 运行时生成 main() |
| Metal | `tgfx_types_glsl.glsl` → SPIRV → MSL（复用现有管线） | 同上 | 运行时生成 main() |
| Vulkan | `tgfx_types_glsl.glsl` + `#version 450` | 同上 | 运行时生成 main() |
| UE RHI | `tgfx_types_ue.glsl` (HLSL 宏) | `#include` 后预编译为 .usf | 离线枚举生成 main() |

### 2.3 整体数据流

```
GL/Metal/Vulkan 运行时路径:
  DrawOp::execute()
    → ProgramInfo (GP + FPs + XP)
    → ModularProgramBuilder::generateMainBody()
        → emit #include "tgfx_types_glsl.glsl"
        → emit #include for each leaf FP module
        → emit uniform declarations (numbered)
        → emit main() { GP_call(); FP_call_chain(); XP_call(); }
    → concatenate: [includes] + [uniforms] + [main()]
    → compile GLSL (or GLSL→SPIRV→MSL for Metal)
    → cache in GlobalCache

UE RHI 离线路径:
  TgfxShaderVariantGenerator (build tool)
    → enumerate all valid Processor combinations
    → ModularProgramBuilder::generateMainBody() (same code path)
    → emit .usf file: #include "tgfx_types_ue.glsl" + includes + main()
    → UE Shader Compiler Worker pre-compiles
    → runtime: ProgramKey → VariantID → TShaderMapRef
```

---

## 3. Layer 0: 后端适配层设计

### 3.1 设计原则

参考 Rive 的跨后端方案：所有 Shader 模块用中间类型名（`tgfx_float2`、`tgfx_half4` 等）编写，不同后端通过宏头文件将这些名称映射为原生类型。

### 3.2 文件结构

```
src/gpu/shaders/
├── backends/
│   ├── tgfx_types_glsl.glsl      # GLSL/GLSL ES backend
│   ├── tgfx_types_metal.glsl     # Metal backend (via SPIRV-Cross, optional)
│   ├── tgfx_types_ue.glsl        # UE RHI (HLSL) backend
│   └── tgfx_types_vulkan.glsl    # Vulkan GLSL backend
├── common/
│   ├── tgfx_sampling.glsl        # Texture sampling abstraction
│   ├── tgfx_uniform.glsl         # Uniform declaration helpers
│   ├── tgfx_varying.glsl         # Varying declaration helpers
│   └── tgfx_blend.glsl           # Blend mode functions
├── fp/                            # Fragment Processor modules
│   ├── texture_effect_rgb.glsl
│   ├── texture_effect_yuv.glsl
│   ├── ... (17 leaf FP files)
├── gp/                            # Geometry Processor templates
│   ├── quad_per_edge_aa_gp.vert.glsl
│   ├── default_gp.vert.glsl
│   ├── ... (10 GP files)
└── xp/                            # Transfer Processor modules
    ├── porter_duff_xp.glsl
    └── empty_xp.glsl
```

### 3.3 tgfx_types_glsl.glsl — GLSL 后端适配

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// Backend adaptation layer for OpenGL ES / WebGL / Desktop GL.

#ifndef TGFX_TYPES_GLSL
#define TGFX_TYPES_GLSL

// Type aliases - GLSL uses native types directly.
#define tgfx_float    float
#define tgfx_float2   vec2
#define tgfx_float3   vec3
#define tgfx_float4   vec4
#define tgfx_float2x2 mat2
#define tgfx_float3x3 mat3
#define tgfx_float4x4 mat4
#define tgfx_int      int
#define tgfx_int2     ivec2
#define tgfx_int3     ivec3
#define tgfx_int4     ivec4
#define tgfx_uint     uint
#define tgfx_bool     bool

// Precision qualifiers.
#define tgfx_highp   highp
#define tgfx_mediump mediump
#define tgfx_lowp    lowp

// Sampling macros.
#define TGFX_SAMPLE(sampler, coord) texture(sampler, coord)
#define TGFX_SAMPLER_2D sampler2D

// Math helpers.
#define TGFX_MUL(a, b)     ((a) * (b))
#define TGFX_INVERSESQRT(x) inversesqrt(x)
#define TGFX_ATAN2(y, x)   atan(y, x)
#define TGFX_MIX(a, b, t)  mix(a, b, t)
#define TGFX_SATURATE(x)   clamp(x, 0.0, 1.0)
#define TGFX_FRACT(x)      fract(x)
#define TGFX_MOD(x, y)     mod(x, y)
#define TGFX_STEP(edge, x) step(edge, x)

// Fragment position.
#define TGFX_FRAG_COORD gl_FragCoord

// Discard.
#define TGFX_DISCARD discard

#endif // TGFX_TYPES_GLSL
```

### 3.4 tgfx_types_ue.glsl — UE RHI (HLSL) 后端适配

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// Backend adaptation layer for Unreal Engine RHI (HLSL).

#ifndef TGFX_TYPES_UE
#define TGFX_TYPES_UE

// Type aliases - map to HLSL types.
#define tgfx_float    float
#define tgfx_float2   float2
#define tgfx_float3   float3
#define tgfx_float4   float4
#define tgfx_float2x2 float2x2
#define tgfx_float3x3 float3x3
#define tgfx_float4x4 float4x4
#define tgfx_int      int
#define tgfx_int2     int2
#define tgfx_int3     int3
#define tgfx_int4     int4
#define tgfx_uint     uint
#define tgfx_bool     bool

// Precision qualifiers - HLSL does not use them.
#define tgfx_highp
#define tgfx_mediump
#define tgfx_lowp

// Sampling macros - UE uses its own texture object model.
#define TGFX_SAMPLE(sampler, coord) sampler.Sample(sampler##Sampler, coord)
#define TGFX_SAMPLER_2D Texture2D

// Math helpers - HLSL equivalents.
#define TGFX_MUL(a, b)     mul(a, b)
#define TGFX_INVERSESQRT(x) rsqrt(x)
#define TGFX_ATAN2(y, x)   atan2(y, x)
#define TGFX_MIX(a, b, t)  lerp(a, b, t)
#define TGFX_SATURATE(x)   saturate(x)
#define TGFX_FRACT(x)      frac(x)
#define TGFX_MOD(x, y)     fmod(x, y)
#define TGFX_STEP(edge, x) step(edge, x)

// Fragment position.
#define TGFX_FRAG_COORD SvPosition

// Discard.
#define TGFX_DISCARD clip(-1)

#endif // TGFX_TYPES_UE
```

### 3.5 tgfx_types_vulkan.glsl — Vulkan 后端适配

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// Backend adaptation layer for Vulkan GLSL (#version 450).

#ifndef TGFX_TYPES_VULKAN
#define TGFX_TYPES_VULKAN

// Vulkan GLSL 450 uses the same types as desktop GLSL.
#define tgfx_float    float
#define tgfx_float2   vec2
#define tgfx_float3   vec3
#define tgfx_float4   vec4
#define tgfx_float2x2 mat2
#define tgfx_float3x3 mat3
#define tgfx_float4x4 mat4
#define tgfx_int      int
#define tgfx_int2     ivec2
#define tgfx_int3     ivec3
#define tgfx_int4     ivec4
#define tgfx_uint     uint
#define tgfx_bool     bool

// No precision qualifiers in Vulkan GLSL 450.
#define tgfx_highp
#define tgfx_mediump
#define tgfx_lowp

#define TGFX_SAMPLE(sampler, coord) texture(sampler, coord)
#define TGFX_SAMPLER_2D sampler2D

#define TGFX_MUL(a, b)     ((a) * (b))
#define TGFX_INVERSESQRT(x) inversesqrt(x)
#define TGFX_ATAN2(y, x)   atan(y, x)
#define TGFX_MIX(a, b, t)  mix(a, b, t)
#define TGFX_SATURATE(x)   clamp(x, 0.0, 1.0)
#define TGFX_FRACT(x)      fract(x)
#define TGFX_MOD(x, y)     mod(x, y)
#define TGFX_STEP(edge, x) step(edge, x)

#define TGFX_FRAG_COORD gl_FragCoord
#define TGFX_DISCARD discard

#endif // TGFX_TYPES_VULKAN
```

### 3.6 tgfx_sampling.glsl — 纹理采样抽象

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// Texture sampling abstraction layer.

#ifndef TGFX_SAMPLING_GLSL
#define TGFX_SAMPLING_GLSL

// Standard texture sample with coordinate clamping to a subset rect.
tgfx_float4 tgfx_sampleTexture(TGFX_SAMPLER_2D tex, tgfx_float2 coord) {
    return TGFX_SAMPLE(tex, coord);
}

// Sample with subset clamping. subset = vec4(left, top, right, bottom).
tgfx_float4 tgfx_sampleTextureSubset(TGFX_SAMPLER_2D tex, tgfx_float2 coord,
                                       tgfx_float4 subset) {
    tgfx_float2 clamped = clamp(coord, subset.xy, subset.zw);
    return TGFX_SAMPLE(tex, clamped);
}

// Sample with both extra subset and inner subset clamping.
tgfx_float4 tgfx_sampleTextureDualSubset(TGFX_SAMPLER_2D tex, tgfx_float2 coord,
                                           tgfx_float4 subset, tgfx_float4 extraSubset) {
    tgfx_float2 clamped = clamp(coord, extraSubset.xy, extraSubset.zw);
    clamped = clamp(clamped, subset.xy, subset.zw);
    return TGFX_SAMPLE(tex, clamped);
}

// Resolve perspective-correct texture coordinate from a varying.
// If the varying has a .z component for perspective, divides by it.
tgfx_float2 tgfx_perspCoord(tgfx_float3 coordWithW) {
    return coordWithW.xy / coordWithW.z;
}

#endif // TGFX_SAMPLING_GLSL
```

### 3.7 tgfx_uniform.glsl — Uniform 声明辅助

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// Uniform declaration helpers.
//
// In modular mode, uniforms are passed as function parameters rather than global
// declarations. This file provides utility macros for the ProgramBuilder to generate
// uniform block declarations in the main() preamble.

#ifndef TGFX_UNIFORM_GLSL
#define TGFX_UNIFORM_GLSL

// Uniform block layout matches current tgfx convention:
//   VertexUniformBlock   -> binding 0
//   FragmentUniformBlock -> binding 1
//   Samplers             -> binding 2+

// These macros are used by the generated main() code, not by FP modules.
// FP modules receive uniforms as function parameters.

#endif // TGFX_UNIFORM_GLSL
```

### 3.8 tgfx_varying.glsl — Varying 声明辅助

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// Varying declaration helpers.

#ifndef TGFX_VARYING_GLSL
#define TGFX_VARYING_GLSL

// Varying qualifiers.
#ifdef TGFX_VERTEX_SHADER
    #define TGFX_VARYING_OUT(type, name) out type name
#endif

#ifdef TGFX_FRAGMENT_SHADER
    #define TGFX_VARYING_IN(type, name) in type name
#endif

// Flat qualifier for integer varyings.
#ifdef TGFX_VERTEX_SHADER
    #define TGFX_FLAT_VARYING_OUT(type, name) flat out type name
#endif

#ifdef TGFX_FRAGMENT_SHADER
    #define TGFX_FLAT_VARYING_IN(type, name) flat in type name
#endif

#endif // TGFX_VARYING_GLSL
```

### 3.9 每个后端需要提供的宏定义集

| 宏名 | GLSL ES | Vulkan GLSL | HLSL (UE) |
|------|---------|-------------|-----------|
| `tgfx_float4` | `vec4` | `vec4` | `float4` |
| `tgfx_float3x3` | `mat3` | `mat3` | `float3x3` |
| `TGFX_SAMPLE(s,c)` | `texture(s,c)` | `texture(s,c)` | `s.Sample(sSampler,c)` |
| `TGFX_MUL(a,b)` | `(a)*(b)` | `(a)*(b)` | `mul(a,b)` |
| `TGFX_FRAG_COORD` | `gl_FragCoord` | `gl_FragCoord` | `SvPosition` |
| `tgfx_highp` | `highp` | (empty) | (empty) |
| `TGFX_DISCARD` | `discard` | `discard` | `clip(-1)` |

---

## 4. Layer 1: 叶子 FP 原子化

### 4.1 函数签名约定

每个叶子 FP 模块定义一个纯函数，签名遵循以下约定：

```glsl
// Returns the output color.
// inputColor: the color flowing into this FP (may be vec4(1.0) if no input).
// coord:      the transformed texture coordinate for this FP (if applicable).
// Uniforms:   passed as individual parameters, named u_<UniformName>.
// Samplers:   passed as parameters, named s_<SamplerName>.
tgfx_float4 FP_<ProcessorName>(tgfx_float4 inputColor, ...processor-specific params...);
```

- **返回值**：输出颜色 `vec4`，取代当前的 `outputColor` 变量赋值
- **inputColor**：上游颜色输入，对应当前 `args.inputColor`
- **coord**：经过 CoordTransform 变换后的纹理坐标（从 varying 传入）
- **Uniform 参数**：以 `u_` 前缀命名，按声明顺序传入
- **Sampler 参数**：以 `s_` 前缀命名

### 4.2 完整的 17 个叶子 FP 函数签名

```glsl
// 1. ConstColorProcessor
//    Variants: Ignore, ModulateRGBA, ModulateA (selected by #define)
tgfx_float4 FP_ConstColor(tgfx_float4 inputColor, tgfx_float4 u_Color);

// 2. TextureEffect (RGB variant)
//    Variants: hasAlphaStart, isAlphaOnly, hasSubset, isStrict, hasPerspective
tgfx_float4 FP_TextureEffect_RGB(tgfx_float4 inputColor, tgfx_float2 coord,
                                   TGFX_SAMPLER_2D s_Texture, tgfx_float4 u_Subset,
                                   tgfx_float2 u_AlphaStart);

// 3. TextureEffect (YUV I420 variant)
tgfx_float4 FP_TextureEffect_YUV_I420(tgfx_float4 inputColor, tgfx_float2 coord,
                                        TGFX_SAMPLER_2D s_TextureY,
                                        TGFX_SAMPLER_2D s_TextureU,
                                        TGFX_SAMPLER_2D s_TextureV,
                                        tgfx_float4 u_Subset,
                                        tgfx_float3x3 u_Mat3ColorConversion,
                                        tgfx_float2 u_AlphaStart);

// 4. TextureEffect (YUV NV12 variant)
tgfx_float4 FP_TextureEffect_YUV_NV12(tgfx_float4 inputColor, tgfx_float2 coord,
                                        TGFX_SAMPLER_2D s_TextureY,
                                        TGFX_SAMPLER_2D s_TextureUV,
                                        tgfx_float4 u_Subset,
                                        tgfx_float3x3 u_Mat3ColorConversion,
                                        tgfx_float2 u_AlphaStart);

// 5. TiledTextureEffect
//    Complex variants based on ShaderMode; handled by #ifdef variants.
tgfx_float4 FP_TiledTextureEffect(tgfx_float4 inputColor, tgfx_float2 coord,
                                    TGFX_SAMPLER_2D s_Texture, tgfx_float4 u_Subset,
                                    tgfx_float4 u_Clamp, tgfx_float2 u_Dimension);

// 6. DeviceSpaceTextureEffect
tgfx_float4 FP_DeviceSpaceTextureEffect(tgfx_float4 inputColor,
                                          TGFX_SAMPLER_2D s_Texture,
                                          tgfx_float3x3 u_DeviceCoordMatrix);

// 7. LinearGradientLayout
tgfx_float4 FP_LinearGradientLayout(tgfx_float2 coord);

// 8. RadialGradientLayout
tgfx_float4 FP_RadialGradientLayout(tgfx_float2 coord);

// 9. ConicGradientLayout
tgfx_float4 FP_ConicGradientLayout(tgfx_float2 coord, tgfx_float u_Bias,
                                     tgfx_float u_Scale);

// 10. DiamondGradientLayout
tgfx_float4 FP_DiamondGradientLayout(tgfx_float2 coord);

// 11. SingleIntervalGradientColorizer
tgfx_float4 FP_SingleIntervalGradientColorizer(tgfx_float4 inputColor,
                                                 tgfx_float4 u_Start, tgfx_float4 u_End);

// 12. DualIntervalGradientColorizer
tgfx_float4 FP_DualIntervalGradientColorizer(tgfx_float4 inputColor,
                                               tgfx_float4 u_Scale01,
                                               tgfx_float4 u_Bias01,
                                               tgfx_float4 u_Scale23,
                                               tgfx_float4 u_Bias23,
                                               tgfx_float u_Threshold);

// 13. TextureGradientColorizer
tgfx_float4 FP_TextureGradientColorizer(tgfx_float4 inputColor,
                                          TGFX_SAMPLER_2D s_GradientTexture);

// 14. UnrolledBinaryGradientColorizer
//     intervalCount determines which uniforms are active (handled by #define).
tgfx_float4 FP_UnrolledBinaryGradientColorizer(tgfx_float4 inputColor,
                                                 tgfx_float4 u_Thresholds1_7,
                                                 tgfx_float4 u_Thresholds9_13,
                                                 tgfx_float4 u_Scale0_1,
                                                 tgfx_float4 u_Bias0_1,
                                                 tgfx_float4 u_Scale2_3,
                                                 tgfx_float4 u_Bias2_3,
                                                 tgfx_float4 u_Scale4_5,
                                                 tgfx_float4 u_Bias4_5,
                                                 tgfx_float4 u_Scale6_7,
                                                 tgfx_float4 u_Bias6_7);

// 15. ColorMatrixFragmentProcessor
tgfx_float4 FP_ColorMatrix(tgfx_float4 inputColor, tgfx_float4x4 u_Matrix,
                             tgfx_float4 u_Vector);

// 16. LumaFragmentProcessor
tgfx_float4 FP_Luma(tgfx_float4 inputColor, tgfx_float u_Kr, tgfx_float u_Kg,
                      tgfx_float u_Kb);

// 17. AlphaThresholdFragmentProcessor
tgfx_float4 FP_AlphaThreshold(tgfx_float4 inputColor, tgfx_float u_Threshold);

// 18. AARectEffect (coverage FP)
tgfx_float4 FP_AARectEffect(tgfx_float4 inputColor, tgfx_float4 u_Rect);

// 19. ColorSpaceXformEffect
tgfx_float4 FP_ColorSpaceXform(tgfx_float4 inputColor, tgfx_float4x4 u_XformMatrix);
```

### 4.3 参数约定详解

**Uniform 参数**：每个 Uniform 以 `u_` 前缀传入函数。在生成的 `main()` 中，这些参数从 FragmentUniformBlock 的成员中读取。命名不再需要 mangle 后缀，因为每个 FP 函数有独立作用域。

**Sampler 参数**：Sampler 以 `s_` 前缀传入。在 `main()` 中，ProgramBuilder 生成对应的全局 `uniform sampler2D` 声明，并在函数调用时传入。

**CoordTransform 参数**：经过 GP 在 VS 中计算后通过 varying 传入 FS。在 `main()` 中作为局部变量读取，传入 FP 函数的 `coord` 参数。

### 4.4 computeProcessorKey 到函数变体选择的映射

当前的 `computeProcessorKey` 写入 `BytesKey` 的 flags 决定 Shader 代码变体。在模块化方案中，这些 flags 映射为 `#define` 宏，在 `#include` FP 模块之前设置：

```glsl
// Example: TextureEffect flags
// Bit 0: isRGBAAA (alphaStart == Point::Zero())
// Bit 1: isAlphaOnly
// Bit 2: isI420 (vs NV12)
// Bit 3: isLimitedRange
// Bit 4: hasSubset
// Bit 5: isStrict
// Bit 6: hasPerspective

// Generated by ProgramBuilder before #include:
#define TGFX_TEXTURE_EFFECT_IS_ALPHA_ONLY 0
#define TGFX_TEXTURE_EFFECT_HAS_SUBSET 1
#define TGFX_TEXTURE_EFFECT_IS_STRICT 0
#define TGFX_TEXTURE_EFFECT_HAS_PERSPECTIVE 0
#include "fp/texture_effect_rgb.glsl"
```

### 4.5 完整代码示例

#### 4.5.1 fp/const_color.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// ConstColorProcessor module.
// Variants controlled by TGFX_CONST_COLOR_INPUT_MODE:
//   0 = Ignore (output = u_Color)
//   1 = ModulateRGBA (output = u_Color * inputColor)
//   2 = ModulateA (output = u_Color * inputColor.a)

#ifndef FP_CONST_COLOR_GLSL
#define FP_CONST_COLOR_GLSL

tgfx_float4 FP_ConstColor(tgfx_float4 inputColor, tgfx_float4 u_Color) {
    tgfx_float4 result = u_Color;
#if TGFX_CONST_COLOR_INPUT_MODE == 1
    result *= inputColor;
#elif TGFX_CONST_COLOR_INPUT_MODE == 2
    result *= inputColor.a;
#endif
    return result;
}

#endif // FP_CONST_COLOR_GLSL
```

#### 4.5.2 fp/texture_effect_rgb.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// TextureEffect RGB module.
// Variants:
//   TGFX_TEXTURE_EFFECT_IS_ALPHA_ONLY  — 0 or 1
//   TGFX_TEXTURE_EFFECT_HAS_SUBSET     — 0 or 1
//   TGFX_TEXTURE_EFFECT_IS_STRICT      — 0 or 1
//   TGFX_TEXTURE_EFFECT_HAS_ALPHA_START — 0 or 1
//   TGFX_TEXTURE_EFFECT_HAS_PERSPECTIVE — 0 or 1

#ifndef FP_TEXTURE_EFFECT_RGB_GLSL
#define FP_TEXTURE_EFFECT_RGB_GLSL

tgfx_float4 FP_TextureEffect_RGB(tgfx_float4 inputColor, tgfx_float2 coord,
                                   TGFX_SAMPLER_2D s_Texture
#if TGFX_TEXTURE_EFFECT_HAS_SUBSET
                                   , tgfx_float4 u_Subset
#endif
#if TGFX_TEXTURE_EFFECT_IS_STRICT
                                   , tgfx_float4 u_ExtraSubset
#endif
#if TGFX_TEXTURE_EFFECT_HAS_ALPHA_START
                                   , tgfx_float2 u_AlphaStart
#endif
                                   ) {
    tgfx_highp tgfx_float2 finalCoord = coord;

#if TGFX_TEXTURE_EFFECT_IS_STRICT
    finalCoord = clamp(finalCoord, u_ExtraSubset.xy, u_ExtraSubset.zw);
#endif

#if TGFX_TEXTURE_EFFECT_HAS_SUBSET
    finalCoord = clamp(finalCoord, u_Subset.xy, u_Subset.zw);
#endif

    tgfx_float4 color = TGFX_SAMPLE(s_Texture, finalCoord);

#if TGFX_TEXTURE_EFFECT_HAS_ALPHA_START
    color = TGFX_SATURATE(color);
    tgfx_float2 alphaCoord = finalCoord + u_AlphaStart;
    tgfx_float4 alpha = TGFX_SAMPLE(s_Texture, alphaCoord);
    alpha = TGFX_SATURATE(alpha);
    color = tgfx_float4(color.rgb * alpha.r, alpha.r);
#endif

#if TGFX_TEXTURE_EFFECT_IS_ALPHA_ONLY
    tgfx_float4 result = color.a * inputColor;
#else
    tgfx_float4 result = color * inputColor.a;
#endif

    return result;
}

#endif // FP_TEXTURE_EFFECT_RGB_GLSL
```

#### 4.5.3 fp/linear_gradient_layout.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// LinearGradientLayout module.

#ifndef FP_LINEAR_GRADIENT_LAYOUT_GLSL
#define FP_LINEAR_GRADIENT_LAYOUT_GLSL

tgfx_float4 FP_LinearGradientLayout(tgfx_float2 coord) {
    tgfx_float t = coord.x + 1.0000000000000001e-05;
    return tgfx_float4(t, 1.0, 0.0, 0.0);
}

#endif // FP_LINEAR_GRADIENT_LAYOUT_GLSL
```

#### 4.5.4 fp/single_interval_gradient_colorizer.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// SingleIntervalGradientColorizer module.

#ifndef FP_SINGLE_INTERVAL_GRADIENT_COLORIZER_GLSL
#define FP_SINGLE_INTERVAL_GRADIENT_COLORIZER_GLSL

tgfx_float4 FP_SingleIntervalGradientColorizer(tgfx_float4 inputColor,
                                                 tgfx_float4 u_Start,
                                                 tgfx_float4 u_End) {
    tgfx_float t = inputColor.x;
    return (1.0 - t) * u_Start + t * u_End;
}

#endif // FP_SINGLE_INTERVAL_GRADIENT_COLORIZER_GLSL
```

#### 4.5.5 fp/color_matrix.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// ColorMatrixFragmentProcessor module.

#ifndef FP_COLOR_MATRIX_GLSL
#define FP_COLOR_MATRIX_GLSL

tgfx_float4 FP_ColorMatrix(tgfx_float4 inputColor, tgfx_float4x4 u_Matrix,
                             tgfx_float4 u_Vector) {
    tgfx_float4 unpremul = tgfx_float4(inputColor.rgb / max(inputColor.a, 9.9999997473787516e-05),
                                         inputColor.a);
    tgfx_float4 result = TGFX_MUL(u_Matrix, unpremul) + u_Vector;
    result = TGFX_SATURATE(result);
    result.rgb *= result.a;
    return result;
}

#endif // FP_COLOR_MATRIX_GLSL
```

#### 4.5.6 fp/aa_rect_effect.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// AARectEffect module (coverage FP).

#ifndef FP_AA_RECT_EFFECT_GLSL
#define FP_AA_RECT_EFFECT_GLSL

tgfx_float4 FP_AARectEffect(tgfx_float4 inputColor, tgfx_float4 u_Rect) {
    tgfx_float4 dists4 = TGFX_SATURATE(tgfx_float4(1.0, 1.0, -1.0, -1.0) *
                                         tgfx_float4(TGFX_FRAG_COORD.xyxy - u_Rect));
    tgfx_float2 dists2 = dists4.xy + dists4.zw - 1.0;
    tgfx_float coverage = dists2.x * dists2.y;
    return inputColor * coverage;
}

#endif // FP_AA_RECT_EFFECT_GLSL
```

#### 4.5.7 fp/luma.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// LumaFragmentProcessor module.

#ifndef FP_LUMA_GLSL
#define FP_LUMA_GLSL

tgfx_float4 FP_Luma(tgfx_float4 inputColor, tgfx_float u_Kr, tgfx_float u_Kg,
                      tgfx_float u_Kb) {
    tgfx_float luma = dot(inputColor.rgb, tgfx_float3(u_Kr, u_Kg, u_Kb));
    return tgfx_float4(luma, luma, luma, luma);
}

#endif // FP_LUMA_GLSL
```

#### 4.5.8 fp/alpha_threshold.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// AlphaThresholdFragmentProcessor module.

#ifndef FP_ALPHA_THRESHOLD_GLSL
#define FP_ALPHA_THRESHOLD_GLSL

tgfx_float4 FP_AlphaThreshold(tgfx_float4 inputColor, tgfx_float u_Threshold) {
    tgfx_float4 result = tgfx_float4(0.0);
    if (inputColor.a > 0.0) {
        result.rgb = inputColor.rgb / inputColor.a;
        result.a = TGFX_STEP(u_Threshold, inputColor.a);
        result = TGFX_SATURATE(result);
    }
    return result;
}

#endif // FP_ALPHA_THRESHOLD_GLSL
```

#### 4.5.9 fp/radial_gradient_layout.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// RadialGradientLayout module.

#ifndef FP_RADIAL_GRADIENT_LAYOUT_GLSL
#define FP_RADIAL_GRADIENT_LAYOUT_GLSL

tgfx_float4 FP_RadialGradientLayout(tgfx_float2 coord) {
    tgfx_float t = length(coord);
    return tgfx_float4(t, 1.0, 0.0, 0.0);
}

#endif // FP_RADIAL_GRADIENT_LAYOUT_GLSL
```

#### 4.5.10 fp/conic_gradient_layout.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// ConicGradientLayout module.

#ifndef FP_CONIC_GRADIENT_LAYOUT_GLSL
#define FP_CONIC_GRADIENT_LAYOUT_GLSL

tgfx_float4 FP_ConicGradientLayout(tgfx_float2 coord, tgfx_float u_Bias,
                                     tgfx_float u_Scale) {
    tgfx_float angle = TGFX_ATAN2(-coord.y, -coord.x);
    tgfx_float t = ((angle * 0.15915494309180001 + 0.5) + u_Bias) * u_Scale;
    return tgfx_float4(t, 1.0, 0.0, 0.0);
}

#endif // FP_CONIC_GRADIENT_LAYOUT_GLSL
```

#### 4.5.11 fp/diamond_gradient_layout.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// DiamondGradientLayout module.

#ifndef FP_DIAMOND_GRADIENT_LAYOUT_GLSL
#define FP_DIAMOND_GRADIENT_LAYOUT_GLSL

tgfx_float4 FP_DiamondGradientLayout(tgfx_float2 coord) {
    tgfx_float t = max(abs(coord.x), abs(coord.y));
    return tgfx_float4(t, 1.0, 0.0, 0.0);
}

#endif // FP_DIAMOND_GRADIENT_LAYOUT_GLSL
```

#### 4.5.12 fp/dual_interval_gradient_colorizer.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// DualIntervalGradientColorizer module.

#ifndef FP_DUAL_INTERVAL_GRADIENT_COLORIZER_GLSL
#define FP_DUAL_INTERVAL_GRADIENT_COLORIZER_GLSL

tgfx_float4 FP_DualIntervalGradientColorizer(tgfx_float4 inputColor,
                                               tgfx_float4 u_Scale01,
                                               tgfx_float4 u_Bias01,
                                               tgfx_float4 u_Scale23,
                                               tgfx_float4 u_Bias23,
                                               tgfx_float u_Threshold) {
    tgfx_float t = inputColor.x;
    tgfx_float4 scale;
    tgfx_float4 bias;
    if (t < u_Threshold) {
        scale = u_Scale01;
        bias = u_Bias01;
    } else {
        scale = u_Scale23;
        bias = u_Bias23;
    }
    return tgfx_float4(t * scale + bias);
}

#endif // FP_DUAL_INTERVAL_GRADIENT_COLORIZER_GLSL
```

#### 4.5.13 fp/texture_gradient_colorizer.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// TextureGradientColorizer module.

#ifndef FP_TEXTURE_GRADIENT_COLORIZER_GLSL
#define FP_TEXTURE_GRADIENT_COLORIZER_GLSL

tgfx_float4 FP_TextureGradientColorizer(tgfx_float4 inputColor,
                                          TGFX_SAMPLER_2D s_GradientTexture) {
    tgfx_float2 coord = tgfx_float2(inputColor.x, 0.5);
    return TGFX_SAMPLE(s_GradientTexture, coord);
}

#endif // FP_TEXTURE_GRADIENT_COLORIZER_GLSL
```

#### 4.5.14 fp/device_space_texture_effect.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// DeviceSpaceTextureEffect module.
// Variants:
//   TGFX_DEVICE_SPACE_TEXTURE_IS_ALPHA_ONLY — 0 or 1

#ifndef FP_DEVICE_SPACE_TEXTURE_EFFECT_GLSL
#define FP_DEVICE_SPACE_TEXTURE_EFFECT_GLSL

tgfx_float4 FP_DeviceSpaceTextureEffect(tgfx_float4 inputColor,
                                          TGFX_SAMPLER_2D s_Texture,
                                          tgfx_float3x3 u_DeviceCoordMatrix) {
    tgfx_float3 deviceCoord = TGFX_MUL(u_DeviceCoordMatrix,
                                         tgfx_float3(TGFX_FRAG_COORD.xy, 1.0));
    tgfx_float4 color = TGFX_SAMPLE(s_Texture, deviceCoord.xy);
#if TGFX_DEVICE_SPACE_TEXTURE_IS_ALPHA_ONLY
    return color.a * inputColor;
#else
    return color * inputColor.a;
#endif
}

#endif // FP_DEVICE_SPACE_TEXTURE_EFFECT_GLSL
```

---

## 5. GP 模板化

### 5.1 设计思路

每种 GP 对应一个 `.vert.glsl` 模板文件，定义 VS 的核心逻辑。VS 模板接收 attribute 输入，计算 varying 输出（包括 CoordTransform 结果），并输出归一化的顶点位置。

GP 的 Varying 输出 Naming 约定：
- `v_Color` — 顶点颜色
- `v_Coverage` — AA 覆盖率
- `v_TexCoord_N` — 第 N 个 CoordTransform 的结果
- `v_TexSubset` — Subset 区域（QuadPerEdgeAAGP 特有）

### 5.2 完整代码示例

#### 5.2.1 gp/quad_per_edge_aa_gp.vert.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// QuadPerEdgeAAGeometryProcessor vertex shader template.
// Variants:
//   TGFX_GP_HAS_AA_COVERAGE   — 0 or 1
//   TGFX_GP_HAS_COMMON_COLOR  — 0 or 1
//   TGFX_GP_HAS_UV_COORD      — 0 or 1
//   TGFX_GP_HAS_SUBSET        — 0 or 1
//   TGFX_GP_NUM_COORD_TRANSFORMS — integer (number of FP coord transforms)

#ifndef GP_QUAD_PER_EDGE_AA_VERT_GLSL
#define GP_QUAD_PER_EDGE_AA_VERT_GLSL

// Attributes (emitted by ProgramBuilder based on GP configuration).
// in vec2 position;
// in vec4 color;        (if !HAS_COMMON_COLOR)
// in float coverage;    (if HAS_AA_COVERAGE)
// in vec2 uvCoord;      (if HAS_UV_COORD)
// in vec4 subset;       (if HAS_SUBSET)

// Uniforms.
// uniform vec4 RTAdjust;
// uniform vec4 Color;   (if HAS_COMMON_COLOR, in fragment uniform block)
// uniform mat3 CoordTransform_N; (one per coord transform)
// uniform mat3 texSubsetMatrix;  (if HAS_SUBSET && !HAS_UV_COORD)

// This function is called from the generated main().
void GP_QuadPerEdgeAA_VS(in tgfx_float2 a_Position,
#if !TGFX_GP_HAS_COMMON_COLOR
                          in tgfx_float4 a_Color,
#endif
#if TGFX_GP_HAS_AA_COVERAGE
                          in tgfx_float a_Coverage,
#endif
#if TGFX_GP_HAS_UV_COORD
                          in tgfx_float2 a_UVCoord,
#endif
#if TGFX_GP_HAS_SUBSET
                          in tgfx_float4 a_Subset,
#endif
                          tgfx_float4 u_RTAdjust
                          // CoordTransform uniforms passed by generated code
                          ) {
    // Position output.
    tgfx_float2 pos = a_Position;
    gl_Position = tgfx_float4(pos.x * u_RTAdjust.x + u_RTAdjust.y,
                               pos.y * u_RTAdjust.z + u_RTAdjust.w,
                               0.0, 1.0);

#if TGFX_GP_HAS_AA_COVERAGE
    v_Coverage = a_Coverage;
#endif

#if !TGFX_GP_HAS_COMMON_COLOR
    v_Color = a_Color;
#endif

    // CoordTransform outputs are generated by ProgramBuilder.
    // Each transform: v_TexCoord_N = (u_CoordTransform_N * vec3(srcCoord, 1.0)).xy;
}

#endif // GP_QUAD_PER_EDGE_AA_VERT_GLSL
```

#### 5.2.2 gp/default_gp.vert.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// DefaultGeometryProcessor vertex shader template.
// Variants:
//   TGFX_GP_HAS_AA_COVERAGE — 0 or 1

#ifndef GP_DEFAULT_VERT_GLSL
#define GP_DEFAULT_VERT_GLSL

void GP_Default_VS(in tgfx_float2 a_Position,
#if TGFX_GP_HAS_AA_COVERAGE
                    in tgfx_float a_Coverage,
#endif
                    tgfx_float4 u_RTAdjust,
                    tgfx_float3x3 u_Matrix
                    ) {
    tgfx_highp tgfx_float2 pos = TGFX_MUL(u_Matrix, tgfx_float3(a_Position, 1.0)).xy;
    gl_Position = tgfx_float4(pos.x * u_RTAdjust.x + u_RTAdjust.y,
                               pos.y * u_RTAdjust.z + u_RTAdjust.w,
                               0.0, 1.0);

#if TGFX_GP_HAS_AA_COVERAGE
    v_Coverage = a_Coverage;
#endif

    // CoordTransform outputs generated by ProgramBuilder.
}

#endif // GP_DEFAULT_VERT_GLSL
```

### 5.3 Attribute 声明方式

Attribute 声明由 ProgramBuilder 根据 GP 的 `vertexAttributes()` 和 `instanceAttributes()` 在生成的 main() 文件头部自动生成：

```glsl
// Generated by ProgramBuilder:
in vec2 position;
in float coverage;
in vec4 color;
```

### 5.4 Varying 输出与 CoordTransform

CoordTransform 的 varying 由 ProgramBuilder 在 VS 生成代码中统一处理。对于每个 FP 的每个 CoordTransform，生成：

```glsl
// VS:
uniform mat3 u_CoordTransform_0;
out vec2 v_TexCoord_0;        // or vec3 if hasPerspective
// ...in main():
v_TexCoord_0 = (u_CoordTransform_0 * vec3(srcCoord, 1.0)).xy;

// FS:
in vec2 v_TexCoord_0;
// ...in main():
vec2 coord_0 = v_TexCoord_0;
vec4 fp0_result = FP_LinearGradientLayout(coord_0);
```

---

## 6. XP 处理

### 6.1 PorterDuff 的函数化

PorterDuff XP 对应 `src/gpu/glsl/GLSLBlend.cpp` 中的 `AppendMode` 函数。在模块化方案中，Blend 逻辑提取为独立的 `.glsl` 文件。

BlendMode 通过编译期 `#define` 选择变体（而非运行时参数），因为不同 BlendMode 生成的代码结构完全不同（coefficient blend vs. advanced blend）。

#### 6.1.1 xp/porter_duff_xp.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// PorterDuffXferProcessor module.
// Variants:
//   TGFX_BLEND_MODE            — integer BlendMode enum value
//   TGFX_XP_HAS_DST_TEXTURE   — 0 or 1
//   TGFX_XP_HAS_COVERAGE_PROC — 0 or 1

#ifndef XP_PORTER_DUFF_GLSL
#define XP_PORTER_DUFF_GLSL

#include "common/tgfx_blend.glsl"

tgfx_float4 XP_PorterDuff(tgfx_float4 inputColor, tgfx_float4 inputCoverage,
                            tgfx_float4 dstColor
#if TGFX_XP_HAS_DST_TEXTURE
                            , TGFX_SAMPLER_2D s_DstTexture,
                            tgfx_float2 u_DstTextureUpperLeft,
                            tgfx_float2 u_DstTextureCoordScale
#endif
                            ) {
#if TGFX_XP_HAS_DST_TEXTURE
    // Discard fully transparent coverage pixels for text draw batching.
    if (inputCoverage.r <= 0.0 && inputCoverage.g <= 0.0 && inputCoverage.b <= 0.0) {
        TGFX_DISCARD;
    }
    tgfx_float2 dstTexCoord = (TGFX_FRAG_COORD.xy - u_DstTextureUpperLeft) *
                                u_DstTextureCoordScale;
    dstColor = TGFX_SAMPLE(s_DstTexture, dstTexCoord);
#endif

    tgfx_float4 result = tgfx_blend(inputColor, inputCoverage, dstColor);

#if TGFX_XP_HAS_COVERAGE_PROC
    result = inputCoverage * result + (tgfx_float4(1.0) - inputCoverage) * dstColor;
#endif

    return result;
}

#endif // XP_PORTER_DUFF_GLSL
```

#### 6.1.2 common/tgfx_blend.glsl

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// Blend mode functions extracted from GLSLBlend.cpp.
// TGFX_BLEND_MODE is defined by the ProgramBuilder before including this file.

#ifndef TGFX_BLEND_GLSL
#define TGFX_BLEND_GLSL

// Helper functions for advanced blend modes.
tgfx_float tgfx_luminance(tgfx_float3 color) {
    return dot(tgfx_float3(0.3, 0.59, 0.11), color);
}

tgfx_float3 tgfx_set_luminance(tgfx_float3 hueSat, tgfx_float alpha,
                                 tgfx_float3 lumColor) {
    tgfx_float diff = tgfx_luminance(lumColor - hueSat);
    tgfx_float3 outColor = hueSat + diff;
    tgfx_float outLum = tgfx_luminance(outColor);
    tgfx_float minComp = min(min(outColor.r, outColor.g), outColor.b);
    tgfx_float maxComp = max(max(outColor.r, outColor.g), outColor.b);
    if (minComp < 0.0 && outLum != minComp) {
        outColor = outLum + ((outColor - tgfx_float3(outLum, outLum, outLum)) * outLum) /
                   (outLum - minComp);
    }
    if (maxComp > alpha && maxComp != outLum) {
        outColor = outLum + ((outColor - tgfx_float3(outLum, outLum, outLum)) *
                   (alpha - outLum)) / (maxComp - outLum);
    }
    return outColor;
}

tgfx_float tgfx_saturation(tgfx_float3 color) {
    return max(max(color.r, color.g), color.b) - min(min(color.r, color.g), color.b);
}

// The main blend function dispatches based on compile-time TGFX_BLEND_MODE.
// This function handles the coefficient-based blend modes (Clear through PlusLighter)
// and delegates to per-mode functions for advanced modes (Overlay through PlusDarker).
tgfx_float4 tgfx_blend(tgfx_float4 src, tgfx_float4 coverage, tgfx_float4 dst) {
    // Coefficient-based blend modes are resolved at compile time
    // via #if TGFX_BLEND_MODE == N blocks.
    // Each mode produces: result = src * srcFactor + dst * dstFactor.
    tgfx_float4 result;

#if TGFX_BLEND_MODE == 0
    // Clear
    result = tgfx_float4(0.0);
#elif TGFX_BLEND_MODE == 3
    // SrcOver: src + dst * (1 - src.a)
    result = src + dst * (1.0 - src.a);
#elif TGFX_BLEND_MODE == 15
    // Overlay: hardLight with src/dst reversed
    result.a = src.a + (1.0 - src.a) * dst.a;
    // Per-component hard-light logic inlined at compile time...
    // (Full implementation omitted for brevity; see common/tgfx_blend.glsl in repo)
#else
    // Default SrcOver fallback for unrecognized mode.
    result = src + dst * (1.0 - src.a);
#endif

    return result;
}

#endif // TGFX_BLEND_GLSL
```

### 6.2 EmptyXP

```glsl
// Copyright (C) 2026 Tencent. All rights reserved.
// EmptyXferProcessor module. Simply multiplies color by coverage.

#ifndef XP_EMPTY_GLSL
#define XP_EMPTY_GLSL

tgfx_float4 XP_Empty(tgfx_float4 inputColor, tgfx_float4 inputCoverage) {
    return inputColor * inputCoverage;
}

#endif // XP_EMPTY_GLSL
```

### 6.3 dst texture blend 的特殊处理

当 `PorterDuffXferProcessor` 持有 `dstTextureInfo`（非空）时，Fragment Shader 需要从 dst texture 采样。通过 `TGFX_XP_HAS_DST_TEXTURE` 宏控制此变体。ProgramBuilder 在检测到 `xferProcessor->dstTextureView()` 非空时设置此宏并传入额外的 sampler 和 uniform 参数。

---

## 7. Layer 2: 胶水代码生成（ProgramBuilder 重构）

### 7.1 新 ProgramBuilder 职责

重构后的 `ModularProgramBuilder` 只负责生成：

1. `#include` 列表（后端适配层 + 需要的 FP/GP/XP 模块）
2. Uniform 声明（FragmentUniformBlock / VertexUniformBlock 成员）
3. Sampler 声明
4. Varying 声明
5. `main()` 函数体（十几行控制流 + 函数调用）

**不再负责**：生成完整的 Shader 逻辑代码。

### 7.2 main() 生成规则

#### 7.2.1 GP → VS main() 调用

```glsl
void main() {
    GP_QuadPerEdgeAA_VS(position, color, coverage, u_RTAdjust);
    // CoordTransform outputs:
    v_TexCoord_0 = (u_CoordTransform_0 * vec3(uvCoord, 1.0)).xy;
}
```

#### 7.2.2 叶子 FP → 直接函数调用

```glsl
// In FS main():
vec4 fp0_result = FP_TextureEffect_RGB(gpOutputColor, v_TexCoord_0, TextureSampler_0, u_Subset_0);
```

#### 7.2.3 ComposeFragmentProcessor → 线性调用链

ComposeFragmentProcessor 将 N 个子 FP 串联执行，前一个的输出作为后一个的输入。在 main() 中展开为线性函数调用链：

```glsl
// Compose(FP_A, FP_B, FP_C):
vec4 compose_0 = FP_A(gpOutputColor, ...);
vec4 compose_1 = FP_B(compose_0, ...);
vec4 colorResult = FP_C(compose_1, ...);
```

#### 7.2.4 ClampedGradientEffect → if/else 条件 + 两次函数调用

```glsl
// ClampedGradient(Layout, Colorizer):
vec4 gradLayout = FP_LinearGradientLayout(v_TexCoord_0);
vec4 t = gradLayout;
vec4 colorResult;
if (t.y < 0.0) {
    colorResult = vec4(0.0);
} else if (t.x <= 0.0) {
    colorResult = u_LeftBorderColor;
} else if (t.x >= 1.0) {
    colorResult = u_RightBorderColor;
} else {
    colorResult = FP_SingleIntervalGradientColorizer(t, u_Start, u_End);
}
colorResult.rgb *= colorResult.a;
colorResult *= gpOutputColor.a;
```

#### 7.2.5 XfermodeFragmentProcessor → 两次函数调用 + blend

```glsl
// Xfermode(srcFP, dstFP, blendMode):
vec4 xferInput = vec4(gpOutputColor.rgb, 1.0);
vec4 xferSrc = FP_SourceFP(xferInput, ...);
vec4 xferDst = FP_DestFP(xferInput, ...);
vec4 colorResult = tgfx_xfermode_blend(xferSrc, xferDst);  // uses TGFX_XFERMODE_BLEND_MODE
colorResult *= gpOutputColor.a;
```

#### 7.2.6 GaussianBlur1DFragmentProcessor → for 循环 + 函数调用

```glsl
// GaussianBlur1D(childFP, sigma, direction):
vec2 blurOffset = u_Step;
float sigma = u_Sigma;
int radius = int(ceil(2.0 * sigma));
vec4 sum = vec4(0.0);
float total = 0.0;
for (int j = 0; j <= TGFX_BLUR_MAX_SAMPLES; ++j) {
    int i = j - radius;
    float weight = exp(-float(i * i) / (2.0 * sigma * sigma));
    total += weight;
    vec4 sample = FP_TextureEffect_RGB(gpOutputColor, v_TexCoord_0 + blurOffset * float(i),
                                        TextureSampler_0, u_Subset_0);
    sum += sample * weight;
    if (i == radius) { break; }
}
vec4 colorResult = sum / total;
```

#### 7.2.7 AARectEffect (coverage FP) → 函数调用

```glsl
// Coverage FP chain:
vec4 coverageResult = FP_AARectEffect(gpOutputCoverage, u_Rect);
```

#### 7.2.8 XP → 函数调用或硬件混合

```glsl
// XP application:
// If hardware blending (EmptyXP + hardware blend state):
fragColor = XP_Empty(colorResult, coverageResult);
// If software blending (PorterDuff with advanced mode or dst texture):
fragColor = XP_PorterDuff(colorResult, coverageResult, dstColor, ...);
```

### 7.3 Uniform 声明生成规则

新方案中，Uniform 按编号命名，不需要 mangle 后缀：

```glsl
layout(std140) uniform FragmentUniformBlock {
    vec4  u_FP0_Color;         // ConstColor's color
    vec4  u_FP1_Subset;        // TextureEffect's subset
    vec4  u_FP2_Start;         // SingleInterval's start
    vec4  u_FP2_End;           // SingleInterval's end
    vec4  u_Cov0_Rect;         // AARectEffect's rect
    vec2  u_XP_DstUpperLeft;   // PorterDuff dst texture offset
    vec2  u_XP_DstCoordScale;  // PorterDuff dst texture scale
};

layout(std140) uniform VertexUniformBlock {
    vec4  u_RTAdjust;
    mat3  u_CoordTransform_0;
    mat3  u_CoordTransform_1;
    mat3  u_GP_Matrix;
    vec4  u_GP_Color;
};
```

编号规则：
- `u_FP{N}_{UniformName}` — 第 N 个 FP 的 Uniform
- `u_Cov{N}_{UniformName}` — 第 N 个 coverage FP 的 Uniform
- `u_XP_{UniformName}` — XP 的 Uniform
- `u_GP_{UniformName}` — GP 的 Uniform
- `u_CoordTransform_{N}` — 第 N 个 CoordTransform 矩阵

### 7.4 #include 列表生成规则

ProgramBuilder 遍历 ProgramInfo 中的所有 Processor，收集需要的模块文件：

```cpp
// Pseudocode for include list generation:
std::set<std::string> includes;
includes.insert("backends/tgfx_types_glsl.glsl");
includes.insert("common/tgfx_sampling.glsl");
for (auto* fp : leafFragmentProcessors) {
    includes.insert(fp->shaderModulePath());  // e.g. "fp/texture_effect_rgb.glsl"
}
includes.insert(gp->shaderModulePath());      // e.g. "gp/quad_per_edge_aa_gp.vert.glsl"
includes.insert(xp->shaderModulePath());      // e.g. "xp/porter_duff_xp.glsl"
if (hasAdvancedBlend) {
    includes.insert("common/tgfx_blend.glsl");
}
```

### 7.5 完整的生成结果示例

#### 7.5.1 管线模板 1：纯色填充

**FP 链**: GP(QuadPerEdgeAA) + ConstColor(Ignore) + AARectEffect + PorterDuff(SrcOver)

生成的 Fragment Shader：

```glsl
#version 100
precision mediump float;

#define TGFX_CONST_COLOR_INPUT_MODE 0
#define TGFX_BLEND_MODE 3

#include "backends/tgfx_types_glsl.glsl"
#include "fp/const_color.glsl"
#include "fp/aa_rect_effect.glsl"
#include "xp/porter_duff_xp.glsl"

// Uniforms
layout(std140) uniform FragmentUniformBlock {
    vec4 u_FP0_Color;
    vec4 u_Cov0_Rect;
};

// Varyings from VS
in vec4 v_Color;
in float v_Coverage;

// Output
out vec4 fragColor;

void main() {
    // GP output
    vec4 gpOutputColor = v_Color;
    vec4 gpOutputCoverage = vec4(v_Coverage);

    // Color FP chain
    vec4 colorResult = FP_ConstColor(gpOutputColor, u_FP0_Color);

    // Coverage FP chain
    vec4 coverageResult = FP_AARectEffect(gpOutputCoverage, u_Cov0_Rect);

    // XP
    fragColor = XP_PorterDuff(colorResult, coverageResult, vec4(0.0));
}
```

#### 7.5.2 管线模板 3：线性渐变

**FP 链**: GP(Default) + ClampedGradient(LinearGradientLayout, SingleIntervalColorizer) + AARectEffect + PorterDuff(SrcOver)

生成的 Fragment Shader：

```glsl
#version 100
precision mediump float;

#define TGFX_BLEND_MODE 3

#include "backends/tgfx_types_glsl.glsl"
#include "fp/linear_gradient_layout.glsl"
#include "fp/single_interval_gradient_colorizer.glsl"
#include "fp/aa_rect_effect.glsl"
#include "xp/porter_duff_xp.glsl"

layout(std140) uniform FragmentUniformBlock {
    vec4 u_GP_Color;
    vec4 u_FP0_LeftBorderColor;
    vec4 u_FP0_RightBorderColor;
    vec4 u_FP0_Start;
    vec4 u_FP0_End;
    vec4 u_Cov0_Rect;
};

in vec2 v_TexCoord_0;
in float v_Coverage;

out vec4 fragColor;

void main() {
    vec4 gpOutputColor = u_GP_Color;
    vec4 gpOutputCoverage = vec4(v_Coverage);

    // ClampedGradientEffect (container FP, inlined as control flow)
    vec4 gradLayout = FP_LinearGradientLayout(v_TexCoord_0);
    vec4 t = gradLayout;
    vec4 colorResult;
    if (t.y < 0.0) {
        colorResult = vec4(0.0);
    } else if (t.x <= 0.0) {
        colorResult = u_FP0_LeftBorderColor;
    } else if (t.x >= 1.0) {
        colorResult = u_FP0_RightBorderColor;
    } else {
        colorResult = FP_SingleIntervalGradientColorizer(t, u_FP0_Start, u_FP0_End);
    }
    colorResult.rgb *= colorResult.a;
    colorResult *= gpOutputColor.a;

    // Coverage
    vec4 coverageResult = FP_AARectEffect(gpOutputCoverage, u_Cov0_Rect);

    // XP
    fragColor = XP_PorterDuff(colorResult, coverageResult, vec4(0.0));
}
```

#### 7.5.3 管线模板 5：高斯模糊

**FP 链**: GP(Default) + GaussianBlur1D(TextureEffect) + PorterDuff(SrcOver)

生成的 Fragment Shader：

```glsl
#version 100
precision mediump float;

#define TGFX_TEXTURE_EFFECT_IS_ALPHA_ONLY 0
#define TGFX_TEXTURE_EFFECT_HAS_SUBSET 0
#define TGFX_TEXTURE_EFFECT_IS_STRICT 0
#define TGFX_TEXTURE_EFFECT_HAS_ALPHA_START 0
#define TGFX_TEXTURE_EFFECT_HAS_PERSPECTIVE 0
#define TGFX_BLUR_MAX_SAMPLES 40
#define TGFX_BLEND_MODE 3

#include "backends/tgfx_types_glsl.glsl"
#include "common/tgfx_sampling.glsl"
#include "fp/texture_effect_rgb.glsl"
#include "xp/porter_duff_xp.glsl"

layout(std140) uniform FragmentUniformBlock {
    vec4 u_GP_Color;
    float u_FP0_Sigma;
    vec2 u_FP0_Step;
};

uniform sampler2D TextureSampler_0;

in vec2 v_TexCoord_0;
in float v_Coverage;

out vec4 fragColor;

void main() {
    vec4 gpOutputColor = u_GP_Color;
    vec4 gpOutputCoverage = vec4(v_Coverage);

    // GaussianBlur1D (container FP, inlined as loop)
    vec2 blurOffset = u_FP0_Step;
    float sigma = u_FP0_Sigma;
    int radius = int(ceil(2.0 * sigma));
    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (int j = 0; j <= TGFX_BLUR_MAX_SAMPLES; ++j) {
        int i = j - radius;
        float weight = exp(-float(i * i) / (2.0 * sigma * sigma));
        total += weight;
        vec4 blurSample = FP_TextureEffect_RGB(gpOutputColor,
                                                 v_TexCoord_0 + blurOffset * float(i),
                                                 TextureSampler_0);
        sum += blurSample * weight;
        if (i == radius) { break; }
    }
    vec4 colorResult = sum / total;

    // XP
    fragColor = XP_PorterDuff(colorResult, gpOutputCoverage, vec4(0.0));
}
```

---

## 8. 运行时 vs 离线生成

### 8.1 GL/Metal/Vulkan 后端：运行时生成

对于 GL/Metal/Vulkan 后端，Shader 模块文件作为资源嵌入到应用中（编译时通过 `xxd` 或类似工具转为 C 字符串常量）。运行时流程：

1. `DrawOp::execute()` 组装 `ProgramInfo`
2. `ModularProgramBuilder::generateShader()` 生成 main() 函数体（十几行）
3. 拼接：`[#defines] + [#includes resolved] + [uniform block] + [main()]`
4. GL：直接编译 GLSL
5. Metal：走现有 `preprocessGLSL() → shaderc(SPIRV) → SPIRV-Cross(MSL)` 管线
6. 缓存到 `GlobalCache`（Key 不变，仍由各 Processor 的 `computeProcessorKey()` 计算）

**注意**：由于 GLSL `#include` 在运行时不可用（不是标准 GLSL 特性），需要在生成时将 `#include` 解析为内联文本。ProgramBuilder 维护一个模块文本注册表，生成时直接拼接模块文本。

### 8.2 UE RHI 后端：离线枚举

UE 后端的离线工具 `TgfxShaderVariantGenerator` 使用同一个 `ModularProgramBuilder`，但以不同的方式输出：

1. 枚举所有合法的 Processor 组合（约 300-800 个变体）
2. 对每个变体，`ModularProgramBuilder::generateMainBody()` 生成 main()
3. 输出 `.usf` 文件，`#include` 模块文件（UE 的 Shader Compiler 支持 `#include`）
4. 为每个变体分配 VariantID，写入 `ShaderVariantRegistry`
5. UE Cook 阶段预编译所有 `.usf` 文件

### 8.3 两种路径共存的架构

```cpp
// src/gpu/ModularProgramBuilder.h
class ModularProgramBuilder {
 public:
    // Generate complete shader source (for GL/Metal/Vulkan runtime).
    // Resolves #includes inline and returns a single GLSL string.
    static std::string GenerateShaderSource(Context* context, const ProgramInfo* programInfo);

    // Generate main() body only (for UE offline tool).
    // Returns the main() body and a list of required #include paths.
    struct ShaderModuleOutput {
        std::string vertexMainBody;
        std::string fragmentMainBody;
        std::vector<std::string> requiredIncludes;
        std::vector<std::string> defines;
    };
    static ShaderModuleOutput GenerateModuleOutput(const ProgramInfo* programInfo);
};
```

### 8.4 ShaderVariantRegistry 在两种模式下的角色

| 模式 | ShaderVariantRegistry 职责 |
|------|--------------------------|
| 运行时 (GL/Metal) | 不需要 — `GlobalCache` 以 `BytesKey` 为键缓存编译后的 Program |
| 离线 (UE RHI) | 维护 `ProgramKey → VariantID` 映射表，运行时查表获取预编译 Shader |

---

## 9. Uniform 与 Sampler 绑定重构

### 9.1 新的 Uniform 命名约定

当前方案：Uniform 名称通过 `nameVariable()` 追加 `_P{index}` 后缀（如 `Color_P1`、`Subset_P2_P0`）。

新方案：Uniform 按 Processor 编号命名（如 `u_FP0_Color`、`u_FP1_Subset`）。编号在 `ProgramInfo` 构建时确定，与 Processor 在链中的位置一一对应。

```
旧: Color_P1          → 新: u_FP0_Color
旧: Subset_P2         → 新: u_FP1_Subset
旧: start_P3          → 新: u_FP2_Start
旧: end_P3            → 新: u_FP2_End
旧: Rect_P4           → 新: u_Cov0_Rect
旧: DstTextureUpperLeft_P5 → 新: u_XP_DstUpperLeft
```

### 9.2 Uniform Buffer 布局

布局不变，仍然使用 std140：

- `VertexUniformBlock` — binding 0
- `FragmentUniformBlock` — binding 1
- Samplers — binding 2+

成员顺序由 ProgramBuilder 在遍历 Processor 链时确定，与当前方案保持一致。

### 9.3 Sampler 槽位分配规则

Sampler 按 FP 链中出现的顺序分配递增的 binding 编号：

```
TextureSampler_0 — binding 2  (第一个 FP 的第一个 texture)
TextureSampler_1 — binding 3  (第一个 FP 的第二个 texture, e.g. YUV U plane)
TextureSampler_2 — binding 4  (第二个 FP 的 texture)
DstTextureSampler — binding 5 (XP 的 dst texture)
```

### 9.4 运行时 setData 适配

`onSetData()` 的实现不变，仍然通过名称字符串设置 Uniform 值。但名称从 mangled 名改为编号名：

```cpp
// Before (mangled):
fragmentUniformData->setData("Color_P1", color);

// After (numbered):
fragmentUniformData->setData("u_FP0_Color", color);
```

ProgramInfo 在 `setUniformsAndSamplers()` 中根据 Processor 编号自动添加前缀，FP 的 `onSetData()` 仍然只写 base name（如 `"Color"`），前缀由框架添加。

---

## 10. tgfx 核心代码修改清单

### 10.1 需要新增的文件列表

**Shader 模块文件** (`src/gpu/shaders/`):

| 文件路径 | 说明 |
|---------|------|
| `src/gpu/shaders/backends/tgfx_types_glsl.glsl` | GLSL 后端类型适配 |
| `src/gpu/shaders/backends/tgfx_types_vulkan.glsl` | Vulkan 后端类型适配 |
| `src/gpu/shaders/backends/tgfx_types_ue.glsl` | UE RHI 后端类型适配 |
| `src/gpu/shaders/common/tgfx_sampling.glsl` | 纹理采样抽象 |
| `src/gpu/shaders/common/tgfx_uniform.glsl` | Uniform 声明辅助 |
| `src/gpu/shaders/common/tgfx_varying.glsl` | Varying 声明辅助 |
| `src/gpu/shaders/common/tgfx_blend.glsl` | Blend 模式函数库 |
| `src/gpu/shaders/fp/const_color.glsl` | ConstColorProcessor |
| `src/gpu/shaders/fp/texture_effect_rgb.glsl` | TextureEffect RGB |
| `src/gpu/shaders/fp/texture_effect_yuv_i420.glsl` | TextureEffect YUV I420 |
| `src/gpu/shaders/fp/texture_effect_yuv_nv12.glsl` | TextureEffect YUV NV12 |
| `src/gpu/shaders/fp/tiled_texture_effect.glsl` | TiledTextureEffect |
| `src/gpu/shaders/fp/device_space_texture_effect.glsl` | DeviceSpaceTextureEffect |
| `src/gpu/shaders/fp/linear_gradient_layout.glsl` | LinearGradientLayout |
| `src/gpu/shaders/fp/radial_gradient_layout.glsl` | RadialGradientLayout |
| `src/gpu/shaders/fp/conic_gradient_layout.glsl` | ConicGradientLayout |
| `src/gpu/shaders/fp/diamond_gradient_layout.glsl` | DiamondGradientLayout |
| `src/gpu/shaders/fp/single_interval_gradient_colorizer.glsl` | SingleIntervalGradientColorizer |
| `src/gpu/shaders/fp/dual_interval_gradient_colorizer.glsl` | DualIntervalGradientColorizer |
| `src/gpu/shaders/fp/texture_gradient_colorizer.glsl` | TextureGradientColorizer |
| `src/gpu/shaders/fp/unrolled_binary_gradient_colorizer.glsl` | UnrolledBinaryGradientColorizer |
| `src/gpu/shaders/fp/color_matrix.glsl` | ColorMatrixFragmentProcessor |
| `src/gpu/shaders/fp/color_space_xform.glsl` | ColorSpaceXformEffect |
| `src/gpu/shaders/fp/luma.glsl` | LumaFragmentProcessor |
| `src/gpu/shaders/fp/alpha_threshold.glsl` | AlphaThresholdFragmentProcessor |
| `src/gpu/shaders/fp/aa_rect_effect.glsl` | AARectEffect |
| `src/gpu/shaders/gp/quad_per_edge_aa_gp.vert.glsl` | QuadPerEdgeAAGeometryProcessor |
| `src/gpu/shaders/gp/default_gp.vert.glsl` | DefaultGeometryProcessor |
| `src/gpu/shaders/gp/ellipse_gp.vert.glsl` | EllipseGeometryProcessor |
| `src/gpu/shaders/gp/atlas_text_gp.vert.glsl` | AtlasTextGeometryProcessor |
| `src/gpu/shaders/gp/mesh_gp.vert.glsl` | MeshGeometryProcessor |
| `src/gpu/shaders/gp/shape_instanced_gp.vert.glsl` | ShapeInstancedGeometryProcessor |
| `src/gpu/shaders/gp/non_aa_rrect_gp.vert.glsl` | NonAARRectGeometryProcessor |
| `src/gpu/shaders/gp/hairline_line_gp.vert.glsl` | HairlineLineGeometryProcessor |
| `src/gpu/shaders/gp/hairline_quad_gp.vert.glsl` | HairlineQuadGeometryProcessor |
| `src/gpu/shaders/gp/round_stroke_rect_gp.vert.glsl` | RoundStrokeRectGeometryProcessor |
| `src/gpu/shaders/xp/porter_duff_xp.glsl` | PorterDuffXferProcessor |
| `src/gpu/shaders/xp/empty_xp.glsl` | EmptyXferProcessor |

**C++ 新增文件**:

| 文件路径 | 说明 |
|---------|------|
| `src/gpu/ModularProgramBuilder.h` | 新的模块化 ProgramBuilder |
| `src/gpu/ModularProgramBuilder.cpp` | 实现 main() 生成逻辑 |
| `src/gpu/ShaderModuleRegistry.h` | Shader 模块文本注册表 |
| `src/gpu/ShaderModuleRegistry.cpp` | 管理嵌入的 .glsl 文本 |

### 10.2 需要修改的文件列表

| 文件路径 | 修改内容 |
|---------|---------|
| `src/gpu/ProgramBuilder.h` | 提取接口，供 ModularProgramBuilder 和 GLSLProgramBuilder 共用 |
| `src/gpu/ProgramBuilder.cpp` | 保留向后兼容的 emitCode 路径 |
| `src/gpu/ProgramInfo.h` | 新增 `generateModularShader()` 入口 |
| `src/gpu/ProgramInfo.cpp` | 增加模块化路径的 program 创建逻辑 |
| `src/gpu/UniformHandler.h` | 支持新的编号命名约定 |
| `src/gpu/UniformHandler.cpp` | 生成 `u_FP{N}_` 前缀 |
| `src/gpu/processors/FragmentProcessor.h` | 每个 FP 子类新增 `shaderModulePath()` 方法 |
| `src/gpu/processors/TextureEffect.h` | 新增 `shaderModulePath()` |
| `src/gpu/processors/ConstColorProcessor.h` | 新增 `shaderModulePath()` |
| `src/gpu/processors/LinearGradientLayout.h` | 新增 `shaderModulePath()` |
| 所有 17 个叶子 FP 的 `.h` 文件 | 新增 `shaderModulePath()` + variant `#define` 生成方法 |
| 所有 10 个 GP 的 `.h` 文件 | 新增 `shaderModulePath()` |
| `src/gpu/processors/PorterDuffXferProcessor.h` | 新增 `shaderModulePath()` |
| `src/gpu/processors/EmptyXferProcessor.h` | 新增 `shaderModulePath()` |
| `src/gpu/glsl/GLSLProgramBuilder.h` | 适配新的 ProgramBuilder 接口 |
| `src/gpu/glsl/GLSLProgramBuilder.cpp` | 增加模块化 Shader 生成路径 |
| `src/gpu/metal/MetalShaderModule.mm` | 适配模块化生成的 GLSL 输入 |
| `src/gpu/GlobalCache.h` | Key 计算兼容新命名 |
| `CMakeLists.txt` | 添加 .glsl 文件的嵌入规则 |

### 10.3 需要废弃的文件列表（Phase 5 清理）

模块化完全迁移后，以下 GLSL* 文件将被废弃：

| 文件路径 | 说明 |
|---------|------|
| `src/gpu/glsl/processors/GLSLTextureEffect.h/.cpp` | 替换为 `fp/texture_effect_rgb.glsl` |
| `src/gpu/glsl/processors/GLSLConstColorProcessor.h/.cpp` | 替换为 `fp/const_color.glsl` |
| `src/gpu/glsl/processors/GLSLLinearGradientLayout.h/.cpp` | 替换为 `fp/linear_gradient_layout.glsl` |
| `src/gpu/glsl/processors/GLSLRadialGradientLayout.h/.cpp` | 替换为 `fp/radial_gradient_layout.glsl` |
| `src/gpu/glsl/processors/GLSLConicGradientLayout.h/.cpp` | 替换为 `fp/conic_gradient_layout.glsl` |
| `src/gpu/glsl/processors/GLSLDiamondGradientLayout.h/.cpp` | 替换为 `fp/diamond_gradient_layout.glsl` |
| `src/gpu/glsl/processors/GLSLSingleIntervalGradientColorizer.h/.cpp` | 替换为对应 .glsl |
| `src/gpu/glsl/processors/GLSLDualIntervalGradientColorizer.h/.cpp` | 替换为对应 .glsl |
| `src/gpu/glsl/processors/GLSLTextureGradientColorizer.h/.cpp` | 替换为对应 .glsl |
| `src/gpu/glsl/processors/GLSLUnrolledBinaryGradientColorizer.h/.cpp` | 替换为对应 .glsl |
| `src/gpu/glsl/processors/GLSLColorMatrixFragmentProcessor.h/.cpp` | 替换为 `fp/color_matrix.glsl` |
| `src/gpu/glsl/processors/GLSLLumaFragmentProcessor.h/.cpp` | 替换为 `fp/luma.glsl` |
| `src/gpu/glsl/processors/GLSLAlphaThresholdFragmentProcessor.h/.cpp` | 替换为对应 .glsl |
| `src/gpu/glsl/processors/GLSLAARectEffect.h/.cpp` | 替换为 `fp/aa_rect_effect.glsl` |
| `src/gpu/glsl/processors/GLSLDeviceSpaceTextureEffect.h/.cpp` | 替换为对应 .glsl |
| `src/gpu/glsl/processors/GLSLTiledTextureEffect.h/.cpp` | 替换为对应 .glsl |
| `src/gpu/glsl/processors/GLSLComposeFragmentProcessor.h/.cpp` | 逻辑内联到 main() 生成 |
| `src/gpu/glsl/processors/GLSLClampedGradientEffect.h/.cpp` | 逻辑内联到 main() 生成 |
| `src/gpu/glsl/processors/GLSLXfermodeFragmentProcessor.h/.cpp` | 逻辑内联到 main() 生成 |
| `src/gpu/glsl/processors/GLSLGaussianBlur1DFragmentProcessor.h/.cpp` | 逻辑内联到 main() 生成 |
| `src/gpu/glsl/processors/GLSLPorterDuffXferProcessor.h/.cpp` | 替换为 `xp/porter_duff_xp.glsl` |
| `src/gpu/glsl/processors/GLSLEmptyXferProcessor.h/.cpp` | 替换为 `xp/empty_xp.glsl` |
| `src/gpu/glsl/processors/GLSLDefaultGeometryProcessor.h/.cpp` | 替换为 GP .vert.glsl |
| `src/gpu/glsl/processors/GLSLQuadPerEdgeAAGeometryProcessor.h/.cpp` | 替换为 GP .vert.glsl |
| 其他 8 个 GLSL GP 文件 | 替换为对应 GP .vert.glsl |
| `src/gpu/glsl/GLSLBlend.h/.cpp` | 替换为 `common/tgfx_blend.glsl` |

共计约 36 个 GLSL* 文件（18 对 .h/.cpp）将被废弃。

### 10.4 修改影响范围评估

| 影响层 | 文件数 | 风险 |
|-------|--------|------|
| Shader 模块（新增） | ~40 个 .glsl 文件 | 低 — 纯新增，不影响现有代码 |
| ProgramBuilder 重构 | ~5 个文件 | 中 — 核心路径修改，需充分测试 |
| Processor 接口扩展 | ~30 个文件 | 低 — 只新增方法，不修改现有接口 |
| UniformHandler 适配 | ~3 个文件 | 中 — 命名约定变更需全链路验证 |
| 后端适配 | ~3 个文件 | 低 — Metal 管线不变，GL 直接编译 |

---

## 11. 边界情况处理

### 11.1 GaussianBlur1D 的循环上限

当前实现中，`GaussianBlur1DFragmentProcessor` 的 for 循环上限由 `maxSigma` 决定（`4 * maxSigma`），作为编译期常量嵌入 Shader。在模块化方案中，通过 `#define TGFX_BLUR_MAX_SAMPLES` 宏设置：

```glsl
#define TGFX_BLUR_MAX_SAMPLES 40  // 4 * maxSigma, e.g. maxSigma=10
```

不同 `maxSigma` 值生成不同的 Shader 变体。这与当前方案行为一致——`maxSigma` 已经是 ProcessorKey 的一部分。

### 11.2 TextureEffect 的 YUV 变体

TextureEffect 有 3 种主要变体：
- **RGB**：单纹理采样，对应 `fp/texture_effect_rgb.glsl`
- **YUV I420**：3 纹理采样（Y/U/V），对应 `fp/texture_effect_yuv_i420.glsl`
- **YUV NV12**：2 纹理采样（Y/UV），对应 `fp/texture_effect_yuv_nv12.glsl`

YUV 变体通过 `computeProcessorKey()` 中的 flags 区分，映射到不同的 `#include` 文件。ProgramBuilder 根据 key 中的 YUV 标志选择正确的模块文件。

此外，YUV 变体还区分 Limited Range 和 Full Range 色彩空间，通过 `#define TGFX_YUV_LIMITED_RANGE` 控制是否在 Shader 中执行 `yuv.x -= (16.0 / 255.0)` 偏移。

### 11.3 PorterDuff 的 dst texture blend

当 `BlendMode` 无法用硬件混合实现时（如 Overlay、Hue 等高级模式），`PorterDuffXferProcessor` 携带 `dstTextureInfo`，需要从 dst texture 采样。

处理方式：
- `TGFX_XP_HAS_DST_TEXTURE = 1` 时，XP 模块额外接收 dst texture sampler 和相关 uniform
- XP 函数内部在读取 dst 颜色后，调用 `tgfx_blend()` 执行软件混合
- Coverage 处理：`result = coverage * blended + (1 - coverage) * dstColor`

### 11.4 framebufferFetch 支持

Metal 后端通过 `SPIRV-Cross` 的 `use_framebuffer_fetch_subpasses = true` 将 subpassInput 转换为 `[[color(0)]]`，实现 framebuffer fetch。

在模块化方案中，当平台支持 framebuffer fetch 时，`dstColor` 不通过 texture 采样获取，而是通过 `inout` 变量：

```glsl
#if TGFX_HAS_FRAMEBUFFER_FETCH
    // dstColor is provided by the framebuffer fetch extension.
    layout(location = 0) inout vec4 dstColor;
#endif
```

此变体通过 `#define TGFX_HAS_FRAMEBUFFER_FETCH` 控制。

### 11.5 纹理格式差异

- **alphaOnly texture**：TextureEffect 的 `isAlphaOnly` 变体改变输出语义（`color.a * inputColor` vs `color * inputColor.a`），通过 `#define TGFX_TEXTURE_EFFECT_IS_ALPHA_ONLY` 控制
- **external texture (OES)**：Android 上的 external texture 使用 `samplerExternalOES`，通过 `#define TGFX_USE_EXTERNAL_TEXTURE` 控制采样宏
- **Rectangle texture**：macOS 上的 Rectangle texture 使用非归一化坐标，通过 subset 计算中的条件处理（不影响 Shader 模块，仅影响 `onSetData()` 中的 Uniform 值计算）

### 11.6 UE 版本兼容性

UE 5.4 至 5.7 的 Shader 系统存在以下差异需要处理：
- UE 5.4/5.5: `FGlobalShader` 的 Permutation 系统已稳定
- UE 5.6+: 可能引入新的 Shader Pipeline 特性

通过 `tgfx_types_ue.glsl` 中的版本宏处理差异：

```glsl
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
    // UE 5.6+ specific adaptations
#endif
```

---

## 12. 性能分析

### 12.1 函数调用开销

GPU Shader 编译器（NVIDIA、Adreno、Mali、Apple GPU）在优化时会积极内联函数调用。由于 FP 模块函数体通常很短（5-20 行），编译器几乎总会将其内联，因此函数调用带来零额外开销。

**验证方法**：比较模块化和非模块化方案生成的 SPIRV IR，确认函数已被内联。

### 12.2 编译时间对比

| 指标 | 当前方案 | 模块化方案 |
|------|---------|-----------|
| GLSL 生成时间 | ~0.5ms（C++ 字符串拼接） | ~0.3ms（模板拼接 + include 解析） |
| GLSL 编译时间 | 不变 | 不变（生成的 GLSL 代码量相同） |
| SPIRV→MSL 转换时间 | 不变 | 不变 |
| 首帧冷启动 | 不变 | 不变 |

模块化方案的 GLSL 生成略快，因为不需要递归的 `emitChild()` 调用和变量名 mangle 计算。

### 12.3 Shader 代码大小对比

模块化方案生成的 GLSL 代码与当前方案在 include 解析后完全等价（因为函数体内容相同），所以编译后的二进制大小无差异。

模块化方案在源码层面更紧凑（main() 只有十几行 + include），但解析 include 后的完整 GLSL 与当前方案大小相同。

### 12.4 运行时开销对比

| 操作 | 当前方案 | 模块化方案 |
|------|---------|-----------|
| ProgramKey 计算 | `computeProcessorKey()` | 完全不变 |
| GlobalCache 查找 | `BytesKey` 哈希查找 | 完全不变 |
| Uniform setData | 按名称字符串设置 | 名称格式变化，性能无差异 |
| Draw call | 不变 | 不变 |

**结论**：模块化重构是纯架构优化，不影响运行时性能。

---

## 13. 测试策略

### 13.1 渲染结果像素级对比

核心验证方法：确保模块化方案生成的 Shader 与当前方案在所有输入下产生完全相同的渲染输出。

```
测试步骤：
1. 使用当前方案渲染所有测试用例 → 产出 baseline 截图
2. 切换到模块化方案 → 渲染相同测试用例 → 产出 new 截图
3. 像素级比较 baseline 和 new
4. 差异必须为零（不允许浮点精度差异，因为 GLSL 代码完全等价）
```

### 13.2 覆盖所有管线模板

需确保以下 9 种管线模板全部覆盖：

| 模板 | 测试用例描述 |
|------|-------------|
| 1. 纯色填充 | `drawRect()` with solid color |
| 2. 纹理绘制 | `drawImage()` with RGB texture |
| 3. 线性渐变 | `drawRect()` with linear gradient shader |
| 4. 径向渐变 | `drawRect()` with radial gradient shader |
| 5. 高斯模糊 | `drawImage()` with blur ImageFilter |
| 6. 颜色矩阵 | `drawImage()` with colorMatrix ColorFilter |
| 7. 混合模式 | `drawImage()` with non-coefficient BlendMode (e.g. Overlay) |
| 8. 文字绘制 | `drawGlyphs()` / `drawTextBlob()` |
| 9. 平铺纹理 | `drawImage()` with TileMode::Repeat |

附加测试：
- YUV 纹理（I420 和 NV12）
- alphaOnly 纹理
- subset 纹理（strict constraint）
- dst texture blend（高级混合模式）
- ClampedGradient 边界条件（t < 0, t > 1, t in [0,1]）
- UnrolledBinary 所有 intervalCount（1-8）
- GaussianBlur 不同 maxSigma 值

### 13.3 覆盖所有后端

| 后端 | 测试环境 | 验证方式 |
|------|---------|---------|
| OpenGL ES 3.0 | Android 设备 / 模拟器 | 全量管线模板测试 |
| OpenGL (Desktop) | macOS / Linux | 全量管线模板测试 |
| Metal | macOS / iOS | 全量管线模板测试 |
| WebGL | Chrome / Safari | 主要管线模板测试 |
| UE RHI | UE Editor (Preview) | 离线变体全量验证 |

---

## 14. 迁移策略（分阶段实施）

### Phase 1: 后端适配层 + 2-3 个简单 FP 试点

**目标**：验证架构可行性。

**工作内容**：
1. 创建 `src/gpu/shaders/` 目录结构
2. 实现 `tgfx_types_glsl.glsl` 和 `tgfx_types_ue.glsl`
3. 实现 `ModularProgramBuilder` 框架（include 解析、main() 生成）
4. 迁移 3 个最简单的 FP：
   - `ConstColorProcessor` → `fp/const_color.glsl`
   - `LinearGradientLayout` → `fp/linear_gradient_layout.glsl`
   - `SingleIntervalGradientColorizer` → `fp/single_interval_gradient_colorizer.glsl`
5. 新旧路径通过编译开关共存（`#define TGFX_USE_MODULAR_SHADERS`）
6. 像素级对比验证试点 FP 的正确性

**验证标准**：纯色填充 + 线性渐变（SingleInterval）管线的渲染结果与当前方案完全一致。

### Phase 2: 所有叶子 FP 迁移

**目标**：完成所有 17 个叶子 FP 的模块化。

**工作内容**：
1. 迁移剩余 14 个叶子 FP 到 `.glsl` 文件
2. 实现所有变体的 `#define` 映射（YUV、alphaOnly、subset 等）
3. 实现 `tgfx_sampling.glsl` 和 `tgfx_blend.glsl`
4. 迁移 `AARectEffect`（coverage FP）
5. 全量管线模板像素级测试

**验证标准**：所有 9 种管线模板的渲染结果与当前方案完全一致。

### Phase 3: GP 模板化 + ProgramBuilder 重构

**目标**：完成 GP 和 XP 的模块化，ProgramBuilder 完全切换到模块化路径。

**工作内容**：
1. 迁移 10 个 GP 到 `.vert.glsl` 模板
2. 迁移 PorterDuff XP 和 Empty XP
3. 实现 4 种容器 FP 的 main() 内联生成逻辑：
   - `ComposeFragmentProcessor` → 线性调用链
   - `ClampedGradientEffect` → if/else 条件
   - `XfermodeFragmentProcessor` → 双调用 + blend
   - `GaussianBlur1DFragmentProcessor` → for 循环
4. `ModularProgramBuilder` 成为默认路径
5. 旧的 `emitCode()` 路径保留为 fallback

**验证标准**：全平台（GL、Metal）全量测试通过。

### Phase 4: UE 后端集成

**目标**：模块化方案与 UE RHI 后端对接。

**工作内容**：
1. 实现 `tgfx_types_ue.glsl` 的完整 HLSL 适配
2. 修改离线工具 `TgfxShaderVariantGenerator`，改用 `ModularProgramBuilder::GenerateModuleOutput()`
3. 生成的 `.usf` 文件使用 `#include` 引用模块文件
4. 验证 UE 离线编译 + 运行时渲染正确性

**验证标准**：UE Editor 中所有管线模板渲染正确。

### Phase 5: 清理废弃代码

**目标**：移除旧的 emitCode 路径。

**工作内容**：
1. 删除所有 `GLSL*.h/.cpp` 文件（约 36 个文件）
2. 删除 `GLSLBlend.h/.cpp`
3. 移除 `FragmentProcessor::emitCode()` 虚函数及相关 `EmitArgs`
4. 移除 `FragmentShaderBuilder::codeAppendf()` 等字符串拼接 API
5. 简化 `ProgramBuilder` 为仅 `ModularProgramBuilder`
6. 更新 CMakeLists.txt

**验证标准**：全平台全量测试通过 + 编译无警告。

---

## 15. 与 ue_shader_restructuring.md 的关系

### 15.1 共存方案

`ue_shader_restructuring.md` 描述的离线枚举方案可以在模块化重构完成前独立实施。两个方案互不冲突：

- **当前**：离线枚举方案调用现有 `ProgramBuilder::emitCode()` 生成完整 GLSL
- **模块化后**：离线枚举方案调用 `ModularProgramBuilder::GenerateModuleOutput()` 生成 main() + include 列表

### 15.2 模块化完成后的简化

模块化完成后，`ue_shader_restructuring.md` 中的以下部分将大幅简化：

| 离线枚举方案组件 | 当前复杂度 | 模块化后复杂度 |
|----------------|-----------|--------------|
| GLSL 生成 | 完整 Shader（几百行） | main() 体（十几行） |
| SPIRV 转换 | 每个变体完整转换 | 只需转换 main() 体（模块已预编译） |
| `.usf` 文件 | 完整 HLSL Shader | `#include` + main() |
| 变体数量 | 不变（由 Processor 组合决定） | 不变 |
| MockContext 需求 | 完整模拟环境 | 仅需 ProgramInfo 组装 |
| 调试难度 | 生成的 GLSL 不可读 | `.glsl` 模块可直接阅读 |

### 15.3 迁移路径

```
阶段 1: 离线枚举方案独立实施 (ue_shader_restructuring.md)
  → UE 后端可工作
  → 依赖 emitCode() 生成完整 GLSL

阶段 2: 模块化重构 Phase 1-3 (本文档)
  → GL/Metal 后端切换到模块化路径
  → UE 后端仍使用离线枚举方案

阶段 3: 模块化重构 Phase 4 (本文档)
  → UE 后端切换到模块化 + 离线枚举
  → 离线工具改用 ModularProgramBuilder
  → .usf 文件改为 #include + main()

阶段 4: 模块化重构 Phase 5 (本文档)
  → 清理旧代码
  → 所有后端统一使用模块化架构
```