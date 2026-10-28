// ComplexNonAARRectFillShader vertex shader
// Processor layout: ComplexNonAARRectGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   STROKE: whether stroke mode is enabled
// The color attribute is unconditional: common-color batches broadcast the paint color into the
// slot (an 8-bit quantized filler), and the exact common color rides the fragment stage's Color
// uniform, selected by the HasCommonColor runtime uniform.
#version 450

#ifndef STROKE
#define STROKE 0
#endif

layout(std140, set = TGFX_VERTEX_UBO_SET, binding = TGFX_VERTEX_UBO_BINDING) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
};

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inLocalCoord;
layout(location = 2) in vec4 inXRadii;
layout(location = 3) in vec4 inYRadii;
layout(location = 4) in vec4 inRectBounds;
layout(location = 5) in vec4 inColor;
#if STROKE
layout(location = 6) in vec2 inStrokeWidth;
#endif

layout(location = 0) out vec2 vLocalCoord;
layout(location = 1) out vec4 vXRadii;
layout(location = 2) out vec4 vYRadii;
layout(location = 3) out vec4 vRectBounds;
layout(location = 4) out vec4 vColor;
#if STROKE
layout(location = 5) out vec2 vStrokeWidth;
#endif

void main() {
  vLocalCoord = inLocalCoord;
  vXRadii = inXRadii;
  vYRadii = inYRadii;
  vRectBounds = inRectBounds;
  vColor = inColor;
#if STROKE
  vStrokeWidth = inStrokeWidth;
#endif
  gl_Position = vec4(inPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
