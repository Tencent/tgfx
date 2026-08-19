// GaussianBlur1DShader fragment shader
// MAX_SIGMA is a FIXED compile-time constant (max supported kernel), NOT a permutation dimension.
// The loop upper bound is 4*(MAX_SIGMA+1); the actual radius comes from the Sigma uniform at runtime
// and the loop breaks early once reached. Sigma being a uniform (not a variant) keeps the variant
// count bounded — it previously multiplied the fragment domain by 10.
// Permutation dimensions (frag):
//   HAS_XP (0~2): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF
//   HAS_TILED_CHILD (bool): When 1, the child is a TiledTextureEffect: each tap is tiled through the
//                           shared tiled_sample.inc logic (mirrors the runtime per-tap tiling).
//   Child subset clamping is always on: the kernel always clamps each tap to Subset, and a child
//   without a real subset uploads the full texture bounds, so the clamp is a no-op.
#version 450

// Fixed maximum kernel: maxSigma=10 → loop upper bound 4*(9+1)=40. Runtime Sigma <= 10 breaks early.
#ifndef MAX_SIGMA
#define MAX_SIGMA 9
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#define HAS_RUNTIME_CLIP 1
#define HAS_RUNTIME_DEVICE_MASK 1
#ifndef HAS_TILED_CHILD
#define HAS_TILED_CHILD 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  float Sigma;
  vec2 Step;
  // Always declared: plain-child taps clamp to it (no-op at full bounds); a tiled child uses it
  // as the tiling domain.
  vec4 Subset;
  // The tiled-child fields are always declared: a plain child never reads them (the runtime
  // TiledChild branch is not taken), so they need no compile-time dimension.
  int ShaderModeX;
  int ShaderModeY;
  vec4 Clamp;
  vec2 Dimension;
  int TiledChild;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

// Always bound: an absent device mask is padded with the shared dummy texture and HasDeviceMask
// is 0.
layout(set = 1, binding = 1) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING 2
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "tiled_sample.inc"

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
    vec4 texColor = vec4(0.0);
    if (TiledChild != 0) {
    // The child is a TiledTextureEffect: tile each tap exactly as the runtime does per sample.
    vec2 tapInCoord;
    vec2 tapSubsetCoord;
    vec2 tapClampedCoord;
    vec2 tapCoord = tiledMapCoord(sampleCoord, false, tapInCoord, tapSubsetCoord, tapClampedCoord);
    texColor = texture(TextureSampler_0, tapCoord);
    // RepeatLinearNone(3) seam blend, ported from the runtime GLSLTiledTextureEffect emission: a
    // wrapped coordinate that clamps means the linear footprint crosses the subset edge, so blend
    // with a sample at the opposite clamp edge (diagonal read when both axes clamp). The signed
    // single-axis weights intentionally match the runtime. Mode 3 uses pixel-space coordinates,
    // so the repeat reads scale by Dimension like tiledMapCoord's returned sample coord does.
    bool repeatX = ShaderModeX == 3 && tapSubsetCoord.x != tapClampedCoord.x;
    bool repeatY = ShaderModeY == 3 && tapSubsetCoord.y != tapClampedCoord.y;
    if (repeatX || repeatY) {
      float errX = tapSubsetCoord.x - tapClampedCoord.x;
      float errY = tapSubsetCoord.y - tapClampedCoord.y;
      float repeatCoordX = errX > 0.0 ? Clamp.x : Clamp.z;
      float repeatCoordY = errY > 0.0 ? Clamp.y : Clamp.w;
      if (repeatX && repeatY) {
        vec4 repeatReadX =
            texture(TextureSampler_0, vec2(repeatCoordX, tapClampedCoord.y) * Dimension);
        vec4 repeatReadY =
            texture(TextureSampler_0, vec2(tapClampedCoord.x, repeatCoordY) * Dimension);
        vec4 repeatReadXY = texture(TextureSampler_0, vec2(repeatCoordX, repeatCoordY) * Dimension);
        texColor = mix(mix(texColor, repeatReadX, abs(errX)),
                       mix(repeatReadY, repeatReadXY, abs(errX)), abs(errY));
      } else if (repeatX) {
        vec4 repeatReadX =
            texture(TextureSampler_0, vec2(repeatCoordX, tapClampedCoord.y) * Dimension);
        texColor = mix(texColor, repeatReadX, errX);
      } else {
        vec4 repeatReadY =
            texture(TextureSampler_0, vec2(tapClampedCoord.x, repeatCoordY) * Dimension);
        texColor = mix(texColor, repeatReadY, errY);
      }
    }
    texColor = tiledApplyBorder(texColor, tapInCoord, tapSubsetCoord, tapClampedCoord);
    } else {
      sampleCoord = clamp(sampleCoord, Subset.xy, Subset.zw);
      texColor = texture(TextureSampler_0, sampleCoord);
    }
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
