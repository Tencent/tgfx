// Copyright (C) 2026 Tencent. All rights reserved.
// quad_aa_geometry.frag.glsl - QuadPerEdgeAAGeometryProcessor fragment shader.
// Outputs color (uniform or varying) and optional AA coverage.
//
// Optional macros:
//   TGFX_GP_QUAD_COVERAGE_AA - defined if AA coverage varying is present
//   TGFX_GP_QUAD_COMMON_COLOR - defined if using uniform color instead of per-vertex color

void TGFX_QuadAAGP_FS(
#ifdef TGFX_GP_QUAD_COMMON_COLOR
    vec4 color,
#else
    vec4 vColor,
#endif
#ifdef TGFX_GP_QUAD_COVERAGE_AA
    float vCoverage,
#endif
    out vec4 outputColor, out vec4 outputCoverage) {
#ifdef TGFX_GP_QUAD_COMMON_COLOR
    outputColor = color;
#else
    outputColor = vColor;
#endif
#ifdef TGFX_GP_QUAD_COVERAGE_AA
    outputCoverage = vec4(vCoverage);
#else
    outputCoverage = vec4(1.0);
#endif
}
