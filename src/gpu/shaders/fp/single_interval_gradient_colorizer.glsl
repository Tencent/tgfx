// Copyright (C) 2026 Tencent. All rights reserved.
// single_interval_gradient_colorizer.glsl - SingleIntervalGradientColorizer modular shader function.
// Performs linear interpolation between u_start and u_end based on inputColor.x (the t-value).

tgfx_float4 FP_SingleIntervalGradientColorizer(tgfx_float4 inputColor,
                                                 tgfx_float4 u_start,
                                                 tgfx_float4 u_end) {
    float t = inputColor.x;
    return (1.0 - t) * u_start + t * u_end;
}
