// Copyright (C) 2026 Tencent. All rights reserved.
// device_space_texture_effect.glsl - DeviceSpaceTextureEffect modular shader function.
// Samples a texture using device-space coordinates (gl_FragCoord) transformed by a matrix.
// Compile switch: TGFX_DST_IS_ALPHA_ONLY
//   0 = output premultiplied color (color * color.a)
//   1 = output alpha modulated by inputColor (color.a * inputColor)

vec4 TGFX_DeviceSpaceTextureEffect(vec4 inputColor, sampler2D textureSampler,
                                    mat3 deviceCoordMatrix) {
    vec3 deviceCoord = deviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
    vec4 color = texture(textureSampler, deviceCoord.xy);
#if TGFX_DST_IS_ALPHA_ONLY
    return color.a * inputColor;
#else
    return color * color.a;
#endif
}
