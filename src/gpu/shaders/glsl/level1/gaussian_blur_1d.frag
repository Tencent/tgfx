// GaussianBlur1DShader fragment shader
// MAX_SIGMA is a FIXED compile-time constant (max supported kernel), NOT a permutation dimension.
// The loop upper bound is 4*(MAX_SIGMA+1); the actual radius comes from the Sigma uniform at runtime
// and the loop breaks early once reached. Sigma being a uniform (not a variant) keeps the variant
// count bounded — it previously multiplied the fragment domain by 10.
// Permutation dimensions (frag):
//   HAS_XP (0~2): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF
//   HAS_CHILD_SUBSET (bool): When 1, clamp each sample coordinate to the child texture's Subset
//                            bounds before sampling. Used when TextureEffect.hasSubset()=true but
//                            GP has no per-vertex subset attribute.
//   HAS_TILED_CHILD (bool): When 1, the child is a TiledTextureEffect: each tap is tiled through the
//                           shared tiled_sample.inc logic (mirrors the runtime per-tap tiling).
//                           Mutually exclusive with HAS_CHILD_SUBSET.
#version 450

// Fixed maximum kernel: maxSigma=10 → loop upper bound 4*(9+1)=40. Runtime Sigma <= 10 breaks early.
#ifndef MAX_SIGMA
#define MAX_SIGMA 9
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_CHILD_SUBSET
#define HAS_CHILD_SUBSET 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_TILED_CHILD
#define HAS_TILED_CHILD 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  float Sigma;
  vec2 Step;
#if HAS_CHILD_SUBSET || HAS_TILED_CHILD
  vec4 Subset;
#endif
#if HAS_TILED_CHILD
  int ShaderModeX;
  int ShaderModeY;
  vec4 Clamp;
  vec2 Dimension;
#endif
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

#if HAS_COVERAGE == 2
layout(set = 1, binding = 1) uniform sampler2D MaskTextureSampler;
  #define XP_DST_TEX_BINDING 2
#else
  #define XP_DST_TEX_BINDING 1
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#if HAS_TILED_CHILD
#include "tiled_sample.inc"
#endif

layout(location = 0) out vec4 fragColor;

void main() {
  float sigma = Sigma;
  vec2 offset = Step;
  int radius = int(ceil(2.0 * sigma));

  vec4 sum = vec4(0.0);
  float total = 0.0;

  for (int j = 0; j <= 4 * (MAX_SIGMA + 1); ++j) {
    int i = j - radius;
    float weight = exp(-float(i * i) / (2.0 * sigma * sigma));
    total += weight;

    vec2 sampleCoord = TransformedCoords_0 + offset * float(i);
#if HAS_TILED_CHILD
    // The child is a TiledTextureEffect: tile each tap exactly as the runtime does per sample.
    vec2 tapInCoord;
    vec2 tapSubsetCoord;
    vec2 tapClampedCoord;
    vec2 tapCoord = tiledMapCoord(sampleCoord, false, tapInCoord, tapSubsetCoord, tapClampedCoord);
    vec4 texColor = texture(TextureSampler_0, tapCoord);
    texColor = tiledApplyBorder(texColor, tapInCoord, tapSubsetCoord, tapClampedCoord);
#else
#if HAS_CHILD_SUBSET
    sampleCoord = clamp(sampleCoord, Subset.xy, Subset.zw);
#endif
    vec4 texColor = texture(TextureSampler_0, sampleCoord);
#endif
    sum += texColor * weight;

    if (i == radius) {
      break;
    }
  }

  vec4 blurResult = sum / total;

#define TGFX_COVERAGE_SRC_COLOR blurResult
#include "coverage_output.inc"
#include "xp_output.inc"
}
