// Device-space texture vertex shader
// Permutation dimensions (vert): HAS_COVERAGE. The position always goes through the Matrix
// uniform (identity for pre-transformed quads), so one variant serves both geometry processors.
#version 450

#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix;
};

layout(location = 0) in vec2 aPosition;
#if HAS_COVERAGE
layout(location = 1) in float inCoverage;
layout(location = 0) out float vCoverage;
#endif

void main() {
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
#if HAS_COVERAGE
  vCoverage = inCoverage;
#endif
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
