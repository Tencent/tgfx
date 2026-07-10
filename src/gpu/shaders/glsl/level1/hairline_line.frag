// HairlineLineShader fragment shader
// Processor layout: HairlineLineGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   HAS_AA: whether coverage-based anti-aliasing is enabled
#version 450

#ifndef HAS_AA
#define HAS_AA 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  float Coverage;
};

layout(location = 0) in float vEdgeDistance;

layout(location = 0) out vec4 fragColor;

void main() {
  highp float edgeAlpha = abs(vEdgeDistance);
  edgeAlpha = clamp(edgeAlpha, 0.0, 1.0);
#if !HAS_AA
  edgeAlpha = edgeAlpha >= 0.5 ? 1.0 : 0.0;
#endif

  vec4 outputColor = Color;
  vec4 outputCoverage = vec4(Coverage * edgeAlpha);

  fragColor = outputColor * outputCoverage;
}
