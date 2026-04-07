// Copyright (C) 2026 Tencent. All rights reserved.
// ellipse_geometry.vert.glsl - EllipseGeometryProcessor vertex shader.
// Passes ellipse offset and radii as varyings, plus optional per-vertex color.
//
// Attributes: inPosition (vec2), inEllipseOffset (vec2), inEllipseRadii (vec4),
//             inColor (vec4, optional)
//
// Optional macros:
//   TGFX_GP_ELLIPSE_COMMON_COLOR - defined if using uniform color instead of per-vertex color

void TGFX_EllipseGP_VS(vec2 inPosition, vec2 inEllipseOffset, vec4 inEllipseRadii,
#ifndef TGFX_GP_ELLIPSE_COMMON_COLOR
                        vec4 inColor, out vec4 vColor,
#endif
                        out vec2 vEllipseOffsets, out vec4 vEllipseRadii,
                        out vec2 position) {
    vEllipseOffsets = inEllipseOffset;
    vEllipseRadii = inEllipseRadii;
#ifndef TGFX_GP_ELLIPSE_COMMON_COLOR
    vColor = inColor;
#endif
    position = inPosition;
}
