// QuadColorFillShader fragment entry: QuadPerEdgeAAGeometryProcessor + EmptyXferProcessor/
// PorterDuffXP, no fragment processor. The pipeline is the shared dedicated one
// (fill_fragment_main.inc); this file declares the family's interface and selects the steps.
// Fragment dimensions: HAS_XP, HAS_MASK_TEXTURE.
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
#include "rrect_clip_uniforms.inc"
  // Exact paint color for uniform-color draws. The per-vertex color attribute is UByte4Normalized
  // and loses precision on fractional colors, so the geometry processor's exact Color uniform is
  // preferred when HasCommonColor is set; otherwise the vColor varying is used (matching the
  // runtime codegen, which reads the per-vertex attribute only when there is no common color).
  vec4 Color;
  int HasCommonColor;
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

#define FILL_COLOR FILL_COLOR_VARYING_OR_COMMON
#define FILL_COVERAGE FILL_COVERAGE_VERTEX
#if HAS_MASK_TEXTURE
#define FILL_DEVICE_MASK FILL_DEVICE_MASK_ALWAYS
#endif
#define FILL_CLIP 1
#include "fill_fragment_main.inc"
