// QuadTextureFillShader fragment entry: QuadPerEdgeAAGeometryProcessor + TextureEffect +
// EmptyXferProcessor/PorterDuffXP. The pipeline is the shared fixed one (fill_fragment_main.inc);
// this file declares the family's interface and selects the steps.
// Fragment dimensions: HAS_SUBSET, HAS_XP, HAS_LOCAL_MASK.
//
// Subset clamping:
//   HAS_SUBSET=1: the per-quad vTexSubset varying clamps first, then the Subset uniform (the
//                 half-pixel-inset safe range).
//   HAS_SUBSET=0: the Subset uniform alone; it holds the full texture bounds when the source has no
//                 real subset, so the clamp is a no-op.
#version 450

#ifndef HAS_SUBSET
#define HAS_SUBSET 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_LOCAL_MASK
#define HAS_LOCAL_MASK 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Rect;
  int HasClip;
#include "rrect_clip_uniforms.inc"
  // Always present. For HAS_SUBSET=1 it provides the half-pixel-inset safe range; for HAS_SUBSET=0
  // it is the sole clamp bound (full texture bounds when no real subset, so the clamp is a no-op).
  vec4 Subset;
  // RGBAAA dual-plane alpha: always declared. Only read when HasRgbaaa != 0 (set at runtime), so it
  // is a uniform branch rather than a compile-time permutation.
  vec2 AlphaStart;
  // The device mask is a runtime uniform: the sampler is always bound (padded with the shared
  // dummy when the draw has no mask) and HasDeviceMask selects the application.
  mat3 DeviceCoordMatrix;
  // Decoy write target for the coverage mask FP (see GLSLDeviceSpaceTextureEffect::onSetData);
  // never read — the mask is sampled unclamped.
  vec4 DeviceMaskSubset;
  int HasDeviceMask;
#if HAS_XP
  vec2 DstTextureUpperLeft;
  vec2 DstTextureCoordScale;
  int XPBlendMode;
#endif
  // Alpha-only source (R8 texture) and RGBAAA dual-plane are runtime uniforms rather than
  // compile-time permutations: both are pure fragment math (RGBAAA adds one coherent-branch sample),
  // so folding them into uniform branches shrinks the variant count.
  int AlphaOnly;
  int HasRgbaaa;
  int OutputAlphaSwizzle;
  // Gates the Subset-uniform clamp. The runtime (JIT) TextureEffect only clamps when
  // needSubset() holds; a unconditional clamp here diverges where coordinates leave [0,1]
  // (perspective outsides, quad edge extrapolation): clamping zeroes the mip-level derivatives
  // and shifts bilinear edge texels by half a texel. Uploaders set this from needSubset().
  int SubsetClampOn;
  // Exact common-color override: the vColor varying is UByte4Normalized (8-bit quantized), which
  // shifts fractional-alpha outputs by one LSB versus the runtime renderer's float Color uniform.
  // HasCommonColor selects the Color uniform for uniform-color draws (same contract as
  // quad_color_fill.frag).
  vec4 Color;
  int HasCommonColor;
};

layout(location = 0) in vec3 TransformedCoords_0;

layout(location = 1) in float vCoverage;

layout(location = 2) in vec4 vColor;

#if HAS_SUBSET
layout(location = 3) in vec4 vTexSubset;
#endif

#if HAS_LOCAL_MASK
layout(location = 4) in vec3 TransformedCoords_1;
#endif

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

// The device-mask sampler always occupies binding 1 (padded with the shared dummy when the draw
// has no mask); the local mask, if any, follows at 2, and the XP dst texture after both.
layout(set = 1, binding = 1) uniform sampler2D MaskTextureSampler;
#if HAS_LOCAL_MASK
layout(set = 1, binding = 2) uniform sampler2D LocalMaskSampler;
  #define XP_DST_TEX_BINDING 3
#else
  #define XP_DST_TEX_BINDING 2
#endif

#define FILL_COLOR FILL_COLOR_VARYING_OR_COMMON
#define FILL_SOURCE FILL_SOURCE_TEXTURE
#define FILL_COVERAGE FILL_COVERAGE_VERTEX
#define FILL_DEVICE_MASK FILL_DEVICE_MASK_RUNTIME
#define FILL_LOCAL_MASK HAS_LOCAL_MASK
#define FILL_CLIP 1
#define FILL_OUTPUT_SWIZZLE 1
#include "fill_fragment_main.inc"
