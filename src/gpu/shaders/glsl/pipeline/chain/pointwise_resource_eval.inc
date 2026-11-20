// The interpreter's resource modules and its slot dispatcher. The entry declares the uniform
// fields of the modules it selects (see the declaration); this file carries the functions
// and the one dispatcher the evaluation loop calls. The three resource kinds never coexist in one
// variant: RESOURCE_KIND is a compile-time dimension.

#ifndef TGSL_POINTWISE_RESOURCE_EVAL_INCLUDED
#define TGSL_POINTWISE_RESOURCE_EVAL_INCLUDED

#if RESOURCE_KIND == 1
#if NTEX > 0
#include "pipeline/chain/pointwise_tiled3.inc"
#endif
#include "pipeline/chain/pointwise_source_coords.inc"
#include "pipeline/chain/pointwise_gradient.inc"

bool evalResourceSlot(ivec4 Packed, vec4 in0, int /*dataAt*/, out vec4 value) {
  if (Packed.x != OP_GRADIENT) {
    value = vec4(0.0);
    return false;
  }
  int chainLutLeaf = 0;
#if NTEX > 0
  chainLutLeaf = GradientLutLeaf;
#endif
  value = evalGradientSlot(
      in0, SourceCoords, GradientLayoutType, GradientColorizerKind, chainLutLeaf,
      GradientLeftBorder, GradientRightBorder, GradientStart, GradientEnd, GradientScale01,
      GradientBias01, GradientScale23, GradientBias23, GradientThreshold, GradientBias,
      GradientScale, GradientThresholds1_7, GradientThresholds9_13, GradientScales, GradientBiases);
  return true;
}
#elif RESOURCE_KIND == 2
#if NTEX > 0
#include "pipeline/chain/pointwise_tiled2.inc"
#endif
#include "pipeline/chain/pointwise_source_coords.inc"
#include "pipeline/chain/pointwise_noise.inc"

bool evalResourceSlot(ivec4 Packed, vec4 in0, int dataAt, out vec4 value) {
  if (Packed.x != OP_PERLIN_NOISE) {
    value = vec4(0.0);
    return false;
  }
  value = evalNoiseSlot(SourceCoords, NoiseFrequencyStitch, NoiseModeOctaves, TextureSampler_0,
                        TextureSampler_1, TextureSampler_2, TextureSampler_3);
  return true;
}
#else
#if NTEX > 0
#include "pipeline/chain/pointwise_tiled2.inc"
#endif

bool evalResourceSlot(ivec4 Packed, vec4 in0, int /*dataAt*/, out vec4 value) {
  value = vec4(0.0);
  return false;
}
#endif


#endif  // TGSL_POINTWISE_RESOURCE_EVAL_INCLUDED
