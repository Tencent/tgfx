// QuadColorFillShader fragment shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + EmptyXferProcessor/PorterDuffXP (no FP)
// Permutation dimensions: HAS_XP, HAS_MASK_TEXTURE
//
// Color comes only from the vColor varying and coverage only from vCoverage: providers broadcast
// the paint color for uniform-color batches and emit coverage=1.0 for non-AA draws, so neither
// HAS_COLOR nor HAS_COVERAGE exists as a dimension.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_MASK_TEXTURE
#define HAS_MASK_TEXTURE 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Rect;
  int HasClip;
#if HAS_MASK_TEXTURE
  mat3 DeviceCoordMatrix;
#endif
#if HAS_XP
  vec2 DstTextureUpperLeft;
  vec2 DstTextureCoordScale;
  int XPBlendMode;
#endif
};

layout(location = 0) in float vCoverage;
layout(location = 1) in vec4 vColor;

// The mask texture (device-space coverage) occupies binding 0; the XP dst texture, if any, follows.
#if HAS_MASK_TEXTURE
layout(set = 1, binding = 0) uniform sampler2D MaskTextureSampler;
  #define XP_DST_TEX_BINDING 1
#else
  #define XP_DST_TEX_BINDING 0
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "aa_rect_clip_coverage.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  float maskAlpha = 1.0;
#if HAS_MASK_TEXTURE
  highp vec3 maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
  maskAlpha = texture(MaskTextureSampler, maskCoord.xy).r;
#endif
  float totalCoverage = vCoverage * maskAlpha * aaRectClipCoverage();

#if HAS_XP
  fragColor = applyPorterDuffXP(vColor, vec4(totalCoverage));
#else
  fragColor = vColor * totalCoverage;
#endif
}
