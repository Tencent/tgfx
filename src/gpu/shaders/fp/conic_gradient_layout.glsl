// Copyright (C) 2026 Tencent. All rights reserved.
// conic_gradient_layout.glsl - ConicGradientLayout modular shader function.
// Takes a transformed coordinate + bias/scale uniforms, outputs gradient t-value.

tgfx_float4 FP_ConicGradientLayout(tgfx_float2 coord, float u_Bias, float u_Scale) {
    float angle = atan(-coord.y, -coord.x);
    float t = ((angle * 0.15915494309180001 + 0.5) + u_Bias) * u_Scale;
    return tgfx_float4(t, 1.0, 0.0, 0.0);
}
