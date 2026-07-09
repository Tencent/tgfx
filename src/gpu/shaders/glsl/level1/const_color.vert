// ConstColorShader vertex shader
// No permutation dimensions — single variant.
#version 450

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix_P0;
};

layout(location = 0) in vec2 aPosition;

void main() {
  highp vec2 position = (Matrix_P0 * vec3(aPosition, 1.0)).xy;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
