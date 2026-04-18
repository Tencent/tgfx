// Copyright (C) 2026 Tencent. All rights reserved.
// porter_duff_xfer.frag.glsl - PorterDuffXferProcessor fragment shader.
//
// Performs Porter-Duff blending with coverage modulation. The blending logic exactly mirrors
// the C++ BlendFormula generator with hasCoverage=true in BlendFormula.cpp: each mode's
// blended result is derived from (primaryOutput, secondaryOutput, srcFactor, dstFactor,
// operation) as defined in the Coeffs[1][N] table.
//
// Parameters:
//   inputColor (vec4)    - source color
//   inputCoverage (vec4) - per-channel coverage (0.0-1.0)
//   dstColor (vec4)      - destination color
// Output:
//   outputColor (vec4)   - final blended result
//
// Required macros:
//   TGFX_BLEND_MODE - integer selecting the blend formula (0=Clear, 1=Src, ... 29=PlusDarker).
//
// Optional macros:
//   TGFX_PDXP_DST_TEXTURE_READ - defined when destination is read from a texture via
//       gl_FragCoord; two overloads of TGFX_PorterDuffXP_FS are provided for sampler2D and
//       sampler2DRect (selected by GLSL at link time). The destination is sampled inside this
//       function and fully transparent fragments are discarded.
//   TGFX_PDXP_NON_COEFF - defined when the blend mode is in the advanced (non-coefficient)
//       range [15..29]. Coverage is applied as: result = coverage * blended + (1 - coverage) * dst.

