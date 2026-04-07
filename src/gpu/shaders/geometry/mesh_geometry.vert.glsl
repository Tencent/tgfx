// Copyright (C) 2026 Tencent. All rights reserved.
// mesh_geometry.vert.glsl - MeshGeometryProcessor vertex shader.
// Transforms position by matrix, passes optional texCoord, color, and coverage varyings.
//
// Attributes: position (vec2), texCoord (vec2, optional), color (vec4, optional),
//             coverage (float, optional)
// Uniforms: Matrix (mat3)
//
// Optional macros:
//   TGFX_GP_MESH_TEX_COORDS - defined if mesh has texture coordinates
//   TGFX_GP_MESH_VERTEX_COLORS - defined if mesh has per-vertex colors
//   TGFX_GP_MESH_VERTEX_COVERAGE - defined if mesh has per-vertex coverage

void TGFX_MeshGP_VS(vec2 inPosition, mat3 matrix,
#ifdef TGFX_GP_MESH_TEX_COORDS
                     vec2 inTexCoord, out vec2 vTexCoord,
#endif
#ifdef TGFX_GP_MESH_VERTEX_COLORS
                     vec4 inColor, out vec4 vColor,
#endif
#ifdef TGFX_GP_MESH_VERTEX_COVERAGE
                     float inCoverage, out float vCoverage,
#endif
                     out vec2 position) {
    position = (matrix * vec3(inPosition, 1.0)).xy;
#ifdef TGFX_GP_MESH_TEX_COORDS
    vTexCoord = inTexCoord;
#endif
#ifdef TGFX_GP_MESH_VERTEX_COLORS
    vColor = inColor;
#endif
#ifdef TGFX_GP_MESH_VERTEX_COVERAGE
    vCoverage = inCoverage;
#endif
}
