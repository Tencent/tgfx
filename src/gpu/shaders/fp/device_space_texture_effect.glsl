// Copyright (C) 2026 Tencent. All rights reserved.
// device_space_texture_effect.glsl - DeviceSpaceTextureEffect modular shader function.
// Samples a texture using device-space coordinates (gl_FragCoord) transformed by a matrix.
// Compile switch: TGFX_DST_IS_ALPHA_ONLY (0 or 1)

tgfx_float4 FP_DeviceSpaceTextureEffect(tgfx_float4 inputColor, sampler2D u_Sampler,
                                          mat3 u_DeviceCoordMatrix) {
    tgfx_float3 deviceCoord = u_DeviceCoordMatrix * tgfx_float3(gl_FragCoord.xy, 1.0);
    tgfx_float4 color = texture(u_Sampler, deviceCoord.xy);
#if TGFX_DST_IS_ALPHA_ONLY
    return color.a * inputColor;
#else
    return color * color.a;
#endif
}
