// Shared blur-source vertex template: position transform plus one affine texture
// coordinate, with the coord output preceding the position write. Used by
// GaussianBlur1DShader and GlassUDFTentBlurShader.
#version 450
layout(std140, set = TGFX_VERTEX_UBO_SET, binding = TGFX_VERTEX_UBO_BINDING) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix;
  mat3 CoordTransformMatrix_0;
};
layout(location = 0) in vec2 aPosition;
// The coverage and color attributes are declared but never read: a blur fill draws one opaque
// quad over its target, while the geometry processor's attribute list always carries both slots.
// The program creator compares the reflected inputs against that list, so they must be declared for
// the interface to line up.
layout(location = 1) in float inCoverage;
layout(location = 2) in vec4 inColor;
layout(location = 0) out vec2 TransformedCoords_0;
void main() {
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(aPosition, 1.0)).xy;
}
