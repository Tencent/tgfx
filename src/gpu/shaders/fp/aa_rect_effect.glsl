// Copyright (C) 2026 Tencent. All rights reserved.
// aa_rect_effect.glsl - AARectEffect modular shader function.
// Computes anti-aliased rectangle coverage using gl_FragCoord.
// rect = vec4(left, top, right, bottom) outset by 0.5 for AA math.

vec4 TGFX_AARectEffect(vec4 inputColor, vec4 rect) {
    vec4 dists4 = clamp(vec4(1.0, 1.0, -1.0, -1.0) *
                        (gl_FragCoord.xyxy - rect), 0.0, 1.0);
    vec2 dists2 = dists4.xy + dists4.zw - 1.0;
    float coverage = dists2.x * dists2.y;
    return inputColor * coverage;
}
