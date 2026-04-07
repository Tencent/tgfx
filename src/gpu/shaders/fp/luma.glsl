// Copyright (C) 2026 Tencent. All rights reserved.
// luma.glsl - LumaFragmentProcessor modular shader function.
// Converts input color to luminance using configurable coefficients.

vec4 TGFX_Luma(vec4 inputColor, float kr, float kg, float kb) {
    float luma = dot(inputColor.rgb, vec3(kr, kg, kb));
    return vec4(luma);
}
