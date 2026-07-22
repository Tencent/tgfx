// MaskFillShader vertex shader
// Processor layout: DefaultGeometryProcessor + one coverage TextureEffect (a shape mask).
// No permutation dimensions — single variant. The coverage sampler's coordinate is produced from
// the vertex position via the fragment processor's coord transform (CoordTransformMatrix_0), the
// same mechanism TextureFillShader uses for its color texture.
#version 450

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix;
  mat3 CoordTransformMatrix_0;
};

layout(location = 0) in vec2 aPosition;

layout(location = 0) out vec2 TransformedCoords_0;

void main() {
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(aPosition, 1.0)).xy;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
