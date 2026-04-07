// Copyright (C) 2026 Tencent. All rights reserved.
// empty_xfer.frag.glsl - EmptyXferProcessor fragment shader.
// Applies coverage to the input color with no blending against destination.
//
// Parameters: inputColor (vec4), inputCoverage (vec4)
// Output: outputColor (vec4)
//
// Required macros:
//   (none)

void TGFX_EmptyXP_FS(vec4 inputColor, vec4 inputCoverage, out vec4 outputColor) {
    outputColor = inputColor * inputCoverage;
}
