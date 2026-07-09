// ShapeInstancedFillShader vertex shader
// Processor layout: ShapeInstancedGeometryProcessor(_P0) + EmptyXferProcessor(_P1)
// Permutation dimensions (injected as #define 0/1):
//   HAS_COLORS: whether per-instance colors are used
//   HAS_AA: whether coverage-based AA is enabled
#version 450

#ifndef HAS_COLORS
#define HAS_COLORS 0
#endif
#ifndef HAS_AA
#define HAS_AA 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 UVMatrix_P0;
  mat3 ViewMatrix_P0;
  mat3 CoordTransformMatrix_0_P0;
};

// Vertex attributes
layout(location = 0) in vec2 aPosition;
#if HAS_AA
layout(location = 1) in float inCoverage;
#endif

// Instance attributes
layout(location = 2) in vec2 aOffset;
#if HAS_COLORS
layout(location = 3) in vec4 aColor;
#endif

layout(location = 0) out vec2 TransformedCoords_0;
#if HAS_AA
layout(location = 1) out float vCoverage;
#endif
#if HAS_COLORS
layout(location = 2) out vec4 vColor;
#endif

void main() {
  // Recover local coords via UV matrix
  highp vec2 local = (UVMatrix_P0 * vec3(aPosition, 1.0)).xy;
  // Transform to device space + instance offset
  highp vec2 position = (ViewMatrix_P0 * vec3(aPosition, 1.0)).xy + aOffset;
  // Coord transform for FP sampling
  TransformedCoords_0 = (CoordTransformMatrix_0_P0 * vec3(local, 1.0)).xy;
#if HAS_AA
  vCoverage = inCoverage;
#endif
#if HAS_COLORS
  vColor = aColor;
#endif
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
