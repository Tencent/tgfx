// Copyright (C) 2026 Tencent. All rights reserved.
// diamond_gradient_layout.glsl - DiamondGradientLayout modular shader function.
// Takes a transformed coordinate and outputs the gradient t-value as vec4(t, 1.0, 0.0, 0.0).

vec4 TGFX_DiamondGradientLayout(vec2 coord) {
    float t = max(abs(coord.x), abs(coord.y));
    return vec4(t, 1.0, 0.0, 0.0);
}
