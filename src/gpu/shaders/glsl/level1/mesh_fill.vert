// MeshFillShader vertex shader
// Processor layout: MeshGeometryProcessor(_P0) + EmptyXferProcessor(_P1)
// Permutation dimensions (injected as #define 0/1):
//   HAS_TEX_COORDS: whether user-provided texture coordinates are present
//   HAS_COLORS: whether per-vertex colors are present
//   HAS_COVERAGE: whether per-vertex coverage is present
#version 450

#ifndef HAS_TEX_COORDS
#define HAS_TEX_COORDS 0
#endif
#ifndef HAS_COLORS
#define HAS_COLORS 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix_P0;
  mat3 CoordTransformMatrix_0_P0;
};

layout(location = 0) in vec2 aPosition;
#if HAS_TEX_COORDS
layout(location = 1) in vec2 aTexCoord;
#if HAS_COLORS
layout(location = 2) in vec4 aColor;
#if HAS_COVERAGE
layout(location = 3) in float aCoverage;
#endif
#else
#if HAS_COVERAGE
layout(location = 2) in float aCoverage;
#endif
#endif
#else
#if HAS_COLORS
layout(location = 1) in vec4 aColor;
#if HAS_COVERAGE
layout(location = 2) in float aCoverage;
#endif
#else
#if HAS_COVERAGE
layout(location = 1) in float aCoverage;
#endif
#endif
#endif

layout(location = 0) out vec2 TransformedCoords_0;
#if HAS_COLORS
layout(location = 1) out vec4 vColor;
#endif
#if HAS_COVERAGE
layout(location = 2) out float vCoverage;
#endif

void main() {
#if HAS_TEX_COORDS
  TransformedCoords_0 = (CoordTransformMatrix_0_P0 * vec3(aTexCoord, 1.0)).xy;
#else
  TransformedCoords_0 = (CoordTransformMatrix_0_P0 * vec3(aPosition, 1.0)).xy;
#endif
#if HAS_COLORS
  vColor = aColor;
#endif
#if HAS_COVERAGE
  vCoverage = aCoverage;
#endif
  highp vec2 position = (Matrix_P0 * vec3(aPosition, 1.0)).xy;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
