// ComplexEllipseFillShader vertex shader
// Processor layout: ComplexEllipseGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   STROKE: whether stroke mode is enabled
// The color attribute is unconditional: common-color batches broadcast the paint color into the
// slot (a single variant), and the exact common color rides the Color uniform through the
// HasCommonColor runtime uniform (the broadcast is 8-bit quantized).
#version 450

#ifndef STROKE
#define STROKE 0
#endif

layout(std140, set = TGFX_VERTEX_UBO_SET, binding = TGFX_VERTEX_UBO_BINDING) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
};

#include "geometry/gp_complex_ellipse_layout.inc"

layout(location = 0) in vec2 inPosition;
layout(location = GP_COMPLEX_IN_COLOR) in vec4 inColor;
layout(location = GP_COMPLEX_IN_OFFSET) in vec2 inEllipseOffset;
layout(location = GP_COMPLEX_IN_RADII) in vec4 inEllipseRadii;
layout(location = GP_COMPLEX_IN_EDGE_DIST) in vec2 inEdgeDist;
#if STROKE
layout(location = GP_COMPLEX_IN_STROKE) in vec2 inStrokeWidth;
#endif

layout(location = GP_COMPLEX_V_OFFSETS) out vec2 vEllipseOffsets;
layout(location = GP_COMPLEX_V_RADII) out vec4 vEllipseRadii;
layout(location = GP_COMPLEX_V_EDGE_DIST) out vec2 vEdgeDist;
layout(location = GP_COMPLEX_V_COLOR) out vec4 vColor;
#if STROKE
layout(location = GP_COMPLEX_V_STROKE) out vec2 vStrokeWidth;
#endif

void main() {
  vEllipseOffsets = inEllipseOffset;
  vEllipseRadii = inEllipseRadii;
  vEdgeDist = inEdgeDist;
  vColor = inColor;
#if STROKE
  vStrokeWidth = inStrokeWidth;
#endif
  gl_Position = vec4(inPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
