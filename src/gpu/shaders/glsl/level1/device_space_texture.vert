// Device-space texture vertex shader
// Permutation dimensions (vert): GP_TYPE, HAS_COVERAGE. DeviceSpaceTextureShader compiles the
// default GP_TYPE=0/HAS_COVERAGE=0 variant; DeviceSpaceTexturedEffectShader uses the full domain.
#version 450

#ifndef GP_TYPE
#define GP_TYPE 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
#if GP_TYPE == 0
  mat3 Matrix;
#endif
};

layout(location = 0) in vec2 aPosition;
#if HAS_COVERAGE
layout(location = 1) in float inCoverage;
layout(location = 0) out float vCoverage;
#endif

void main() {
#if GP_TYPE == 0
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
#else
  highp vec2 position = aPosition;
#endif
#if HAS_COVERAGE
  vCoverage = inCoverage;
#endif
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
