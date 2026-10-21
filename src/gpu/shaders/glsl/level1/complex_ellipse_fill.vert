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

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
};

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inEllipseOffset;
layout(location = 3) in vec4 inEllipseRadii;
layout(location = 4) in vec2 inEdgeDist;
#if STROKE
layout(location = 5) in vec2 inStrokeWidth;
#endif

layout(location = 0) out vec2 vEllipseOffsets;
layout(location = 1) out vec4 vEllipseRadii;
layout(location = 2) out vec2 vEdgeDist;
layout(location = 3) out vec4 vColor;
#if STROKE
layout(location = 4) out vec2 vStrokeWidth;
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
