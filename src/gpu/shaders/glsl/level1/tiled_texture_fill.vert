// TiledTextureFillShader vertex shader
// Permutation dimensions (vert): HAS_COVERAGE
// The coordinate is always affine (vec2); perspective TiledTextureEffect draws fall back to
// ProgramBuilder, so no perspective path exists here.
// HAS_COVERAGE is set when the geometry processor emits a per-vertex AA coverage attribute; it is
// forwarded as a varying and modulated into the fragment output.
// The position always goes through the Matrix uniform: DefaultGP writes the view matrix and
// QuadPerEdgeAAGP (pre-transformed to device space) writes identity, which is bit-exact, so one
// variant serves both geometry processors.
#version 450

#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix;
  mat3 CoordTransformMatrix_0;
};

layout(location = 0) in vec2 aPosition;
#if HAS_COVERAGE
layout(location = 1) in float inCoverage;
#endif

layout(location = 0) out vec2 TransformedCoords_0;
#if HAS_COVERAGE
layout(location = 1) out float vCoverage;
#endif

void main() {
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  vec3 coordResult = CoordTransformMatrix_0 * vec3(aPosition, 1.0);
  TransformedCoords_0 = coordResult.xy;
#if HAS_COVERAGE
  vCoverage = inCoverage;
#endif
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
