// MeshFillShader vertex shader
// Processor layout: MeshGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   HAS_TEX_COORDS: whether user-provided texture coordinates are present
//   HAS_COVERAGE: whether per-vertex coverage is present
// The color attribute is unconditional: the vertex buffer always carries the slot (white for
// meshes without per-vertex colors), and the fragment stage picks the exact Color uniform for
// colorless draws through the HasCommonColor runtime uniform.
#version 450

#ifndef HAS_TEX_COORDS
#define HAS_TEX_COORDS 0
#endif
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
layout(location = 1) in float aCoverage;
#endif
#if HAS_TEX_COORDS
layout(location = 1 + HAS_COVERAGE) in vec2 aTexCoord;
#endif
layout(location = 1 + HAS_COVERAGE + HAS_TEX_COORDS) in vec4 aColor;

layout(location = 0) out vec2 TransformedCoords_0;
layout(location = 1) out vec4 vColor;
#if HAS_COVERAGE
layout(location = 2) out float vCoverage;
#endif

void main() {
#if HAS_TEX_COORDS
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(aTexCoord, 1.0)).xy;
#else
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(aPosition, 1.0)).xy;
#endif
  vColor = aColor;
#if HAS_COVERAGE
  vCoverage = aCoverage;
#endif
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
