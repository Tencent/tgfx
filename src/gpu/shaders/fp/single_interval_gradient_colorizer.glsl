// Copyright (C) 2026 Tencent. All rights reserved.
// single_interval_gradient_colorizer.glsl - SingleIntervalGradientColorizer modular shader function.
// Performs linear interpolation between start and end based on inputColor.x (the t-value).

vec4 TGFX_SingleIntervalGradientColorizer(vec4 inputColor, vec4 start, vec4 end) {
    float t = inputColor.x;
    return (1.0 - t) * start + t * end;
}
