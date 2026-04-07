// Copyright (C) 2026 Tencent. All rights reserved.
// texture_gradient_colorizer.glsl - TextureGradientColorizer modular shader function.
// Samples a 1D gradient texture using the t-value from inputColor.x.

tgfx_float4 FP_TextureGradientColorizer(tgfx_float4 inputColor, sampler2D u_Sampler) {
    tgfx_float2 coord = tgfx_float2(inputColor.x, 0.5);
    return texture(u_Sampler, coord);
}
