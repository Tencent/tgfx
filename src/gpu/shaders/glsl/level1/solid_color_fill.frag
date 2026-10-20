// SolidColorFillShader fragment entry: DefaultGeometryProcessor + EmptyXferProcessor/PorterDuffXP,
// no fragment processor. The fill color is the DefaultGP Color uniform. The pipeline is the shared
// dedicated one (fill_fragment_main.inc); this file declares the family's interface and selects the
// steps.
// Fragment dimensions: HAS_COVERAGE, HAS_XP.
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
#include "rrect_clip_uniforms.inc"
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

#if HAS_COVERAGE
#define FILL_COVERAGE FILL_COVERAGE_VERTEX
#endif
#define FILL_CLIP 1
#define FILL_OUTPUT_SWIZZLE 1
#include "fill_fragment_main.inc"
