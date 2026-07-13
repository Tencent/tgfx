// QuadColorFillShader vertex shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + EmptyXferProcessor (no FP)
// Permutation dimensions: HAS_COVERAGE, HAS_COLOR
#version 450

#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_COLOR
#define HAS_COLOR 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
};

layout(location = 0) in vec2 aPosition;

#if HAS_COVERAGE
layout(location = 1) in float inCoverage;
  #if HAS_COLOR
layout(location = 2) in vec4 inColor;
  #endif
#else
  #if HAS_COLOR
layout(location = 1) in vec4 inColor;
  #endif
#endif

#if HAS_COVERAGE
layout(location = 0) out float vCoverage;
#endif

#if HAS_COLOR
layout(location = 1) out vec4 vColor;
#endif

void main() {
#if HAS_COVERAGE
  vCoverage = inCoverage;
#endif

#if HAS_COLOR
  vColor = inColor;
#endif

  gl_Position = vec4(aPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
