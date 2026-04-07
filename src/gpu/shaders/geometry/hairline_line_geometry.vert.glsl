// Copyright (C) 2026 Tencent. All rights reserved.
// hairline_line_geometry.vert.glsl - HairlineLineGeometryProcessor vertex shader.
// Transforms position by matrix and passes edge distance as a varying.
//
// Attributes: position (vec2), edgeDistance (float)
// Uniforms: Matrix (mat3)
//
// Optional macros:
//   TGFX_GP_HLINE_COVERAGE_AA - defined if AA coverage is used (affects FS only)

void TGFX_HairlineLineGP_VS(vec2 inPosition, float inEdgeDistance, mat3 matrix,
                              out float vEdgeDistance, out vec2 position) {
    position = (matrix * vec3(inPosition, 1.0)).xy;
    vEdgeDistance = inEdgeDistance;
}
