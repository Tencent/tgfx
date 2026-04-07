// Copyright (C) 2026 Tencent. All rights reserved.
// color_matrix.glsl - ColorMatrixFragmentProcessor modular shader function.
// Applies a 4x4 color matrix + offset vector to the input color.
// Input is unpremultiplied before matrix multiply, then re-premultiplied.

vec4 TGFX_ColorMatrix(vec4 inputColor, mat4 matrix, vec4 vector) {
    vec4 unpremul = vec4(inputColor.rgb / max(inputColor.a, 9.9999997473787516e-05),
                         inputColor.a);
    vec4 result = matrix * unpremul + vector;
    result = clamp(result, 0.0, 1.0);
    result.rgb *= result.a;
    return result;
}
