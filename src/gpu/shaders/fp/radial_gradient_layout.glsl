// Copyright (C) 2026 Tencent. All rights reserved.
// radial_gradient_layout.glsl - RadialGradientLayout modular shader function.
// Takes a transformed coordinate and outputs the gradient t-value as vec4(t, 1.0, 0.0, 0.0).

vec4 TGFX_RadialGradientLayout(vec2 coord) {
    float t = length(coord);
    return vec4(t, 1.0, 0.0, 0.0);
}
