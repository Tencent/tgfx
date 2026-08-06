#version 450

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 UVMatrix;
  mat3 ViewMatrix;
  mat3 CoordTransformMatrix_0;
};

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aOffset;
layout(location = 2) in vec4 aColor;

layout(location = 0) out vec2 TransformedCoords_0;
layout(location = 1) out vec4 vColor;

void main() {
  highp vec2 local = (UVMatrix * vec3(aPosition, 1.0)).xy;
  highp vec2 position = (ViewMatrix * vec3(aPosition, 1.0)).xy + aOffset;
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(local, 1.0)).xy;
  vColor = aColor;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
