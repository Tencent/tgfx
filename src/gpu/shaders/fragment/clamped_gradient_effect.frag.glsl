// Copyright (C) 2026 Tencent. All rights reserved.
// clamped_gradient_effect.frag.glsl - ClampedGradientEffect modular shader function.
// Calls a gradient layout child to get t-value, then clamps and dispatches to colorizer child.
// The layout and colorizer are called via their own TGFX_ functions (passed as results).
//
// This is a container FP: it orchestrates calls to child FPs (gradLayout + colorizer)
// rather than performing the computation itself.
//
// Parameters:
//   inputColor      - incoming color to modulate result alpha by
//   gradLayoutResult - result from the gradient layout child FP (vec4 with t in .x, validity in .y)
//   colorizerResult  - result from the colorizer child FP evaluated at t
//   leftBorderColor  - color for t <= 0
//   rightBorderColor - color for t >= 1

vec4 TGFX_ClampedGradientEffect(vec4 inputColor, vec4 gradLayoutResult,
                                  vec4 colorizerResult,
                                  vec4 leftBorderColor, vec4 rightBorderColor) {
    vec4 t = gradLayoutResult;
    vec4 result;
    if (t.y < 0.0) {
        result = vec4(0.0);
    } else if (t.x <= 0.0) {
        result = leftBorderColor;
    } else if (t.x >= 1.0) {
        result = rightBorderColor;
    } else {
        result = colorizerResult;
    }
    // Premultiply and modulate by input alpha.
    result.rgb *= result.a;
    result *= inputColor.a;
    return result;
}
