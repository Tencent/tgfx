// Copyright (C) 2026 Tencent. All rights reserved.
// shape_instanced_geometry.frag.glsl - ShapeInstancedGeometryProcessor fragment shader.
// Outputs per-instance color (or white) and optional AA coverage.

void TGFX_ShapeInstancedGP_FS(
#ifdef TGFX_GP_SHAPE_HAS_COLORS
    vec4 vInstanceColor,
#endif
#ifdef TGFX_GP_SHAPE_COVERAGE_AA
    float vCoverage,
#endif
    out vec4 outputColor, out vec4 outputCoverage) {
#ifdef TGFX_GP_SHAPE_HAS_COLORS
    outputColor = vInstanceColor;
#else
    outputColor = vec4(1.0);
#endif
#ifdef TGFX_GP_SHAPE_COVERAGE_AA
    outputCoverage = vec4(vCoverage);
#else
    outputCoverage = vec4(1.0);
#endif
}
