// TiledTextureFillShader vertex shader
// Permutation dimensions (vert): HAS_PERSPECTIVE
#version 450

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix;
  mat3 CoordTransformMatrix_0;
};

layout(location = 0) in vec2 aPosition;

#if HAS_PERSPECTIVE
layout(location = 0) out vec3 TransformedCoords_0;
#else
layout(location = 0) out vec2 TransformedCoords_0;
#endif

void main() {
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  vec3 coordResult = CoordTransformMatrix_0 * vec3(aPosition, 1.0);
#if HAS_PERSPECTIVE
  TransformedCoords_0 = coordResult;
#else
  TransformedCoords_0 = coordResult.xy;
#endif
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
