// Copyright (C) 2026 Tencent. All rights reserved.
// mesh_geometry.frag.glsl - MeshGeometryProcessor fragment shader.
// Outputs color from uniform or varying, and optional per-vertex coverage.
//
// Optional macros:
//   TGFX_GP_MESH_VERTEX_COLORS - defined if mesh has per-vertex colors
//   TGFX_GP_MESH_VERTEX_COVERAGE - defined if mesh has per-vertex coverage

void TGFX_MeshGP_FS(
#ifdef TGFX_GP_MESH_VERTEX_COLORS
    vec4 vColor,
#else
    vec4 uniformColor,
#endif
#ifdef TGFX_GP_MESH_VERTEX_COVERAGE
    float vCoverage,
#endif
    out vec4 outputColor, out vec4 outputCoverage) {
#ifdef TGFX_GP_MESH_VERTEX_COLORS
    outputColor = vColor;
#else
    outputColor = uniformColor;
#endif
#ifdef TGFX_GP_MESH_VERTEX_COVERAGE
    outputCoverage = vec4(vCoverage);
#else
    outputCoverage = vec4(1.0);
#endif
}
