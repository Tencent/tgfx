// HairlineQuadShader vertex shader
// Processor layout: HairlineQuadGeometryProcessor(_P0) + EmptyXferProcessor(_P1)
// Permutation dimensions (injected as #define 0/1):
//   HAS_AA: whether coverage-based anti-aliasing is enabled
#version 450

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix_P0;
};

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aHairQuadEdge;

layout(location = 0) out vec4 vHairQuadEdge;

void main() {
  vHairQuadEdge = aHairQuadEdge;
  highp vec2 position = (Matrix_P0 * vec3(aPosition, 1.0)).xy;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
