#version 450

#ifndef GRADIENT
#define GRADIENT 0
#endif
#ifndef HAS_COLORS
#define HAS_COLORS 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 UVMatrix;
  mat3 ViewMatrix;
  mat3 CoordTransformMatrix_0;
#if GRADIENT
  // The gradient color FP precedes the coverage FP, so its transform takes index 0 and the
  // coverage texture's transform moves to index 1.
  mat3 CoordTransformMatrix_1;
#endif
};

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aOffset;
#if HAS_COLORS
layout(location = 2) in vec4 aColor;
#endif

layout(location = 0) out vec2 TransformedCoords_0;
#if HAS_COLORS
layout(location = 1) out vec4 vColor;
#endif
#if GRADIENT
layout(location = 2) out vec2 GradientCoords;
#endif

void main() {
  highp vec2 local = (UVMatrix * vec3(aPosition, 1.0)).xy;
  highp vec2 position = (ViewMatrix * vec3(aPosition, 1.0)).xy + aOffset;
#if GRADIENT
  TransformedCoords_0 = (CoordTransformMatrix_1 * vec3(local, 1.0)).xy;
  GradientCoords = (CoordTransformMatrix_0 * vec3(local, 1.0)).xy;
#else
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(local, 1.0)).xy;
#endif
#if HAS_COLORS
  vColor = aColor;
#endif
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
