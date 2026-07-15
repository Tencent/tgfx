// BlendMergeShader vertex shader
// Permutation dimensions (vert):
//   GP_TYPE (0=DefaultGP, 1=QuadPerEdgeAAGP)
//   HAS_COVERAGE (0/1): whether QuadGP provides per-vertex AA coverage
//   HAS_UV_COORD (0/1): whether QuadGP has uvCoord attribute as coord transform source
//   HAS_COLOR (0/1): whether QuadGP provides per-vertex color attribute
// Passes texture coordinates to fragment shader for child sampling.
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

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
#if GP_TYPE == 0
  mat3 Matrix;
#endif
  mat3 CoordTransformMatrix_0;
};

layout(location = 0) in vec2 aPosition;

// Compute compact attribute locations for QuadGP optional attributes.
// Order matches QuadPerEdgeAAGeometryProcessor attribute emission: coverage, uvCoord, color.
#if GP_TYPE == 1

#if HAS_COVERAGE
  #define LOC_COVERAGE 1
  #define LOC_AFTER_COVERAGE 2
#else
  #define LOC_AFTER_COVERAGE 1
#endif

#if HAS_UV_COORD
  #define LOC_UV_COORD LOC_AFTER_COVERAGE
  #if LOC_AFTER_COVERAGE == 1
    #define LOC_AFTER_UV_COORD 2
  #else
    #define LOC_AFTER_UV_COORD 3
  #endif
#else
  #define LOC_AFTER_UV_COORD LOC_AFTER_COVERAGE
#endif

#if HAS_COLOR
  #define LOC_COLOR LOC_AFTER_UV_COORD
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

#endif // GP_TYPE == 1

layout(location = 0) out vec2 TransformedCoords_0;

#if HAS_COVERAGE
layout(location = 1) out float vCoverage;
#endif

#if HAS_COLOR
layout(location = 2) out vec4 vColor;
#endif

void main() {
#if GP_TYPE == 0
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
#else
  highp vec2 position = aPosition;
#endif
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);

#if HAS_UV_COORD
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(uvCoord, 1.0)).xy;
#else
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(aPosition, 1.0)).xy;
#endif

#if HAS_COVERAGE
  vCoverage = inCoverage;
#endif

#if HAS_COLOR
  vColor = inColor;
#endif
}
