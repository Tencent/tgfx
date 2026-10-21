// EllipseFillShader vertex shader
// Processor layout: EllipseGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   STROKE: whether stroke mode is enabled
// The color attribute is unconditional: common-color batches broadcast the paint color into the
// slot (a single variant), and the exact common color rides the Color uniform through the
// HasCommonColor runtime uniform (the broadcast is 8-bit quantized).
#version 450

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
};

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inEllipseOffset;
layout(location = 3) in vec4 inEllipseRadii;

layout(location = 0) out vec2 vEllipseOffsets;
layout(location = 1) out vec4 vEllipseRadii;
layout(location = 2) out vec4 vColor;

void main() {
  vEllipseOffsets = inEllipseOffset;
  vEllipseRadii = inEllipseRadii;
  vColor = inColor;
  gl_Position = vec4(inPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
