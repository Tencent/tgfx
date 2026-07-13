// QuadColorFillShader fragment shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + EmptyXferProcessor (no FP)
// Permutation dimensions: HAS_COVERAGE, HAS_COLOR
#version 450

#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_COLOR
#define HAS_COLOR 0
#endif

#if !HAS_COLOR
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
};
#endif

#if HAS_COVERAGE
layout(location = 0) in float vCoverage;
#endif

#if HAS_COLOR
layout(location = 1) in vec4 vColor;
#endif

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COLOR
  vec4 outputColor = vColor;
#else
  vec4 outputColor = Color;
#endif

  // EmptyXferProcessor: output = outputColor * outputCoverage
#if HAS_COVERAGE
  fragColor = outputColor * vCoverage;
#else
  fragColor = outputColor;
#endif
}
