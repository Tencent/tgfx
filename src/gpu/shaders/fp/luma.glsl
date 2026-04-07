// Copyright (C) 2026 Tencent. All rights reserved.
// luma.glsl - LumaFragmentProcessor modular shader function.
// Converts input color to luminance using configurable coefficients.

tgfx_float4 FP_Luma(tgfx_float4 inputColor, float u_Kr, float u_Kg, float u_Kb) {
    float luma = dot(inputColor.rgb, tgfx_float3(u_Kr, u_Kg, u_Kb));
    return tgfx_float4(luma);
}
