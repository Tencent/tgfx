// Copyright (C) 2026 Tencent. All rights reserved.
// hairline_line_geometry.vert.glsl - HairlineLineGeometryProcessor vertex shader.
// Transforms position by matrix, writes gl_Position, and passes edge distance plus the
// device-space (post-matrix) position as varyings. The device-space position varying is then
// used by ModularProgramBuilder as the input to FP coord transforms, which expect a VS-scope
// expression available after the VS call.
//
// Attributes: position (vec2), edgeDistance (float)
// Uniforms: Matrix (mat3)
//
// Depends on: tgfx_vs_boilerplate.glsl (TGFX_NormalizePosition).

void TGFX_HairlineLineGP_VS(vec2 inPosition, float inEdgeDistance, mat3 matrix,
                              out float vEdgeDistance, out vec2 vTransformedPosition) {
    vTransformedPosition = (matrix * vec3(inPosition, 1.0)).xy;
    vEdgeDistance = inEdgeDistance;
    gl_Position = TGFX_NormalizePosition(vTransformedPosition);
}
