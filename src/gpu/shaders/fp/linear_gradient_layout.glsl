// Copyright (C) 2026 Tencent. All rights reserved.
// linear_gradient_layout.glsl - LinearGradientLayout modular shader function.
// Takes a transformed coordinate and outputs the gradient t-value as vec4(t, 1.0, 0.0, 0.0).

vec4 TGFX_LinearGradientLayout(vec2 coord) {
    float t = coord.x + 1.0000000000000001e-05;
    return vec4(t, 1.0, 0.0, 0.0);
}
