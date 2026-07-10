// MeshFillShader fragment shader
// Processor layout: MeshGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   HAS_TEX_COORDS: whether user-provided texture coordinates are present
//   HAS_COLORS: whether per-vertex colors are present
//   HAS_COVERAGE: whether per-vertex coverage is present
#version 450

#ifndef HAS_COLORS
#define HAS_COLORS 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

#if !HAS_COLORS
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
};
#endif

layout(location = 0) in vec2 TransformedCoords_0;
#if HAS_COLORS
layout(location = 1) in vec4 vColor;
#endif
#if HAS_COVERAGE
layout(location = 2) in float vCoverage;
#endif

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COLORS
  vec4 outputColor = vColor;
#else
  vec4 outputColor = Color;
#endif

#if HAS_COVERAGE
  vec4 outputCoverage = vec4(vCoverage);
#else
  vec4 outputCoverage = vec4(1.0);
#endif

  fragColor = outputColor * outputCoverage;
}
