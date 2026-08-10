// PointwiseChainShader vertex shader
// Feeds one TransformedCoords varying per texture leaf of the pointwise DAG. The leaf count is the
// TEXTURE_COUNT permutation (0 -> 0 leaves, 1 -> 1, 2 -> 2, 3 -> 4) because the varying layout is
// part of the vertex/fragment interface.
// Permutation dimensions (vert): GP_TYPE (0=DefaultGP, 1=QuadPerEdgeAAGP), HAS_COVERAGE,
// HAS_UV_COORD (QuadGP only), HAS_COLOR (QuadGP only), TEXTURE_COUNT.
// QuadGP vertex buffers always carry the coverage slot (providers emit 1.0 for non-AA draws), so
// inCoverage is always declared for QuadGP; the color slot only exists when the provider carries
// per-vertex colors (HAS_COLOR).
#version 450

#ifndef GP_TYPE
#define GP_TYPE 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_UV_COORD
#define HAS_UV_COORD 0
#endif
#ifndef HAS_COLOR
#define HAS_COLOR 0
#endif
#ifndef TEXTURE_COUNT
#define TEXTURE_COUNT 0
#endif

#if TEXTURE_COUNT == 0
#define NTEX 0
#elif TEXTURE_COUNT == 1
#define NTEX 1
#elif TEXTURE_COUNT == 2
#define NTEX 2
#else
#define NTEX 4
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
#if GP_TYPE == 0
  mat3 Matrix;
#endif
#if NTEX >= 1
  mat3 CoordTransformMatrix_0;
#endif
#if NTEX >= 2
  mat3 CoordTransformMatrix_1;
#endif
#if NTEX >= 4
  mat3 CoordTransformMatrix_2;
  mat3 CoordTransformMatrix_3;
#endif
};

layout(location = 0) in vec2 aPosition;
#if GP_TYPE == 0
#if HAS_COVERAGE
layout(location = 1) in float inCoverage;
#endif
#else
// QuadGP attribute order is fixed: coverage (always present), uvCoord (optional), color (optional).
layout(location = 1) in float inCoverage;
#if HAS_UV_COORD
layout(location = 2) in vec2 uvCoord;
#if HAS_COLOR
layout(location = 3) in vec4 inColor;
#endif
#else
#if HAS_COLOR
layout(location = 2) in vec4 inColor;
#endif
#endif
#endif

#if NTEX >= 1
layout(location = 0) out vec2 TransformedCoords_0;
#endif
#if NTEX >= 2
layout(location = 1) out vec2 TransformedCoords_1;
#endif
#if NTEX >= 4
layout(location = 2) out vec2 TransformedCoords_2;
layout(location = 3) out vec2 TransformedCoords_3;
#endif
#if GP_TYPE == 0
#if HAS_COVERAGE
layout(location = NTEX) out float vCoverage;
#endif
#else
layout(location = NTEX) out float vCoverage;
#if HAS_COLOR
layout(location = NTEX + 1) out vec4 vColor;
#endif
#endif

void main() {
#if GP_TYPE == 0
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  vec2 coordSource = aPosition;
#else
  // QuadPerEdgeAAGeometryProcessor pre-transforms vertices to device space.
  highp vec2 position = aPosition;
#if HAS_UV_COORD
  vec2 coordSource = uvCoord;
#else
  vec2 coordSource = aPosition;
#endif
#endif
#if NTEX >= 1
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(coordSource, 1.0)).xy;
#endif
#if NTEX >= 2
  TransformedCoords_1 = (CoordTransformMatrix_1 * vec3(coordSource, 1.0)).xy;
#endif
#if NTEX >= 4
  TransformedCoords_2 = (CoordTransformMatrix_2 * vec3(coordSource, 1.0)).xy;
  TransformedCoords_3 = (CoordTransformMatrix_3 * vec3(coordSource, 1.0)).xy;
#endif
#if GP_TYPE == 0
#if HAS_COVERAGE
  vCoverage = inCoverage;
#endif
#else
  vCoverage = inCoverage;
#if HAS_COLOR
  vColor = inColor;
#endif
#endif
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
