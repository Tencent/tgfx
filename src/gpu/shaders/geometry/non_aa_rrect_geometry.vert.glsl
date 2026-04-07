// Copyright (C) 2026 Tencent. All rights reserved.
// non_aa_rrect_geometry.vert.glsl - NonAARRectGeometryProcessor vertex shader.
// Passes local coordinates, radii, rect bounds, and optional stroke width as varyings.
//
// Attributes: inPosition (vec2), inColor (vec4, optional), inLocalCoord (vec2),
//             inRadii (vec2), inRectBounds (vec4), inStrokeWidth (vec2, optional)
//
// Optional macros:
//   TGFX_GP_NONAA_COMMON_COLOR - defined if using uniform color instead of per-vertex color
//   TGFX_GP_NONAA_STROKE - defined if rendering a stroked rounded rect

void TGFX_NonAARRectGP_VS(vec2 inPosition, vec2 inLocalCoord, vec2 inRadii, vec4 inRectBounds,
#ifndef TGFX_GP_NONAA_COMMON_COLOR
                           vec4 inColor, out vec4 vColor,
#endif
#ifdef TGFX_GP_NONAA_STROKE
                           vec2 inStrokeWidth, out vec2 vStrokeWidth,
#endif
                           out vec2 vLocalCoord, out vec2 vRadii, out vec4 vRectBounds,
                           out vec2 position) {
    position = inPosition;
    vLocalCoord = inLocalCoord;
    vRadii = inRadii;
    vRectBounds = inRectBounds;
#ifndef TGFX_GP_NONAA_COMMON_COLOR
    vColor = inColor;
#endif
#ifdef TGFX_GP_NONAA_STROKE
    vStrokeWidth = inStrokeWidth;
#endif
}
