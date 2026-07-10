// ShapeInstancedFillShader fragment shader
// Processor layout: ShapeInstancedGeometryProcessor() + EmptyXferProcessor()
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

layout(location = 0) in vec2 TransformedCoords_0;
#if HAS_AA
layout(location = 1) in float vCoverage;
#endif
#if HAS_COLORS
layout(location = 2) in vec4 vColor;
#endif

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COLORS
  vec4 outputColor = vColor;
#else
  vec4 outputColor = vec4(1.0);
#endif

#if HAS_AA
  vec4 outputCoverage = vec4(vCoverage);
#else
  vec4 outputCoverage = vec4(1.0);
#endif

  fragColor = outputColor * outputCoverage;
}
