// Copyright (C) 2026 Tencent. All rights reserved.
// aa_rect_effect.glsl - AARectEffect modular shader function.
// Computes anti-aliased rectangle coverage using gl_FragCoord.
// u_Rect = vec4(left, top, right, bottom) outset by 0.5 for AA math.

tgfx_float4 FP_AARectEffect(tgfx_float4 inputColor, tgfx_float4 u_Rect) {
    tgfx_float4 dists4 = clamp(tgfx_float4(1.0, 1.0, -1.0, -1.0) *
                                (gl_FragCoord.xyxy - u_Rect), 0.0, 1.0);
    tgfx_float2 dists2 = dists4.xy + dists4.zw - 1.0;
    float coverage = dists2.x * dists2.y;
    return inputColor * coverage;
}
