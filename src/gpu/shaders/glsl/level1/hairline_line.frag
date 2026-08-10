// HairlineLineShader fragment shader
// Processor layout: HairlineLineGeometryProcessor() + EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (injected as #define 0/1):
//   HAS_XP: 0=passthrough, 1=PorterDuff XP (dst texture blend)
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
#include "xp_uniforms.inc"
};

layout(location = 0) in float vEdgeDistance;

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "aa_rect_clip_coverage.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  highp float edgeAlpha = abs(vEdgeDistance);
  edgeAlpha = clamp(edgeAlpha, 0.0, 1.0);
  if (AAEnabled == 0) {
    edgeAlpha = edgeAlpha >= 0.5 ? 1.0 : 0.0;
  }

  vec4 outputCoverage = vec4(Coverage * edgeAlpha * aaRectClipCoverage());

#define TGFX_XP_SRC_COLOR (Color * outputCoverage)
#define TGFX_XP_SRC_UNPREMUL Color
#define TGFX_XP_COVERAGE outputCoverage
#include "xp_output.inc"
}
