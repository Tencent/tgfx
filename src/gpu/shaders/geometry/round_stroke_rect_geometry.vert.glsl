// Copyright (C) 2026 Tencent. All rights reserved.
// round_stroke_rect_geometry.vert.glsl - RoundStrokeRectGeometryProcessor vertex shader.
// Passes ellipse offsets, optional coverage + ellipse radii, and optional per-vertex color
// as varyings.
//
// Attributes: inPosition (vec2), inEllipseOffset (vec2), inCoverage (float, optional),
//             inEllipseRadii (vec2, optional), inColor (vec4, optional)
//
// Optional macros:
//   TGFX_GP_RRECT_COVERAGE_AA - defined if AA coverage is used (provides coverage + ellipseRadii)
//   TGFX_GP_RRECT_COMMON_COLOR - defined if using uniform color instead of per-vertex color

void TGFX_RoundStrokeRectGP_VS(vec2 inPosition, vec2 inEllipseOffset,
#ifdef TGFX_GP_RRECT_COVERAGE_AA
                                 float inCoverage, vec2 inEllipseRadii,
                                 out float vCoverage, out vec2 vEllipseRadii,
#endif
#ifndef TGFX_GP_RRECT_COMMON_COLOR
                                 vec4 inColor, out vec4 vColor,
#endif
                                 out vec2 vEllipseOffsets, out vec2 position) {
    vEllipseOffsets = inEllipseOffset;
#ifdef TGFX_GP_RRECT_COVERAGE_AA
    vCoverage = inCoverage;
    vEllipseRadii = inEllipseRadii;
#endif
#ifndef TGFX_GP_RRECT_COMMON_COLOR
    vColor = inColor;
#endif
    position = inPosition;
}
