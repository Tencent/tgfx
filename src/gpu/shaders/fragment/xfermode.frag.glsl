// Copyright (C) 2026 Tencent. All rights reserved.
// xfermode.frag.glsl - XfermodeFragmentProcessor modular shader function.
// Composes child FP colors using a blend mode. Requires tgfx_blend.glsl for advanced modes.
//
// Required macros:
//   TGFX_XFP_CHILD_MODE - 0=DstChild, 1=SrcChild, 2=TwoChild
//   TGFX_BLEND_MODE     - blend mode index (0=Clear through 29=PlusDarker)

#ifndef TGFX_XFP_CHILD_MODE
#define TGFX_XFP_CHILD_MODE 0
#endif

#ifndef TGFX_BLEND_MODE
#define TGFX_BLEND_MODE 3
#endif

vec4 tgfx_xfp_blend(vec4 src, vec4 dst) {
#if TGFX_BLEND_MODE == 0
    return vec4(0.0);
#elif TGFX_BLEND_MODE == 1
    return src;
#elif TGFX_BLEND_MODE == 2
    return dst;
#elif TGFX_BLEND_MODE == 3
    return src + (1.0 - src.a) * dst;
#elif TGFX_BLEND_MODE == 4
    return (1.0 - dst.a) * src + dst;
#elif TGFX_BLEND_MODE == 5
    return dst.a * src;
#elif TGFX_BLEND_MODE == 6
    return src.a * dst;
#elif TGFX_BLEND_MODE == 7
    return (1.0 - dst.a) * src;
#elif TGFX_BLEND_MODE == 8
    return (1.0 - src.a) * dst;
#elif TGFX_BLEND_MODE == 9
    return dst.a * src + (1.0 - src.a) * dst;
#elif TGFX_BLEND_MODE == 10
    return (1.0 - dst.a) * src + src.a * dst;
#elif TGFX_BLEND_MODE == 11
    return (1.0 - dst.a) * src + (1.0 - src.a) * dst;
#elif TGFX_BLEND_MODE == 12
    return min(src + dst, vec4(1.0));
#elif TGFX_BLEND_MODE == 13
    return src * dst;
#elif TGFX_BLEND_MODE == 14
    return src + dst - src * dst;
#elif TGFX_BLEND_MODE == 15
    return tgfx_blend_overlay(src, dst);
#elif TGFX_BLEND_MODE == 16
    return tgfx_blend_darken(src, dst);
#elif TGFX_BLEND_MODE == 17
    return tgfx_blend_lighten(src, dst);
#elif TGFX_BLEND_MODE == 18
    return tgfx_blend_color_dodge(src, dst);
#elif TGFX_BLEND_MODE == 19
    return tgfx_blend_color_burn(src, dst);
#elif TGFX_BLEND_MODE == 20
    return tgfx_blend_hard_light(src, dst);
#elif TGFX_BLEND_MODE == 21
    return tgfx_blend_soft_light(src, dst);
#elif TGFX_BLEND_MODE == 22
    return tgfx_blend_difference(src, dst);
#elif TGFX_BLEND_MODE == 23
    return tgfx_blend_exclusion(src, dst);
#elif TGFX_BLEND_MODE == 24
    return tgfx_blend_multiply(src, dst);
#elif TGFX_BLEND_MODE == 25
    return tgfx_blend_hue(src, dst);
#elif TGFX_BLEND_MODE == 26
    return tgfx_blend_saturation(src, dst);
#elif TGFX_BLEND_MODE == 27
    return tgfx_blend_color(src, dst);
#elif TGFX_BLEND_MODE == 28
    return tgfx_blend_luminosity(src, dst);
#elif TGFX_BLEND_MODE == 29
    return tgfx_blend_plus_darker(src, dst);
#else
    return src + (1.0 - src.a) * dst;
#endif
}

#if TGFX_XFP_CHILD_MODE == 2
vec4 TGFX_XfermodeFragmentProcessor(vec4 inputColor, vec4 childSrc, vec4 childDst) {
    vec4 result = tgfx_xfp_blend(childSrc, childDst);
    result *= inputColor.a;
    return result;
}
#elif TGFX_XFP_CHILD_MODE == 0
vec4 TGFX_XfermodeFragmentProcessor(vec4 inputColor, vec4 childDst) {
    return tgfx_xfp_blend(inputColor, childDst);
}
#elif TGFX_XFP_CHILD_MODE == 1
vec4 TGFX_XfermodeFragmentProcessor(vec4 inputColor, vec4 childSrc) {
    return tgfx_xfp_blend(childSrc, inputColor);
}
#endif
