// Copyright (C) 2026 Tencent. All rights reserved.
// texture_gradient_colorizer.glsl - TextureGradientColorizer modular shader function.
// Samples a 1D gradient texture using the t-value from inputColor.x.

vec4 TGFX_TextureGradientColorizer(vec4 inputColor, sampler2D gradientSampler) {
    vec2 coord = vec2(inputColor.x, 0.5);
    return texture(gradientSampler, coord);
}
