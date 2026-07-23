// TiledTextureFillShader vertex shader
// Permutation dimensions (vert): GP_TYPE (0=DefaultGP, 1=QuadPerEdgeAAGP)
// The coordinate is always affine (vec2); perspective TiledTextureEffect draws fall back to
// ProgramBuilder, so no perspective path exists here.
#version 450

#ifndef GP_TYPE
#define GP_TYPE 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
#if GP_TYPE == 0
  mat3 Matrix;
#endif
  mat3 CoordTransformMatrix_0;
};

layout(location = 0) in vec2 aPosition;

layout(location = 0) out vec2 TransformedCoords_0;

void main() {
#if GP_TYPE == 0
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
#else
  // QuadPerEdgeAAGeometryProcessor pre-transforms vertices to device space.
  highp vec2 position = aPosition;
#endif
  vec3 coordResult = CoordTransformMatrix_0 * vec3(aPosition, 1.0);
  TransformedCoords_0 = coordResult.xy;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
