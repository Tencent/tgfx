// RoundStrokeRectFillShader vertex shader
// Processor layout: RoundStrokeRectGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   HAS_UV_MATRIX: whether a UV matrix transform is used (no per-vertex UV coord)
// The coverage and ellipse-radii attributes are unconditional: non-AA draws write a 1.0 coverage
// and zero radii, and the fragment stage selects the AA ramp through the AAEnabled runtime
// uniform. The color attribute is likewise unconditional (common-color batches broadcast the
// paint color; the exact color rides the Color uniform through HasCommonColor).
#version 450

#ifndef HAS_UV_MATRIX
#define HAS_UV_MATRIX 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
#if HAS_UV_MATRIX
  mat3 CoordTransformMatrix_0;
#endif
};

layout(location = 0) in vec2 inPosition;
layout(location = 1) in float inCoverage;
layout(location = 2) in vec2 inEllipseOffset;
layout(location = 3) in vec2 inEllipseRadii;
#if !HAS_UV_MATRIX
layout(location = 4) in vec2 inUVCoord;
#endif
layout(location = 5) in vec4 inColor;

layout(location = 0) out vec2 vEllipseOffsets;
layout(location = 1) out float vCoverage;
layout(location = 2) out vec2 vEllipseRadii;
layout(location = 3) out vec4 vColor;
#if HAS_UV_MATRIX
layout(location = 6) out vec2 TransformedCoords_0;
#endif

void main() {
  vEllipseOffsets = inEllipseOffset;
  vCoverage = inCoverage;
  vEllipseRadii = inEllipseRadii;
  vColor = inColor;
#if HAS_UV_MATRIX
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(inPosition, 1.0)).xy;
#endif
  gl_Position = vec4(inPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
