// TiledTextureFillShader vertex shader
// The coordinate is always affine (vec2); perspective TiledTextureEffect draws fall back to
// ProgramBuilder, so no perspective path exists here.
// The position always goes through the Matrix uniform: DefaultGP writes the view matrix and
// QuadPerEdgeAAGP (pre-transformed to device space) writes identity, which is bit-exact, so one
// variant serves both geometry processors.
// The coverage attribute is unconditional: the non-AA path triangles carry a constant 1.0 slot
// (they share the AA vertex layout, and the quad providers emit 1.0 for non-AA draws), so the
// kernel binds it in a single variant and the non-AA modulation by one is the identity.
#version 450

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix;
  mat3 CoordTransformMatrix_0;
};

layout(location = 0) in vec2 aPosition;
layout(location = 1) in float inCoverage;

layout(location = 0) out vec2 TransformedCoords_0;
layout(location = 1) out float vCoverage;

void main() {
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  vec3 coordResult = CoordTransformMatrix_0 * vec3(aPosition, 1.0);
  TransformedCoords_0 = coordResult.xy;
  vCoverage = inCoverage;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
