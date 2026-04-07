// Copyright (C) 2026 Tencent. All rights reserved.
// diamond_gradient_layout.glsl - DiamondGradientLayout modular shader function.
// Takes a transformed coordinate and outputs the gradient t-value as vec4(t, 1.0, 0.0, 0.0).

tgfx_float4 FP_DiamondGradientLayout(tgfx_float2 coord) {
    float t = max(abs(coord.x), abs(coord.y));
    return tgfx_float4(t, 1.0, 0.0, 0.0);
}
