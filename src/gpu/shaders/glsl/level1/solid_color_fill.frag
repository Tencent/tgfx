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
  vec4 Rect;
  int HasClip;
#if HAS_XP
  vec2 DstTextureUpperLeft;
  vec2 DstTextureCoordScale;
  int XPBlendMode;
#endif
  int OutputAlphaSwizzle;
};

#if HAS_COVERAGE
layout(location = 0) in float vCoverage;
#endif

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "aa_rect_clip_coverage.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  float totalCoverage = aaRectClipCoverage();
#if HAS_COVERAGE
  totalCoverage *= vCoverage;
#endif

#if HAS_XP
  fragColor = applyPorterDuffXP(Color, vec4(totalCoverage));
#else
  fragColor = Color * totalCoverage;
#endif
#include "output_swizzle.inc"
}
