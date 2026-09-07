// GlassRefractionShader vertex shader
// Processor layout: EllipseGeometryProcessor(common color) + GlassRefractionFragmentProcessor.
// Permutation dimensions (vert): none (the Glass draw always uses the common-color attribute
// layout, so the vertex has no color attribute to carry).
#version 450


layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 CoordTransformMatrix_0;
};

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inEllipseOffset;
layout(location = 2) in vec4 inEllipseRadii;

layout(location = 0) out vec2 vEllipseOffsets;
layout(location = 1) out vec4 vEllipseRadii;
layout(location = 2) out vec2 TransformedCoords_0;

void main() {
  vEllipseOffsets = inEllipseOffset;
  vEllipseRadii = inEllipseRadii;
  gl_Position = vec4(inPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(inPosition, 1.0)).xy;
}
