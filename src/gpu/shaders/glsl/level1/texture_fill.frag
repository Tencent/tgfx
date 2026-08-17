// TextureFillShader fragment shader (non-YUV path only)
// Processor layout: DefaultGeometryProcessor() + TextureEffect() + [coverage FP] + EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (injected by build tool as #define 0/1):
//   HAS_XP, HAS_DEVICE_MASK (AARect clip is a runtime uniform)
// ALPHA_ONLY and HAS_RGBAAA are runtime uniforms (AlphaOnly / HasRgbaaa), not compile-time
// permutations: both are pure fragment math (alpha-only replicates .r; RGBAAA adds one
// coherent-branch alpha sample), so folding them into uniform branches shrinks the variant count.
// This mirrors QuadTextureFillShader.
// Note: HAS_YUV is always 0 at runtime — YUV textures fall back to ProgramBuilder.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#define HAS_RUNTIME_CLIP 1
#ifndef HAS_DEVICE_MASK
#define HAS_DEVICE_MASK 0
#endif
// 0 = TwoD, 1 = Rect (desktop GL images). Rect variants compile only into the opengl bundle.
#ifndef TEXTURE_KIND
#define TEXTURE_KIND 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  // Always declared: a draw without a subset uploads the full texture bounds, making the clamp a
  // no-op, so subset handling needs no compile-time dimension.
  vec4 Subset;
  // RGBAAA dual-plane alpha offset: always declared, only read when HasRgbaaa != 0 (uniform branch).
  vec2 AlphaStart;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
  int AlphaOnly;
  int HasRgbaaa;
};

layout(location = 0) in vec2 TransformedCoords_0;

#if TEXTURE_KIND == 1
layout(set = 1, binding = 0) uniform sampler2DRect TextureSampler_0;
#else
layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;
#endif

#if HAS_DEVICE_MASK
layout(set = 1, binding = 1) uniform sampler2D MaskTextureSampler;
  #define XP_DST_TEX_BINDING 2
#else
  #define XP_DST_TEX_BINDING 1

  // Decoy write target for the coverage mask FP (see GLSLDeviceSpaceTextureEffect::onSetData);
  // never read — the mask is sampled unclamped.
  vec4 DeviceMaskSubset;
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 outputColor = Color;
  highp vec2 texCoord = TransformedCoords_0;
  highp vec2 finalCoord = texCoord;

  finalCoord = clamp(finalCoord, Subset.xy, Subset.zw);

  vec4 color = texture(TextureSampler_0, finalCoord);

  if (AlphaOnly != 0) {
    // Alpha-only textures use R8 format in Metal. The sampler returns (r, 0, 0, 1).
    // Use .r (replicated to all channels) to get the actual alpha value.
    color = vec4(color.r);
  }

  if (HasRgbaaa != 0) {
    color = clamp(color, 0.0, 1.0);
    highp vec2 alphaCoord = finalCoord + AlphaStart;
    vec4 alpha = texture(TextureSampler_0, alphaCoord);
    alpha = clamp(alpha, 0.0, 1.0);
    color = vec4(color.rgb * alpha.r, alpha.r);
  }

  // Post-processing: alpha multiply
  if (AlphaOnly != 0) {
    color = color.a * outputColor;
  } else {
    color = color * outputColor.a;
  }

#define TGFX_COVERAGE_SRC_COLOR color
#include "coverage_output.inc"
#include "xp_output.inc"
}
