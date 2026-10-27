#version 450

#ifndef GRADIENT
#define GRADIENT 0
#endif
// The instance color attribute is unconditional: colorless batches broadcast white, so every
// variant binds the color in a single form.

layout(std140, set = TGFX_VERTEX_UBO_SET, binding = TGFX_VERTEX_UBO_BINDING) uniform VertexUniformBlock {
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
// Location 1 is the coverage stride filler of the shared 16-byte path-triangle layout (a
// constant 1.0); the coverage texture is this kernel's coverage source, so the slot is never
// read. Location 2 is the color stride filler (the instance color is the color source).
layout(location = 3) in vec2 aOffset;
layout(location = 4) in vec4 aColor;

layout(location = 0) out vec2 TransformedCoords_0;
layout(location = 1) out vec4 vColor;
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
  vColor = aColor;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
