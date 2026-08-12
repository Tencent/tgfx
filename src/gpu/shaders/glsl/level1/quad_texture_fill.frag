// QuadTextureFillShader fragment shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + TextureEffect + EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (frag): HAS_SUBSET, HAS_XP, HAS_MASK_TEXTURE, HAS_LOCAL_MASK (coverage is unconditional).
//                                HAS_XP
// Note: HAS_YUV is always 0 at runtime — YUV textures fall back to ProgramBuilder.
// Vertex-driven varyings are controlled by vert permutation dimensions (
// HAS_SUBSET) which are communicated via matching varying declarations.
//
// Subset clamping modes:
//   HAS_SUBSET=1: Full subset — vertex attribute provides per-quad vTexSubset varying, and Subset
//                 uniform provides the half-pixel-inset safe range. Both are used for clamping.
//   HAS_SUBSET=0: Uniform-only clamp — no per-vertex subset attribute. The Subset uniform is always
//                 present and clamps the coordinate; when the source has no real subset it holds the
//                 full texture bounds, so the clamp is a no-op. This subsumes the former
//                 HAS_CLAMP_SUBSET dimension.
#version 450

#ifndef HAS_YUV
#define HAS_YUV 0
#endif
#ifndef HAS_SUBSET
#define HAS_SUBSET 0
#endif

// These are driven by vertex shader permutation but the fragment shader must declare matching
// varyings. The build tool defines them for each (vert, frag) pair.
#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_MASK_TEXTURE
#define HAS_MASK_TEXTURE 0
#endif
#ifndef HAS_LOCAL_MASK
#define HAS_LOCAL_MASK 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Rect;
  int HasClip;
  // Always present. For HAS_SUBSET=1 it provides the half-pixel-inset safe range; for HAS_SUBSET=0
  // it is the sole clamp bound (full texture bounds when no real subset, so the clamp is a no-op).
  vec4 Subset;
  // RGBAAA dual-plane alpha: always declared. Only read when HasRgbaaa != 0 (set at runtime), so it
  // is a uniform branch rather than a compile-time permutation.
  vec2 AlphaStart;
#if HAS_MASK_TEXTURE
  mat3 DeviceCoordMatrix;
  // Decoy write target for the coverage mask FP (see GLSLDeviceSpaceTextureEffect::onSetData);
  // never read — the mask is sampled unclamped.
  vec4 DeviceMaskSubset;
#endif
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

// A coverage mask (device-space or local-space) occupies binding 1; the XP dst texture, if any,
// follows at 2. HAS_MASK_TEXTURE (device) and HAS_LOCAL_MASK (local) are mutually exclusive.
#if HAS_MASK_TEXTURE
layout(set = 1, binding = 1) uniform sampler2D MaskTextureSampler;
  #define XP_DST_TEX_BINDING 2
#elif HAS_LOCAL_MASK
layout(set = 1, binding = 1) uniform sampler2D LocalMaskSampler;
  #define XP_DST_TEX_BINDING 2
#else
  #define XP_DST_TEX_BINDING 1
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "aa_rect_clip_coverage.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  // The only color source is the per-vertex varying: providers broadcast the record's paint color
  // for uniform-color batches, so no Color uniform fallback exists.
  vec4 outputColor = vColor;

  // Perspective divide. For affine transforms z is 1.0 (no-op); for perspective it applies the
  // correct division.
  highp vec2 texCoord = TransformedCoords_0.xy / TransformedCoords_0.z;
  highp vec2 finalCoord = texCoord;

#if HAS_SUBSET
  // Full subset: clamp first by per-quad varying bounds, then by uniform safe range.
  finalCoord = clamp(finalCoord, vTexSubset.xy, vTexSubset.zw);
  finalCoord = clamp(finalCoord, Subset.xy, Subset.zw);
#else
  // Uniform-only clamp. Subset holds the full texture bounds when no real subset applies, so this
  // degenerates to a no-op; otherwise it bounds the sample to the valid texel region.
  finalCoord = clamp(finalCoord, Subset.xy, Subset.zw);
#endif

  vec4 color = texture(TextureSampler_0, finalCoord);

  if (AlphaOnly != 0) {
    // Alpha-only textures use R8 format in Metal. The sampler returns (r, 0, 0, 1).
    // Replicate .r to all channels to get the actual alpha value.
    color = vec4(color.r);
  }

  if (HasRgbaaa != 0) {
    // RGBAAA dual-plane: the alpha lives in a separate region reached by AlphaStart. This second
    // sample is behind a uniform (coherent) branch, so non-RGBAAA draws skip it at no cost.
    color = clamp(color, 0.0, 1.0);
    highp vec2 alphaCoord = finalCoord + AlphaStart;
    vec4 alpha = texture(TextureSampler_0, alphaCoord);
    alpha = clamp(alpha, 0.0, 1.0);
    color = vec4(color.rgb * alpha.r, alpha.r);
  }

  if (AlphaOnly != 0) {
    color = color.a * outputColor;
  } else {
    color = color * outputColor.a;
  }

  float maskAlpha = 1.0;
#if HAS_MASK_TEXTURE
  highp vec3 maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
  maskAlpha = texture(MaskTextureSampler, maskCoord.xy).r;
#endif

  float localMaskAlpha = 1.0;
#if HAS_LOCAL_MASK
  highp vec2 localMaskCoord = TransformedCoords_1.xy / TransformedCoords_1.z;
  localMaskAlpha = texture(LocalMaskSampler, localMaskCoord).a;
#endif
  float totalCoverage = vCoverage * maskAlpha * localMaskAlpha * aaRectClipCoverage();

#if HAS_XP
  fragColor = applyPorterDuffXP(color, vec4(totalCoverage));
#else
  fragColor = color * totalCoverage;
#endif
#include "output_swizzle.inc"
}
