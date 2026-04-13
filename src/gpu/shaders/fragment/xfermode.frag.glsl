// Copyright (C) 2026 Tencent. All rights reserved.
// xfermode.frag.glsl - XfermodeFragmentProcessor modular shader function.
// Composes child FP colors using a blend mode. Requires tgfx_blend.glsl for advanced modes.
//
// Macro-free design: blend mode and child mode are passed as int parameters
// so multiple XfermodeFragmentProcessor instances with different modes can coexist
// in the same shader without macro redefinition conflicts.
//
// Parameters:
//   inputColor - incoming color from the pipeline
//   childSrc   - output from src child FP (vec4(0.0) if unused)
//   childDst   - output from dst child FP (vec4(0.0) if unused)
//   blendMode  - BlendMode enum value (0=Clear through 29=PlusDarker)
//   childMode  - 0=DstChild, 1=SrcChild, 2=TwoChild

// Coefficient blend for modes 0-14, advanced blend for modes 15-29.
vec4 tgfx_xfp_blend(vec4 src, vec4 dst, int blendMode) {
    if (blendMode == 0) return vec4(0.0);
    if (blendMode == 1) return src;
    if (blendMode == 2) return dst;
    if (blendMode == 3) return src + (1.0 - src.a) * dst;
    if (blendMode == 4) return (1.0 - dst.a) * src + dst;
    if (blendMode == 5) return dst.a * src;
    if (blendMode == 6) return src.a * dst;
    if (blendMode == 7) return (1.0 - dst.a) * src;
    if (blendMode == 8) return (1.0 - src.a) * dst;
    if (blendMode == 9) return dst.a * src + (1.0 - src.a) * dst;
    if (blendMode == 10) return (1.0 - dst.a) * src + src.a * dst;
    if (blendMode == 11) return (1.0 - dst.a) * src + (1.0 - src.a) * dst;
    if (blendMode == 12) return min(src + dst, vec4(1.0));
    if (blendMode == 13) return src * dst;
    if (blendMode == 14) return src + dst - src * dst;
    return tgfx_blend(src, dst, blendMode);
}

// Unified entry point for all child modes.
vec4 TGFX_XfermodeFragmentProcessor(vec4 inputColor, vec4 childSrc, vec4 childDst,
                                     int blendMode, int childMode) {
    if (childMode == 2) {
        vec4 result = tgfx_xfp_blend(childSrc, childDst, blendMode);
        result *= inputColor.a;
        return result;
    } else if (childMode == 0) {
        return tgfx_xfp_blend(inputColor, childDst, blendMode);
    } else {
        return tgfx_xfp_blend(childSrc, inputColor, blendMode);
    }
}
