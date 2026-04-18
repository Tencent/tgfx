// Copyright (C) 2026 Tencent. All rights reserved.
// hairline_line_geometry.vert.glsl - HairlineLineGeometryProcessor vertex shader.
// Transforms position by matrix, writes gl_Position, and passes edge distance as a varying.
//
// Attributes: position (vec2), edgeDistance (float)
// Uniforms: Matrix (mat3)
//
// Depends on: tgfx_vs_boilerplate.glsl (TGFX_NormalizePosition).

void TGFX_HairlineLineGP_VS(vec2 inPosition, float inEdgeDistance, mat3 matrix,
                              out float vEdgeDistance, out vec2 position) {
    position = (matrix * vec3(inPosition, 1.0)).xy;
    vEdgeDistance = inEdgeDistance;
    gl_Position = TGFX_NormalizePosition(position);
}
