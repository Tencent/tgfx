// Copyright (C) 2026 Tencent. All rights reserved.
// ellipse_geometry.frag.glsl - EllipseGeometryProcessor fragment shader.
// Computes SDF-based ellipse coverage with optional stroke support.
//
// Varyings: vEllipseOffsets (vec2), vEllipseRadii (vec4)
//
// Optional macros:
//   TGFX_GP_ELLIPSE_STROKE - defined if rendering a stroked ellipse (inner + outer SDF)
//   TGFX_GP_ELLIPSE_COMMON_COLOR - defined if using uniform color instead of per-vertex color

void TGFX_EllipseGP_FS(vec2 vEllipseOffsets, vec4 vEllipseRadii,
#ifdef TGFX_GP_ELLIPSE_COMMON_COLOR
                        vec4 color,
#else
                        vec4 vColor,
#endif
                        out vec4 outputColor, out vec4 outputCoverage) {
#ifdef TGFX_GP_ELLIPSE_COMMON_COLOR
    outputColor = color;
#else
    outputColor = vColor;
#endif
    vec2 offset = vEllipseOffsets.xy;
#ifdef TGFX_GP_ELLIPSE_STROKE
    offset *= vEllipseRadii.xy;
#endif
    float test = dot(offset, offset) - 1.0;
    vec2 grad = 2.0 * offset * vEllipseRadii.xy;
    float grad_dot = dot(grad, grad);
    grad_dot = max(grad_dot, 1.1755e-38);
    float invlen = inversesqrt(grad_dot);
    float edgeAlpha = clamp(0.5 - test * invlen, 0.0, 1.0);
#ifdef TGFX_GP_ELLIPSE_STROKE
    offset = vEllipseOffsets.xy * vEllipseRadii.zw;
    test = dot(offset, offset) - 1.0;
    grad = 2.0 * offset * vEllipseRadii.zw;
    grad_dot = dot(grad, grad);
    invlen = inversesqrt(grad_dot);
    edgeAlpha *= clamp(0.5 + test * invlen, 0.0, 1.0);
#endif
    outputCoverage = vec4(edgeAlpha);
}
