// HairlineQuadShader fragment entry: HairlineQuadGeometryProcessor + EmptyXferProcessor/PorterDuffXP.
// Uses Loop-Blinn quadratic curve anti-aliasing: u^2 - v = 0 defines the curve
// (hairline_quad_coverage.inc). The pipeline is the shared dedicated one (fill_fragment_main.inc);
// this file declares the family's interface and selects the steps.
// Fragment dimensions: HAS_XP.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  float Coverage;
  int AAEnabled;
  vec4 Rect;
  int HasClip;
#include "rrect_clip_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec4 vHairQuadEdge;

#define XP_DST_TEX_BINDING 0
#define FILL_COVERAGE FILL_COVERAGE_HAIRLINE_QUAD
#define FILL_CLIP 1
#include "fill_fragment_main.inc"
