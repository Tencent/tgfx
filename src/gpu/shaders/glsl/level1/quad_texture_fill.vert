// QuadTextureFillShader vertex shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + TextureEffect + EmptyXferProcessor
// Permutation dimensions (vert): HAS_COVERAGE, HAS_UV_COORD, HAS_COLOR, HAS_SUBSET,
//                                HAS_UV_PERSPECTIVE
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
#ifndef HAS_SUBSET
#define HAS_SUBSET 0
#endif
#ifndef HAS_UV_PERSPECTIVE
#define HAS_UV_PERSPECTIVE 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 CoordTransformMatrix_0;
#if HAS_SUBSET && !HAS_UV_COORD
  mat3 texSubsetMatrix;
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
layout(location = 3) in vec4 inColor;
#endif

#if HAS_SUBSET
layout(location = 4) in vec4 texSubset;
#endif

#if HAS_UV_PERSPECTIVE
layout(location = 0) out vec3 TransformedCoords_0;
#else
layout(location = 0) out vec2 TransformedCoords_0;
#endif

#if HAS_COVERAGE
layout(location = 1) out float vCoverage;
#endif

#if HAS_COLOR
layout(location = 2) out vec4 vColor;
#endif

#if HAS_SUBSET
layout(location = 3) out vec4 vTexSubset;
#endif

void main() {
  // Position is already in device space (normalized window coordinates).
#if HAS_COVERAGE
  vCoverage = inCoverage;
#endif

#if HAS_COLOR
  vColor = inColor;
#endif

  // Compute texture coordinates.
#if HAS_UV_COORD
  vec3 coordResult = CoordTransformMatrix_0 * vec3(uvCoord, 1.0);
#else
  vec3 coordResult = CoordTransformMatrix_0 * vec3(aPosition, 1.0);
#endif

#if HAS_UV_PERSPECTIVE
  TransformedCoords_0 = coordResult;
#else
  TransformedCoords_0 = coordResult.xy;
#endif

  // Transform subset bounds to texture space.
#if HAS_SUBSET
#if HAS_UV_COORD
  highp vec2 srcLT = texSubset.xy;
  highp vec2 srcRB = texSubset.zw;
  highp vec2 perspLT = (CoordTransformMatrix_0 * vec3(srcLT, 1.0)).xy;
  highp vec2 perspRB = (CoordTransformMatrix_0 * vec3(srcRB, 1.0)).xy;
#else
  highp vec2 srcLT = texSubset.xy;
  highp vec2 srcRB = texSubset.zw;
  highp vec2 perspLT = (texSubsetMatrix * vec3(srcLT, 1.0)).xy;
  highp vec2 perspRB = (texSubsetMatrix * vec3(srcRB, 1.0)).xy;
#endif
  highp vec4 subset = vec4(min(perspLT, perspRB), max(perspLT, perspRB));
  vTexSubset = subset;
#endif

  gl_Position = vec4(aPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
