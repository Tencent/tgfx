// HairlineLineShader vertex shader
// Processor layout: HairlineLineGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   HAS_AA: whether coverage-based anti-aliasing is enabled
#version 450

layout(std140, set = TGFX_VERTEX_UBO_SET, binding = TGFX_VERTEX_UBO_BINDING) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix;
};

layout(location = 0) in vec2 aPosition;
layout(location = 1) in float aEdgeDistance;
// The color attribute is unconditional on this GP: the hairline buffers are shared across draws, so
// the slot carries a white filler and the exact paint color rides the Color uniform, selected by
// HasCommonColor. Declaring it keeps the reflected vertex interface identical to the GP's
// attribute list (aPosition, aEdgeDistance, inColor), which the program creator checks by count.
layout(location = 2) in vec4 inColor;

layout(location = 0) out float vEdgeDistance;
layout(location = 1) out vec4 vColor;

void main() {
  vEdgeDistance = aEdgeDistance;
  vColor = inColor;
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
