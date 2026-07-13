// QuadColorFillShader fragment shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + EmptyXferProcessor/PorterDuffXP (no FP)
// Permutation dimensions: HAS_COVERAGE, HAS_COLOR, HAS_XP
#version 450

#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_COLOR
#define HAS_COLOR 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

#if !HAS_COLOR || HAS_XP
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#if !HAS_COLOR
  vec4 Color;
#endif
#if HAS_XP
  vec2 DstTextureUpperLeft;
  vec2 DstTextureCoordScale;
  int XPBlendMode;
#endif
};
#endif

#if HAS_COVERAGE
layout(location = 0) in float vCoverage;
#endif

#if HAS_COLOR
layout(location = 1) in vec4 vColor;
#endif

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COLOR
  vec4 outputColor = vColor;
#else
  vec4 outputColor = Color;
#endif

#if HAS_XP
  #if HAS_COVERAGE
  fragColor = applyPorterDuffXP(outputColor, vec4(vCoverage));
  #else
  fragColor = applyPorterDuffXP(outputColor, vec4(1.0));
  #endif
#else
  // EmptyXferProcessor: output = outputColor * outputCoverage
  #if HAS_COVERAGE
  fragColor = outputColor * vCoverage;
  #else
  fragColor = outputColor;
  #endif
#endif
}
