// Copyright (C) 2026 Tencent. All rights reserved.
// color_matrix.glsl - ColorMatrixFragmentProcessor modular shader function.
// Applies a 4x4 color matrix + offset vector to the input color.
// Input is unpremultiplied before matrix multiply, then re-premultiplied.

tgfx_float4 FP_ColorMatrix(tgfx_float4 inputColor, mat4 u_Matrix, tgfx_float4 u_Vector) {
    tgfx_float4 unpremul = tgfx_float4(inputColor.rgb / max(inputColor.a, 9.9999997473787516e-05),
                                         inputColor.a);
    tgfx_float4 result = u_Matrix * unpremul + u_Vector;
    result = clamp(result, 0.0, 1.0);
    result.rgb *= result.a;
    return result;
}
