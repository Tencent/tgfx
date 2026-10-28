// HairlineLineShader fragment entry: HairlineLineGeometryProcessor + EmptyXferProcessor/PorterDuffXP.
// The pipeline is the shared dedicated one (fill_fragment_main.inc); this file declares the family's
// interface and selects the steps.
// Fragment dimensions: HAS_XP.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = TGFX_FRAGMENT_UBO_SET, binding = TGFX_FRAGMENT_UBO_BINDING) uniform FragmentUniformBlock {
  // Exact paint color for common-color draws; the broadcast into the per-vertex attribute is
  // 8-bit quantized, so HasCommonColor selects this uniform when set.
  vec4 Color;
  int HasCommonColor;
  float Coverage;
  int AAEnabled;
  vec4 Rect;
  int HasClip;
#include "rrect_clip_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in float vEdgeDistance;
layout(location = 1) in vec4 vColor;

#define XP_DST_TEX_BINDING 0
#define FILL_COLOR FILL_COLOR_VARYING_OR_COMMON
#define FILL_COVERAGE FILL_COVERAGE_HAIRLINE_LINE
#define FILL_CLIP 1
#include "fill_fragment_main.inc"
