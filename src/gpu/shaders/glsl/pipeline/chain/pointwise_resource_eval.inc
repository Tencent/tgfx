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
#include "pipeline/chain/pointwise_gradient.inc"

bool evalResourceSlot(ivec4 Packed, vec4 in0, int /*dataAt*/, out vec4 value) {
  return evalGradientSlot(Packed, in0, value);
}
#elif RESOURCE_KIND == 2
#if NTEX > 0
#include "pipeline/chain/pointwise_tiled2.inc"
#endif
#include "pipeline/chain/pointwise_noise.inc"

bool evalResourceSlot(ivec4 Packed, vec4 in0, int dataAt, out vec4 value) {
  return evalNoiseSlot(Packed, dataAt, value);
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
