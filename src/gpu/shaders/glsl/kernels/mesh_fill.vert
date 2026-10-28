// MeshFillShader vertex shader
// Processor layout: MeshGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   HAS_TEX_COORDS: whether user-provided texture coordinates are present
// The coverage and color attributes are unconditional: vertex meshes always write a constant
// 1.0f coverage and the color slot (white for meshes without per-vertex colors), and the
// fragment stage picks the exact Color uniform for colorless draws through the HasCommonColor
// runtime uniform.
#version 450

#ifndef HAS_TEX_COORDS
#define HAS_TEX_COORDS 0
#endif

layout(std140, set = TGFX_VERTEX_UBO_SET, binding = TGFX_VERTEX_UBO_BINDING) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix;
  mat3 CoordTransformMatrix_0;
};

layout(location = 0) in vec2 aPosition;
layout(location = 1) in float aCoverage;
#if HAS_TEX_COORDS
layout(location = 2) in vec2 aTexCoord;
#endif
layout(location = 2 + HAS_TEX_COORDS) in vec4 aColor;

layout(location = 0) out vec2 TransformedCoords_0;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vCoverage;

void main() {
#if HAS_TEX_COORDS
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(aTexCoord, 1.0)).xy;
#else
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(aPosition, 1.0)).xy;
#endif
  vColor = aColor;
  vCoverage = aCoverage;
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
