// Copyright (C) 2026 Tencent. All rights reserved.
// round_stroke_rect_geometry.frag.glsl - RoundStrokeRectGeometryProcessor fragment shader.
// Computes ellipse SDF coverage for rounded stroke rectangles.
// When TGFX_GP_RRECT_COVERAGE_AA is defined, applies smooth SDF-based anti-aliasing.
// Otherwise, uses a simple step function for coverage.
//
// Varyings: vEllipseOffsets (vec2), vCoverage (float, optional), vEllipseRadii (vec2, optional)
//
// Optional macros:
//   TGFX_GP_RRECT_COVERAGE_AA - defined if AA coverage is used (SDF-based edge smoothing)
//   TGFX_GP_RRECT_COMMON_COLOR - defined if using uniform color instead of per-vertex color

void TGFX_RoundStrokeRectGP_FS(vec2 vEllipseOffsets,
#ifdef TGFX_GP_RRECT_COVERAGE_AA
                                 float vCoverage, vec2 vEllipseRadii,
#endif
#ifdef TGFX_GP_RRECT_COMMON_COLOR
                                 vec4 uniformColor,
#else
                                 vec4 vColor,
#endif
                                 out vec4 outputColor, out vec4 outputCoverage) {
#ifdef TGFX_GP_RRECT_COMMON_COLOR
    outputColor = uniformColor;
#else
    outputColor = vColor;
#endif

#ifdef TGFX_GP_RRECT_COVERAGE_AA
    outputCoverage = vec4(vCoverage);
#else
    outputCoverage = vec4(1.0);
#endif

    vec2 offset = vEllipseOffsets;
#ifdef TGFX_GP_RRECT_COVERAGE_AA
    float test = dot(offset, offset) - 1.0;
    if (test > -0.5) {
        vec2 grad = 2.0 * offset * vEllipseRadii;
        float grad_dot = dot(grad, grad);
        grad_dot = max(grad_dot, 1.1755e-38);
        float invlen = inversesqrt(grad_dot);
        float edgeAlpha = clamp(0.5 - test * invlen, 0.0, 1.0);
        outputCoverage *= edgeAlpha;
    }
#else
    float test = dot(offset, offset);
    float edgeAlpha = step(test, 1.0);
    outputCoverage *= edgeAlpha;
#endif
}
