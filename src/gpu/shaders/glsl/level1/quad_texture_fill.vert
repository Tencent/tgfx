// QuadTextureFillShader vertex shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + TextureEffect + EmptyXferProcessor
// Permutation dimensions (vert): HAS_COVERAGE, HAS_UV_COORD, HAS_COLOR, HAS_SUBSET
// The transformed coordinate is always emitted as a vec3 and perspective-divided in the fragment
// shader. For affine transforms the third row of CoordTransformMatrix_0 is [0,0,1], so the divisor
// is 1.0 and the division is a no-op; this lets one shader serve both affine and perspective.
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

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 CoordTransformMatrix_0;
#if HAS_SUBSET && !HAS_UV_COORD
  mat3 texSubsetMatrix;
#endif
};

layout(location = 0) in vec2 aPosition;

// Compute compact attribute locations: each optional attribute gets the next
// available index after position (0). Order: coverage, uvCoord, color, subset.
#if HAS_COVERAGE
  #define LOC_COVERAGE 1
  #define LOC_AFTER_COVERAGE 2
#else
  #define LOC_AFTER_COVERAGE 1
#endif

#if HAS_UV_COORD
  #if LOC_AFTER_COVERAGE == 1
    #define LOC_UV_COORD 1
    #define LOC_AFTER_UV_COORD 2
  #else
    #define LOC_UV_COORD 2
    #define LOC_AFTER_UV_COORD 3
  #endif
#else
  #define LOC_AFTER_UV_COORD LOC_AFTER_COVERAGE
#endif

#if HAS_COLOR
  #if LOC_AFTER_UV_COORD == 1
    #define LOC_COLOR 1
    #define LOC_AFTER_COLOR 2
  #elif LOC_AFTER_UV_COORD == 2
    #define LOC_COLOR 2
    #define LOC_AFTER_COLOR 3
  #else
    #define LOC_COLOR 3
    #define LOC_AFTER_COLOR 4
  #endif
#else
  #define LOC_AFTER_COLOR LOC_AFTER_UV_COORD
#endif

#if HAS_SUBSET
  #if LOC_AFTER_COLOR == 1
    #define LOC_SUBSET 1
  #elif LOC_AFTER_COLOR == 2
    #define LOC_SUBSET 2
  #elif LOC_AFTER_COLOR == 3
    #define LOC_SUBSET 3
  #else
    #define LOC_SUBSET 4
  #endif
#endif

#if HAS_COVERAGE
layout(location = LOC_COVERAGE) in float inCoverage;
#endif

#if HAS_UV_COORD
layout(location = LOC_UV_COORD) in vec2 uvCoord;
#endif

#if HAS_COLOR
layout(location = LOC_COLOR) in vec4 inColor;
#endif

#if HAS_SUBSET
layout(location = LOC_SUBSET) in vec4 texSubset;
#endif

layout(location = 0) out vec3 TransformedCoords_0;

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

  TransformedCoords_0 = coordResult;

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
