// QuadColorFillShader vertex shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + EmptyXferProcessor (no FP)
// Permutation dimensions: HAS_COVERAGE. The color attribute is unconditional: rect providers
// always write a color slot (broadcasting the common color for uniform-color batches), so the
// per-vertex varying is the only color path and HAS_COLOR is no longer a dimension.
#version 450

#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
};

layout(location = 0) in vec2 aPosition;

#if HAS_COVERAGE
layout(location = 1) in float inCoverage;
layout(location = 2) in vec4 inColor;
#else
layout(location = 1) in vec4 inColor;
#endif

#if HAS_COVERAGE
layout(location = 0) out float vCoverage;
#endif
layout(location = 1) out vec4 vColor;

void main() {
#if HAS_COVERAGE
  vCoverage = inCoverage;
#endif
  vColor = inColor;

  gl_Position = vec4(aPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
