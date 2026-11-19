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
  chainLutLeaf = GradientLUTLeaf;
#endif
  value = evalGradientSlot(
      in0, SourceCoords, GradientLayoutType, GradientColorizerKind, chainLutLeaf,
      Gradient_leftBorder, Gradient_rightBorder, Gradient_start, Gradient_end, Gradient_scale01,
      Gradient_bias01, Gradient_scale23, Gradient_bias23, Gradient_threshold, Gradient_bias,
      Gradient_scale, Gradient_thresholds1_7, Gradient_thresholds9_13, Gradient_scales[0],
      Gradient_biases[0], Gradient_scales[1], Gradient_biases[1], Gradient_scales[2],
      Gradient_biases[2], Gradient_scales[3], Gradient_biases[3], Gradient_scales[4],
      Gradient_biases[4], Gradient_scales[5], Gradient_biases[5], Gradient_scales[6],
      Gradient_biases[6], Gradient_scales[7], Gradient_biases[7]);
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
  value = evalNoiseSlot(SourceCoords, Noise_frequencyStitch, NoiseModeOctaves);
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
