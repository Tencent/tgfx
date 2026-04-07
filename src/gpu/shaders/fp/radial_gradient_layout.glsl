// Copyright (C) 2026 Tencent. All rights reserved.
// radial_gradient_layout.glsl - RadialGradientLayout modular shader function.
// Takes a transformed coordinate and outputs the gradient t-value as vec4(t, 1.0, 0.0, 0.0).

tgfx_float4 FP_RadialGradientLayout(tgfx_float2 coord) {
    float t = length(coord);
    return tgfx_float4(t, 1.0, 0.0, 0.0);
}
