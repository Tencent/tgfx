// PointwiseChainShader vertex shader
// Feeds one TransformedCoords varying per texture leaf of the pointwise DAG. The leaf count is the
// TEXTURE_COUNT permutation (0 -> 1 leaf, 1 -> 2 leaves, 2 -> 4 leaves) because the varying layout
// is part of the vertex/fragment interface.
// Permutation dimensions (vert): GP_TYPE (0=DefaultGP, 1=QuadPerEdgeAAGP)
#version 450

#ifndef GP_TYPE
#define GP_TYPE 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef TEXTURE_COUNT
#define TEXTURE_COUNT 0
#endif

#if TEXTURE_COUNT == 0
#define NTEX 1
#elif TEXTURE_COUNT == 1
#define NTEX 2
#else
#define NTEX 4
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
#if GP_TYPE == 0
  mat3 Matrix;
#endif
  mat3 CoordTransformMatrix_0;
#if NTEX >= 2
  mat3 CoordTransformMatrix_1;
#endif
#if NTEX >= 4
  mat3 CoordTransformMatrix_2;
  mat3 CoordTransformMatrix_3;
#endif
};

layout(location = 0) in vec2 aPosition;
#if HAS_COVERAGE
layout(location = 1) in float inCoverage;
#endif

layout(location = 0) out vec2 TransformedCoords_0;
#if NTEX >= 2
layout(location = 1) out vec2 TransformedCoords_1;
#endif
#if NTEX >= 4
layout(location = 2) out vec2 TransformedCoords_2;
layout(location = 3) out vec2 TransformedCoords_3;
#endif
#if HAS_COVERAGE
layout(location = NTEX) out float vCoverage;
#endif

void main() {
#if GP_TYPE == 0
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
#else
  // QuadPerEdgeAAGeometryProcessor pre-transforms vertices to device space.
  highp vec2 position = aPosition;
#endif
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(aPosition, 1.0)).xy;
#if NTEX >= 2
  TransformedCoords_1 = (CoordTransformMatrix_1 * vec3(aPosition, 1.0)).xy;
#endif
#if NTEX >= 4
  TransformedCoords_2 = (CoordTransformMatrix_2 * vec3(aPosition, 1.0)).xy;
  TransformedCoords_3 = (CoordTransformMatrix_3 * vec3(aPosition, 1.0)).xy;
#endif
#if HAS_COVERAGE
  vCoverage = inCoverage;
#endif
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
