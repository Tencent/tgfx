// Copyright (C) 2026 Tencent. All rights reserved.
// dual_interval_gradient_colorizer.glsl - DualIntervalGradientColorizer modular shader function.
// Performs piecewise linear interpolation across two intervals split by a threshold.

vec4 TGFX_DualIntervalGradientColorizer(vec4 inputColor,
    vec4 scale01, vec4 bias01,
    vec4 scale23, vec4 bias23, float threshold) {
    float t = inputColor.x;
    vec4 scale, bias;
    if (t < threshold) {
        scale = scale01;
        bias = bias01;
    } else {
        scale = scale23;
        bias = bias23;
    }
    return vec4(t * scale + bias);
}
