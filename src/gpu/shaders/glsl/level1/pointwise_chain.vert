// PointwiseChainShader vertex shader
// Feeds one TransformedCoords varying per texture leaf of the pointwise DAG. The leaf count is the
// TEXTURE_COUNT permutation (0 -> 0 leaves, 1 -> 1, 2 -> 2, 3 -> 4) because the varying layout is
// part of the vertex/fragment interface.
// Permutation dimensions (vert): HAS_COVERAGE, HAS_UV_COORD, HAS_COLOR, TEXTURE_COUNT.
// There is no GP_TYPE: the position always goes through the Matrix uniform, which DefaultGP fills
// with the view matrix and QuadPerEdgeAAGP fills with identity (bit-exact), so one variant serves
// both. HAS_UV_COORD/HAS_COLOR only ever appear on quad draws (their buffers always carry the
// coverage slot), which ShouldCompile enforces without a GP dimension.
#version 450

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
  mat3 Matrix;
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
#if HAS_COVERAGE
layout(location = 1) in float inCoverage;
#endif
#if HAS_UV_COORD
layout(location = 2) in vec2 uvCoord;
#endif
#if HAS_COLOR
#if HAS_UV_COORD
layout(location = 3) in vec4 inColor;
#else
layout(location = 2) in vec4 inColor;
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
#if HAS_COVERAGE
layout(location = NTEX) out float vCoverage;
#endif
#if HAS_COLOR
// HAS_COLOR implies HAS_COVERAGE (ShouldCompile), so the color varying always follows it.
layout(location = NTEX + 1) out vec4 vColor;
#endif

void main() {
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
#if HAS_UV_COORD
  vec2 coordSource = uvCoord;
#else
  vec2 coordSource = aPosition;
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
#if HAS_COVERAGE
  vCoverage = inCoverage;
#endif
#if HAS_COLOR
  vColor = inColor;
#endif
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
