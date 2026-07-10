// BlendMergeShader vertex shader
// No vertex permutation dimensions — single vertex variant.
#version 450

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix;
  mat3 CoordTransform;
};

layout(location = 0) in vec2 aPosition;
layout(location = 0) out vec2 TransformedCoords_0;

void main() {
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
  TransformedCoords_0 = (CoordTransform * vec3(aPosition, 1.0)).xy;
}
