// SolidColorFillShader fragment shader
// Processor layout: DefaultGeometryProcessor + EmptyXferProcessor/PorterDuffXP (no fragment
// processors). The fill color is the DefaultGP Color uniform.
// Permutation dimensions (frag): HAS_COVERAGE, HAS_XP
#version 450

#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
#if HAS_XP
  vec2 DstTextureUpperLeft;
  vec2 DstTextureCoordScale;
  int XPBlendMode;
#endif
};

#if HAS_COVERAGE
layout(location = 0) in float vCoverage;
#endif

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 outputColor = Color;

#if HAS_XP
  #if HAS_COVERAGE
  fragColor = applyPorterDuffXP(outputColor, vec4(vCoverage));
  #else
  fragColor = applyPorterDuffXP(outputColor, vec4(1.0));
  #endif
#else
  #if HAS_COVERAGE
  fragColor = outputColor * vCoverage;
  #else
  fragColor = outputColor;
  #endif
#endif
}
