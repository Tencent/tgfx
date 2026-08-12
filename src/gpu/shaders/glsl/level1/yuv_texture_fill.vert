// YUVTextureFillShader vertex shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + TextureEffect(YUV) + EmptyXferProcessor
// Permutation dimensions (vert): HAS_UV_COORD (coverage and color are unconditional attributes)
// The transformed coordinate is always emitted as a vec3 and perspective-divided in the fragment
// shader, matching QuadTextureFillShader: affine transforms yield z=1 so the divide is a no-op.
#version 450

#ifndef HAS_UV_COORD
#define HAS_UV_COORD 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 CoordTransformMatrix_0;
};

layout(location = 0) in vec2 aPosition;
layout(location = 1) in float inCoverage;
#if HAS_UV_COORD
layout(location = 2) in vec2 uvCoord;
layout(location = 3) in vec4 inColor;
#else
layout(location = 2) in vec4 inColor;
#endif

layout(location = 0) out vec3 TransformedCoords_0;
layout(location = 1) out float vCoverage;
layout(location = 2) out vec4 vColor;

void main() {
  vCoverage = inCoverage;
  vColor = inColor;
#if HAS_UV_COORD
  vec3 coordResult = CoordTransformMatrix_0 * vec3(uvCoord, 1.0);
#else
  vec3 coordResult = CoordTransformMatrix_0 * vec3(aPosition, 1.0);
#endif
  TransformedCoords_0 = coordResult;
  gl_Position = vec4(aPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