// Internal helper: compute the final output given pre-resolved source, coverage and destination.
void TGFX_PorterDuffXP_Blend(vec4 inputColor, vec4 inputCoverage, vec4 dstColor,
                               out vec4 outputColor) {
#ifndef TGFX_PDXP_NON_COEFF
    // --- Coefficient-based blend modes (0..14) with coverage baked into the formula ---
    // Each branch exactly matches the formula produced by BlendFormula.cpp Coeffs[1][N]
    // combined with GLSLPorterDuffXferProcessor.cpp buildXferCallStatement(). See the
    // comments below for the effective formula.

#if TGFX_BLEND_MODE == 0
    // Clear: MakeCoverageSrcCoeffZeroFormula(Coverage)
    // primary = cov; op = ReverseSubtract; src = Dst; dst = One
    // => clamp(One*dst - Dst*primary, 0, 1) = clamp(dst*(1 - cov), 0, 1)
    outputColor = clamp(dstColor * (vec4(1.0) - inputCoverage), 0.0, 1.0);
#elif TGFX_BLEND_MODE == 1
    // Src: MakeCoverageDstCoeffZeroFormula(One)
    // primary = src*cov, secondary = cov; srcFactor = One; dstFactor = OneMinusSrc1Alpha
    // => clamp(primary + (1 - secondary.a)*dst, 0, 1) = clamp(src*cov + (1 - cov.a)*dst, 0, 1)
    outputColor = clamp(inputColor * inputCoverage + (1.0 - inputCoverage.a) * dstColor, 0.0, 1.0);
#elif TGFX_BLEND_MODE == 2
    // Dst: MakeCoeffFormula(Zero, One)
    // primary = None (vec4(0)); srcFactor = Zero; dstFactor = One
    // => clamp(vec4(0) + dst, 0, 1)
    outputColor = clamp(dstColor, 0.0, 1.0);
#elif TGFX_BLEND_MODE == 3
    // SrcOver: MakeCoeffFormula(One, OneMinusSrcAlpha)
    // primary = src*cov; srcFactor = One; dstFactor = OneMinusSrcAlpha
    // => clamp(primary + (1 - primary.a)*dst, 0, 1)
    vec4 primary = inputColor * inputCoverage;
    outputColor = clamp(primary + (1.0 - primary.a) * dstColor, 0.0, 1.0);
#elif TGFX_BLEND_MODE == 4
    // DstOver: MakeCoeffFormula(OneMinusDstAlpha, One)
    // primary = src*cov; srcFactor = OneMinusDstAlpha; dstFactor = One
    // => clamp(primary*(1 - dst.a) + dst, 0, 1)
    outputColor = clamp(inputColor * inputCoverage * (1.0 - dstColor.a) + dstColor, 0.0, 1.0);
#elif TGFX_BLEND_MODE == 5
    // SrcIn: MakeCoverageDstCoeffZeroFormula(DstAlpha)
    // primary = src*cov, secondary = cov; srcFactor = DstAlpha; dstFactor = OneMinusSrc1Alpha
    // => clamp(primary*dst.a + (1 - secondary.a)*dst, 0, 1)
    outputColor = clamp(inputColor * inputCoverage * dstColor.a +
                        (1.0 - inputCoverage.a) * dstColor, 0.0, 1.0);
#elif TGFX_BLEND_MODE == 6
    // DstIn: MakeCoverageSrcCoeffZeroFormula(ISAModulate)
    // primary = (1 - src.a)*cov; op = ReverseSubtract; src = Dst; dst = One
    // => clamp(dst - primary*dst, 0, 1) = clamp(dst*(1 - (1 - src.a)*cov), 0, 1)
    outputColor = clamp(dstColor * (vec4(1.0) - (1.0 - inputColor.a) * inputCoverage), 0.0, 1.0);
#elif TGFX_BLEND_MODE == 7
    // SrcOut: MakeCoverageDstCoeffZeroFormula(OneMinusDstAlpha)
    // primary = src*cov, secondary = cov; srcFactor = OneMinusDstAlpha; dstFactor = OneMinusSrc1Alpha
    // => clamp(primary*(1 - dst.a) + (1 - secondary.a)*dst, 0, 1)
    outputColor = clamp(inputColor * inputCoverage * (1.0 - dstColor.a) +
                        (1.0 - inputCoverage.a) * dstColor, 0.0, 1.0);
#elif TGFX_BLEND_MODE == 8
    // DstOut: MakeCoeffFormula(Zero, OneMinusSrcAlpha)
    // primary = None (vec4(0)); srcFactor = Zero; dstFactor = OneMinusSrcAlpha (applied to primary.a)
    // Matches C++ generator: srcTerm = vec4(0); dstTerm = dst * (1 - 0.a) = dst.
    // Result => clamp(vec4(0) + dst, 0, 1)
    outputColor = clamp(dstColor, 0.0, 1.0);
#elif TGFX_BLEND_MODE == 9
    // SrcATop: MakeCoeffFormula(DstAlpha, OneMinusSrcAlpha)
    // primary = src*cov; srcFactor = DstAlpha; dstFactor = OneMinusSrcAlpha
    // => clamp(primary*dst.a + (1 - primary.a)*dst, 0, 1)
    vec4 primary = inputColor * inputCoverage;
    outputColor = clamp(primary * dstColor.a + (1.0 - primary.a) * dstColor, 0.0, 1.0);
#elif TGFX_BLEND_MODE == 10
    // DstATop: MakeCoverageFormula(ISAModulate, OneMinusDstAlpha)
    // primary = src*cov, secondary = (1 - src.a)*cov;
    // srcFactor = OneMinusDstAlpha; dstFactor = OneMinusSrc1
    // => clamp(primary*(1 - dst.a) + (1 - secondary)*dst, 0, 1)
    vec4 secondary = (1.0 - inputColor.a) * inputCoverage;
    outputColor = clamp(inputColor * inputCoverage * (1.0 - dstColor.a) +
                        (vec4(1.0) - secondary) * dstColor, 0.0, 1.0);
#elif TGFX_BLEND_MODE == 11
    // Xor: MakeCoeffFormula(OneMinusDstAlpha, OneMinusSrcAlpha)
    // primary = src*cov; srcFactor = OneMinusDstAlpha; dstFactor = OneMinusSrcAlpha
    // => clamp(primary*(1 - dst.a) + (1 - primary.a)*dst, 0, 1)
    vec4 primary = inputColor * inputCoverage;
    outputColor = clamp(primary * (1.0 - dstColor.a) + (1.0 - primary.a) * dstColor, 0.0, 1.0);
#elif TGFX_BLEND_MODE == 12
    // PlusLighter: MakeCoeffFormula(One, One)
    // primary = src*cov; srcFactor = One; dstFactor = One
    // => clamp(primary + dst, 0, 1)
    outputColor = clamp(inputColor * inputCoverage + dstColor, 0.0, 1.0);
#elif TGFX_BLEND_MODE == 13
    // Modulate: MakeCoverageSrcCoeffZeroFormula(ISCModulate)
    // primary = (1 - src)*cov; op = ReverseSubtract; src = Dst; dst = One
    // => clamp(dst - primary*dst, 0, 1) = clamp(dst*(1 - (1 - src)*cov), 0, 1)
    outputColor = clamp(dstColor * (vec4(1.0) - (vec4(1.0) - inputColor) * inputCoverage),
                        0.0, 1.0);
#elif TGFX_BLEND_MODE == 14
    // Screen: MakeCoeffFormula(One, OneMinusSrc)
    // primary = src*cov; srcFactor = One; dstFactor = OneMinusSrc
    // => clamp(primary + (1 - primary)*dst, 0, 1)
    vec4 primary = inputColor * inputCoverage;
    outputColor = clamp(primary + (vec4(1.0) - primary) * dstColor, 0.0, 1.0);
#else
    // Unexpected coefficient mode - fall back to SrcOver.
    vec4 primary = inputColor * inputCoverage;
    outputColor = clamp(primary + (1.0 - primary.a) * dstColor, 0.0, 1.0);
#endif

#else  // TGFX_PDXP_NON_COEFF: advanced blend modes (15..29)

    vec4 blended;
#if TGFX_BLEND_MODE == 15
    blended = tgfx_blend_overlay(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 16
    blended = tgfx_blend_darken(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 17
    blended = tgfx_blend_lighten(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 18
    blended = tgfx_blend_color_dodge(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 19
    blended = tgfx_blend_color_burn(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 20
    blended = tgfx_blend_hard_light(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 21
    blended = tgfx_blend_soft_light(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 22
    blended = tgfx_blend_difference(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 23
    blended = tgfx_blend_exclusion(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 24
    blended = tgfx_blend_multiply(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 25
    blended = tgfx_blend_hue(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 26
    blended = tgfx_blend_saturation(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 27
    blended = tgfx_blend_color(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 28
    blended = tgfx_blend_luminosity(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 29
    blended = tgfx_blend_plus_darker(inputColor, dstColor);
#else
    blended = inputColor + (1.0 - inputColor.a) * dstColor;  // Fallback to SrcOver
#endif
    // Apply coverage as a lerp between blended and destination.
    outputColor = inputCoverage * blended + (vec4(1.0) - inputCoverage) * dstColor;

#endif  // TGFX_PDXP_NON_COEFF
}

#ifdef TGFX_PDXP_DST_TEXTURE_READ
// sampler2D overload: samples destination from a normalized-coordinate texture.
void TGFX_PorterDuffXP_FS(vec4 inputColor, vec4 inputCoverage,
                            vec2 dstTextureUpperLeft, vec2 dstTextureCoordScale,
                            sampler2D dstTextureSampler,
                            out vec4 outputColor) {
    if (inputCoverage.r <= 0.0 && inputCoverage.g <= 0.0 && inputCoverage.b <= 0.0) {
        discard;
    }
    vec2 dstTexCoord = (gl_FragCoord.xy - dstTextureUpperLeft) * dstTextureCoordScale;
    vec4 dstColor = texture(dstTextureSampler, dstTexCoord);
    TGFX_PorterDuffXP_Blend(inputColor, inputCoverage, dstColor, outputColor);
}

// sampler2DRect overload: samples destination from an unnormalized rectangle texture. The
// uniform scales encode 1/width and 1/height; this overload re-multiplies by the rectangle
// extent (width, height = 1/scale) to get unnormalized coordinates expected by texture().
void TGFX_PorterDuffXP_FS(vec4 inputColor, vec4 inputCoverage,
                            vec2 dstTextureUpperLeft, vec2 dstTextureCoordScale,
                            sampler2DRect dstTextureSampler,
                            out vec4 outputColor) {
    if (inputCoverage.r <= 0.0 && inputCoverage.g <= 0.0 && inputCoverage.b <= 0.0) {
        discard;
    }
    // For Rectangle textures the C++ path sets dstTextureCoordScale = (1, 1), so
    // dstTexCoord below is already in unnormalized fragment-space pixel coordinates.
    vec2 dstTexCoord = (gl_FragCoord.xy - dstTextureUpperLeft) * dstTextureCoordScale;
    vec4 dstColor = texture(dstTextureSampler, dstTexCoord);
    TGFX_PorterDuffXP_Blend(inputColor, inputCoverage, dstColor, outputColor);
}
#else
// No-destination-texture overload: dstColor is supplied by the caller (e.g. via
// framebuffer-fetch or a precomputed expression).
void TGFX_PorterDuffXP_FS(vec4 inputColor, vec4 inputCoverage, vec4 dstColor,
                            out vec4 outputColor) {
    TGFX_PorterDuffXP_Blend(inputColor, inputCoverage, dstColor, outputColor);
}
#endif
