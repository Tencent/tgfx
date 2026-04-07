// Copyright (C) 2026 Tencent. All rights reserved.
// hairline_quad_geometry.vert.glsl - HairlineQuadGeometryProcessor vertex shader.
// Transforms position by matrix and passes hairQuadEdge as a varying for Loop-Blinn evaluation.
//
// Attributes: position (vec2), hairQuadEdge (vec4)
// Uniforms: Matrix (mat3)
//
// Optional macros:
//   TGFX_GP_HQUAD_COVERAGE_AA - defined if AA coverage is used (affects FS only)

void TGFX_HairlineQuadGP_VS(vec2 inPosition, vec4 inHairQuadEdge, mat3 matrix,
                              out vec4 vHairQuadEdge, out vec2 position) {
    position = (matrix * vec3(inPosition, 1.0)).xy;
    vHairQuadEdge = inHairQuadEdge;
}
