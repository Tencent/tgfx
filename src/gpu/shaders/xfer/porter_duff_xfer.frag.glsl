// Copyright (C) 2026 Tencent. All rights reserved.
// porter_duff_xfer.frag.glsl - PorterDuffXferProcessor fragment shader.
// Performs Porter-Duff blending of source against destination with coverage modulation.
//
// Parameters: inputColor (vec4), inputCoverage (vec4), dstColor (vec4)
// Output: outputColor (vec4)
//
// Required macros:
//   TGFX_BLEND_MODE - integer selecting the blend formula (0=Clear, 1=Src, 2=Dst, etc.)
//
// Optional macros:
//   TGFX_PDXP_DST_TEXTURE_READ - defined when destination is read from a texture via
//       gl_FragCoord; provides DstTextureUpperLeft (vec2) and DstTextureCoordScale (vec2) uniforms
//       and the DstTextureSampler sampler.
//   TGFX_PDXP_NON_COEFF - defined when the blend mode cannot be expressed as simple
//       src/dst coefficient pairs and requires per-component blend evaluation. When set,
//       coverage is applied as: result = coverage * blended + (1 - coverage) * dst.

#ifdef TGFX_PDXP_DST_TEXTURE_READ
void TGFX_PorterDuffXP_FS(vec4 inputColor, vec4 inputCoverage,
                            vec2 dstTextureUpperLeft, vec2 dstTextureCoordScale,
                            sampler2D dstTextureSampler,
                            out vec4 outputColor) {
    // Discard fully transparent fragments to avoid destination read overhead.
    if (inputCoverage.r <= 0.0 && inputCoverage.g <= 0.0 && inputCoverage.b <= 0.0) {
        discard;
    }

    // Read destination color from the copy texture.
    vec2 dstTexCoord = (gl_FragCoord.xy - dstTextureUpperLeft) * dstTextureCoordScale;
    vec4 dstColor = texture(dstTextureSampler, dstTexCoord);
#else
void TGFX_PorterDuffXP_FS(vec4 inputColor, vec4 inputCoverage, vec4 dstColor,
                            out vec4 outputColor) {
#endif

    // Compute blended result based on blend mode.
    vec4 blended;

    // Common coefficient-based blend modes.
    // Porter-Duff formula: result = srcCoeff * src + dstCoeff * dst
    //   Clear:    srcCoeff = 0, dstCoeff = 0
    //   Src:      srcCoeff = 1, dstCoeff = 0
    //   Dst:      srcCoeff = 0, dstCoeff = 1
    //   SrcOver:  srcCoeff = 1, dstCoeff = (1 - srcAlpha)
    //   DstOver:  srcCoeff = (1 - dstAlpha), dstCoeff = 1
    //   SrcIn:    srcCoeff = dstAlpha, dstCoeff = 0
    //   DstIn:    srcCoeff = 0, dstCoeff = srcAlpha
    //   SrcOut:   srcCoeff = (1 - dstAlpha), dstCoeff = 0
    //   DstOut:   srcCoeff = 0, dstCoeff = (1 - srcAlpha)
    //   SrcATop:  srcCoeff = dstAlpha, dstCoeff = (1 - srcAlpha)
    //   DstATop:  srcCoeff = (1 - dstAlpha), dstCoeff = srcAlpha
    //   Xor:      srcCoeff = (1 - dstAlpha), dstCoeff = (1 - srcAlpha)
    //   Modulate: srcCoeff = 0, dstCoeff = src (component-wise multiply)
    //   Screen:   result = src + dst - src * dst

#if TGFX_BLEND_MODE == 0
    // Clear
    blended = vec4(0.0);
#elif TGFX_BLEND_MODE == 1
    // Src
    blended = inputColor;
#elif TGFX_BLEND_MODE == 2
    // Dst
    blended = dstColor;
#elif TGFX_BLEND_MODE == 3
    // SrcOver
    blended = inputColor + (1.0 - inputColor.a) * dstColor;
#elif TGFX_BLEND_MODE == 4
    // DstOver
    blended = (1.0 - dstColor.a) * inputColor + dstColor;
#elif TGFX_BLEND_MODE == 5
    // SrcIn
    blended = dstColor.a * inputColor;
#elif TGFX_BLEND_MODE == 6
    // DstIn
    blended = inputColor.a * dstColor;
#elif TGFX_BLEND_MODE == 7
    // SrcOut
    blended = (1.0 - dstColor.a) * inputColor;
#elif TGFX_BLEND_MODE == 8
    // DstOut
    blended = (1.0 - inputColor.a) * dstColor;
#elif TGFX_BLEND_MODE == 9
    // SrcATop
    blended = dstColor.a * inputColor + (1.0 - inputColor.a) * dstColor;
#elif TGFX_BLEND_MODE == 10
    // DstATop
    blended = (1.0 - dstColor.a) * inputColor + inputColor.a * dstColor;
#elif TGFX_BLEND_MODE == 11
    // Xor
    blended = (1.0 - dstColor.a) * inputColor + (1.0 - inputColor.a) * dstColor;
#elif TGFX_BLEND_MODE == 12
    // PlusLighter
    blended = min(inputColor + dstColor, vec4(1.0));
#elif TGFX_BLEND_MODE == 13
    // Modulate
    blended = inputColor * dstColor;
#elif TGFX_BLEND_MODE == 14
    // Screen
    blended = inputColor + dstColor - inputColor * dstColor;
#else
    // Advanced blend modes (15=Overlay through 29=PlusDarker).
    // These require the tgfx_blend utility functions (included via BlendModes module).
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
#endif

#ifdef TGFX_PDXP_NON_COEFF
    // For non-coefficient blend modes, apply coverage as a lerp between blended and dst.
    outputColor = inputCoverage * blended + (vec4(1.0) - inputCoverage) * dstColor;
#else
    outputColor = blended;
#endif
}
