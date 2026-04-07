// Copyright (C) 2026 Tencent. All rights reserved.
// dual_interval_gradient_colorizer.glsl - DualIntervalGradientColorizer modular shader function.
// Performs piecewise linear interpolation across two intervals split by a threshold.

tgfx_float4 FP_DualIntervalGradientColorizer(tgfx_float4 inputColor,
    tgfx_float4 u_scale01, tgfx_float4 u_bias01,
    tgfx_float4 u_scale23, tgfx_float4 u_bias23, float u_threshold) {
    float t = inputColor.x;
    tgfx_float4 scale, bias;
    if (t < u_threshold) {
        scale = u_scale01;
        bias = u_bias01;
    } else {
        scale = u_scale23;
        bias = u_bias23;
    }
    return tgfx_float4(t * scale + bias);
}
