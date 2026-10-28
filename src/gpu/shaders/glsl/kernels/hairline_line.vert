// HairlineLineShader vertex shader
// Processor layout: HairlineLineGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   HAS_AA: whether coverage-based anti-aliasing is enabled
#version 450

layout(std140, set = TGFX_VERTEX_UBO_SET, binding = TGFX_VERTEX_UBO_BINDING) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix;
};

#include "geometry/gp_hairline_layout.inc"

layout(location = GP_HAIRLINE_IN_POSITION) in vec2 aPosition;
layout(location = GP_HAIRLINE_IN_EDGE) in float aEdgeDistance;

layout(location = GP_HAIRLINE_V_EDGE) out float vEdgeDistance;

void main() {
  vEdgeDistance = aEdgeDistance;
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
