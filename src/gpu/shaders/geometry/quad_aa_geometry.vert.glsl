// Copyright (C) 2026 Tencent. All rights reserved.
// quad_aa_geometry.vert.glsl - QuadPerEdgeAAGeometryProcessor vertex shader.
// Passes position directly (pre-transformed), with optional coverage and color varyings.
//
// Attributes: position (vec2), uvCoord (vec2, optional), coverage (float, optional),
//             color (vec4, optional)
//
// Optional macros:
//   TGFX_GP_QUAD_COVERAGE_AA - defined if AA coverage attribute is present
//   TGFX_GP_QUAD_COMMON_COLOR - defined if using uniform color instead of per-vertex color

void TGFX_QuadAAGP_VS(vec2 inPosition,
#ifdef TGFX_GP_QUAD_COVERAGE_AA
                       float inCoverage, out float vCoverage,
#endif
#ifndef TGFX_GP_QUAD_COMMON_COLOR
                       vec4 inColor, out vec4 vColor,
#endif
                       out vec2 position) {
    position = inPosition;
#ifdef TGFX_GP_QUAD_COVERAGE_AA
    vCoverage = inCoverage;
#endif
#ifndef TGFX_GP_QUAD_COMMON_COLOR
    vColor = inColor;
#endif
}
