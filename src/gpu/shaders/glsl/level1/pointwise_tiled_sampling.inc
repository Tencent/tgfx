// Shared tiled-sampling state and helpers of the pointwise kernels. The tiling module the kernel
// includes (pointwise_tiled2.inc or pointwise_tiled3.inc) loads a leaf's recipe into the
// chainTiled* state through chainLoadTiledRecipe(); chainLeafFetch (pointwise_fragment_main.inc)
// then samples with it.

// The recipe of the tiled leaf being fetched. chainLeafFetch loads it from the first (Tiled*) or
// second (Tiled*2) uniform set before calling the helpers, so the coordinate mapping, the seam
// blend and the border post-pass all read the same leaf's recipe.
int chainTiledModeX;
int chainTiledModeY;
highp vec4 chainTiledSubset;
highp vec4 chainTiledClamp;
highp vec2 chainTiledDimension;
// Rebind the shared tiled-sampling helpers to the current recipe; the include expects the
// standalone names used by the single-texture kernels.
#define ShaderModeX chainTiledModeX
#define ShaderModeY chainTiledModeY
#define Subset chainTiledSubset
#define Clamp chainTiledClamp
#define Dimension chainTiledDimension
#include "tiled_sample.inc"
// RepeatLinearNone(3) seam blend, ported from the runtime GLSLTiledTextureEffect emission (and
// the blur kernel's per-tap copy): a wrapped coordinate that clamps means the linear footprint
// crosses the subset edge, so blend with a sample at the opposite clamp edge (diagonal read when
// both axes clamp). Mode 3 uses pixel-space coordinates, so the repeat reads scale by Dimension
// exactly like tiledMapCoord's returned sample coord. Without this, every tile edge on the chain
// route snaps to the clamped edge texel while the runtime blends (the chain-side
// shape needs a subset-bearing tiled leaf, which the hardware wrap never produces).
#include "texture_read_swizzle.inc"

vec4 aotChainRead(CHAIN_LEAF_SAMPLER texSampler, vec2 coord, int readSwizzle) {
  return aotReadSwizzle(texture(texSampler, coord), readSwizzle);
}

vec4 tiledSeamBlend(CHAIN_LEAF_SAMPLER texSampler, vec4 texColor, vec2 subsetCoord,
                    vec2 clampedCoord, int readSwizzle) {
  bool repeatX = ShaderModeX == 3 && subsetCoord.x != clampedCoord.x;
  bool repeatY = ShaderModeY == 3 && subsetCoord.y != clampedCoord.y;
  if (!repeatX && !repeatY) {
    return texColor;
  }
  float errX = subsetCoord.x - clampedCoord.x;
  float errY = subsetCoord.y - clampedCoord.y;
  float repeatCoordX = errX > 0.0 ? Clamp.x : Clamp.z;
  float repeatCoordY = errY > 0.0 ? Clamp.y : Clamp.w;
  if (repeatX && repeatY) {
    vec4 repeatReadX = aotChainRead(texSampler, vec2(repeatCoordX, clampedCoord.y) * Dimension,
                                    readSwizzle);
    vec4 repeatReadY = aotChainRead(texSampler, vec2(clampedCoord.x, repeatCoordY) * Dimension, readSwizzle);
    vec4 repeatReadXY =
        aotChainRead(texSampler, vec2(repeatCoordX, repeatCoordY) * Dimension, readSwizzle);
    return mix(mix(texColor, repeatReadX, abs(errX)), mix(repeatReadY, repeatReadXY, abs(errX)),
               abs(errY));
  }
  if (repeatX) {
    vec4 repeatReadX = aotChainRead(texSampler, vec2(repeatCoordX, clampedCoord.y) * Dimension,
                                    readSwizzle);
    return mix(texColor, repeatReadX, errX);
  }
  vec4 repeatReadY = aotChainRead(texSampler, vec2(clampedCoord.x, repeatCoordY) * Dimension, readSwizzle);
  return mix(texColor, repeatReadY, errY);
}
#undef ShaderModeX
#undef ShaderModeY
#undef Subset
#undef Clamp
#undef Dimension
