// Copyright (C) 2026 Tencent. All rights reserved.
// color_space_xform.frag.glsl - ColorSpaceXformEffect modular shader function.
// Performs color space transformation with parametric transfer functions.
//
// Required macros (all optional, omit to skip the step):
//   TGFX_CSX_UNPREMUL     - defined if unpremultiply input
//   TGFX_CSX_SRC_TF       - defined if apply source transfer function
//   TGFX_CSX_SRC_TF_TYPE  - TF type: 1=sRGBish, 2=PQish, 3=HLGish, 4=HLGinvish
//   TGFX_CSX_SRC_OOTF     - defined if apply source OOTF
//   TGFX_CSX_GAMUT_XFORM  - defined if apply gamut transform matrix
//   TGFX_CSX_DST_OOTF     - defined if apply destination OOTF
//   TGFX_CSX_DST_TF       - defined if apply destination transfer function
//   TGFX_CSX_DST_TF_TYPE  - TF type: 1=sRGBish, 2=PQish, 3=HLGish, 4=HLGinvish
//   TGFX_CSX_PREMUL       - defined if premultiply output
//
// All 7 uniform parameters are always present in the signature. Unused ones receive dummy values.

#ifdef TGFX_CSX_SRC_TF
float tgfx_csx_src_tf(float x, vec4 tf0, vec4 tf1) {
    float G = tf0[0], A = tf0[1], B = tf0[2], C = tf0[3];
    float D = tf1[0], E = tf1[1], F = tf1[2];
    float s = sign(x);
    x = abs(x);
#if TGFX_CSX_SRC_TF_TYPE == 1
    x = (x < D) ? (C * x) + F : pow(A * x + B, G) + E;
#elif TGFX_CSX_SRC_TF_TYPE == 2
    x = pow(max(A + B * pow(x, C), 0.0) / (D + E * pow(x, C)), F);
#elif TGFX_CSX_SRC_TF_TYPE == 3
    x = (x * A <= 1.0) ? pow(x * A, B) : exp((x - E) * C) + D;
    x *= (F + 1.0);
#elif TGFX_CSX_SRC_TF_TYPE == 4
    x /= (F + 1.0);
    x = (x <= 1.0) ? A * pow(x, B) : C * log(x - D) + E;
#endif
    return s * x;
}
#endif

#ifdef TGFX_CSX_DST_TF
float tgfx_csx_dst_tf(float x, vec4 tf0, vec4 tf1) {
    float G = tf0[0], A = tf0[1], B = tf0[2], C = tf0[3];
    float D = tf1[0], E = tf1[1], F = tf1[2];
    float s = sign(x);
    x = abs(x);
#if TGFX_CSX_DST_TF_TYPE == 1
    x = (x < D) ? (C * x) + F : pow(A * x + B, G) + E;
#elif TGFX_CSX_DST_TF_TYPE == 2
    x = pow(max(A + B * pow(x, C), 0.0) / (D + E * pow(x, C)), F);
#elif TGFX_CSX_DST_TF_TYPE == 3
    x = (x * A <= 1.0) ? pow(x * A, B) : exp((x - E) * C) + D;
    x *= (F + 1.0);
#elif TGFX_CSX_DST_TF_TYPE == 4
    x /= (F + 1.0);
    x = (x <= 1.0) ? A * pow(x, B) : C * log(x - D) + E;
#endif
    return s * x;
}
#endif

#ifdef TGFX_CSX_SRC_OOTF
vec3 tgfx_csx_src_ootf(vec3 color, vec4 params) {
    float Y = dot(color, params.rgb);
    return color * sign(Y) * pow(abs(Y), params.a);
}
#endif

#ifdef TGFX_CSX_DST_OOTF
vec3 tgfx_csx_dst_ootf(vec3 color, vec4 params) {
    float Y = dot(color, params.rgb);
    return color * sign(Y) * pow(abs(Y), params.a);
}
#endif

vec4 TGFX_ColorSpaceXformEffect(vec4 color,
                                 vec4 srcTF0, vec4 srcTF1,
                                 vec4 srcOOTFParams,
                                 mat3 colorXform,
                                 vec4 dstOOTFParams,
                                 vec4 dstTF0, vec4 dstTF1) {
#ifdef TGFX_CSX_UNPREMUL
    float alpha = color.a;
    color = alpha > 0.0 ? vec4(color.rgb / alpha, alpha) : vec4(0.0);
#endif

#ifdef TGFX_CSX_SRC_TF
    color.r = tgfx_csx_src_tf(color.r, srcTF0, srcTF1);
    color.g = tgfx_csx_src_tf(color.g, srcTF0, srcTF1);
    color.b = tgfx_csx_src_tf(color.b, srcTF0, srcTF1);
#endif

#ifdef TGFX_CSX_SRC_OOTF
    color.rgb = tgfx_csx_src_ootf(color.rgb, srcOOTFParams);
#endif

#ifdef TGFX_CSX_GAMUT_XFORM
    color.rgb = colorXform * color.rgb;
#endif

#ifdef TGFX_CSX_DST_OOTF
    color.rgb = tgfx_csx_dst_ootf(color.rgb, dstOOTFParams);
#endif

#ifdef TGFX_CSX_DST_TF
    color.r = tgfx_csx_dst_tf(color.r, dstTF0, dstTF1);
    color.g = tgfx_csx_dst_tf(color.g, dstTF0, dstTF1);
    color.b = tgfx_csx_dst_tf(color.b, dstTF0, dstTF1);
#endif

#ifdef TGFX_CSX_PREMUL
    color.rgb *= color.a;
#endif

    return color;
}
