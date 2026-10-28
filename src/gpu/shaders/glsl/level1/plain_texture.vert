// Shared plain-texture vertex template: position transform plus one affine texture
// coordinate (CoordTransformMatrix_0). Used by MaskFillShader, whose coverage mask rides the
// coord-transform mechanism.
#version 450
layout(std140, set = TGFX_VERTEX_UBO_SET, binding = TGFX_VERTEX_UBO_BINDING) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix;
  mat3 CoordTransformMatrix_0;
};
layout(location = 0) in vec2 aPosition;
// The coverage and color attributes are unconditional on the DefaultGP: the shared vertex buffers
// always carry both slots (a constant 1.0 coverage and a white filler for non-AA and common-color
// draws), so the kernel binds one vertex layout, and both modulations are the identity there.
layout(location = 1) in float inCoverage;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 TransformedCoords_0;
layout(location = 1) out float vCoverage;
layout(location = 2) out vec4 vColor;
void main() {
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(aPosition, 1.0)).xy;
  vCoverage = inCoverage;
  vColor = inColor;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
