// LumaShader vertex shader
// Permutation dimensions (vert): GP_TYPE (0=DefaultGP, 1=QuadPerEdgeAAGP), HAS_COVERAGE
// Unified varying contract: per-vertex AA coverage is gated by HAS_COVERAGE (driven by the GP's
// AAType), independent of GP_TYPE. This lets both AA and non-AA quads share the GP_TYPE variant.
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
#endif

#if HAS_COVERAGE
layout(location = 0) out float vCoverage;
#endif

void main() {
#if HAS_COVERAGE
  vCoverage = inCoverage;
#endif
#if GP_TYPE == 0
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
#else
  highp vec2 position = aPosition;
#endif
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
