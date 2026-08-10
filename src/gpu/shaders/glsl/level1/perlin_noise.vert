// PerlinNoiseFillShader vertex shader
// Emits the noise lattice coordinate from the geometry processor position via CoordTransformMatrix_0
// (the PerlinNoiseFragmentProcessor's uvMatrix), identical in structure to textured_effect.vert.
// Permutation dimensions (vert): HAS_COVERAGE.
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
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(aPosition, 1.0)).xy;
#if HAS_COVERAGE
  vCoverage = inCoverage;
#endif
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
