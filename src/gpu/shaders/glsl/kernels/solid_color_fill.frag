// SolidColorFillShader fragment entry: DefaultGeometryProcessor + EmptyXferProcessor/PorterDuffXP,
// no fragment processor. The fill color is the DefaultGP Color uniform. The pipeline is the shared
// dedicated one (fill_fragment_main.inc); this file declares the family's interface and selects the
// steps. The coverage source is the vCoverage varying (a constant 1.0 for non-AA path triangles,
// so the modulation is the identity there).
// Fragment dimensions: HAS_XP.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = TGFX_FRAGMENT_UBO_SET, binding = TGFX_FRAGMENT_UBO_BINDING) uniform FragmentUniformBlock {
  vec4 Color;
  vec4 Rect;
  int HasClip;
#include "coverage/rrect_clip_uniforms.inc"
#if HAS_XP
  vec2 DstTextureUpperLeft;
  vec2 DstTextureCoordScale;
  int XPBlendMode;
#endif
  int OutputAlphaSwizzle;
};

layout(location = 0) in float vCoverage;

#define XP_DST_TEX_BINDING 0

#define FILL_COVERAGE FILL_COVERAGE_VERTEX
#define FILL_CLIP 1
#define FILL_OUTPUT_SWIZZLE 1
#include "pipeline/fill/fill_fragment_main.inc"
