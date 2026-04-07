// Copyright (C) 2026 Tencent. All rights reserved.
// conic_gradient_layout.glsl - ConicGradientLayout modular shader function.
// Takes a transformed coordinate + bias/scale uniforms, outputs gradient t-value.

vec4 TGFX_ConicGradientLayout(vec2 coord, float bias, float scale) {
    float angle = atan(-coord.y, -coord.x);
    float t = ((angle * 0.15915494309180001 + 0.5) + bias) * scale;
    return vec4(t, 1.0, 0.0, 0.0);
}
