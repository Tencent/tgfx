// PointwiseChainShader: single-pass pointwise chains and graphs. RESOURCE_KIND selects the
// resource profile (PointwiseChainShader::FragDims, AOTResourceProfile):
//   0 = plain chain: no resource module, two-recipe tiling (16 slots)
//   1 = TiledGraph: gradient source, three-recipe tiling (16 slots)
//   2 = NoiseGraph: Perlin noise source, two-recipe tiling (32 slots)
// Everything else is the shared pointwise core (see pointwise_fragment_config.inc).
#version 450

#ifndef RESOURCE_KIND
#define RESOURCE_KIND 0
#endif
#if RESOURCE_KIND == 2
#define CHAIN_SLOT_CAPACITY 32
#else
#define CHAIN_SLOT_CAPACITY 16
#endif

#include "pointwise_fragment_config.inc"

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#include "pointwise_fragment_uniforms.inc"
#if RESOURCE_KIND == 1
#include "pointwise_gradient_uniforms.inc"
#include "pointwise_tiled3_uniforms.inc"
#elif RESOURCE_KIND == 2
#include "pointwise_noise_uniforms.inc"
#include "pointwise_tiled2_uniforms.inc"
#else
#include "pointwise_tiled2_uniforms.inc"
#endif
#include "xp_uniforms.inc"
};

#include "pointwise_fragment_interface.inc"

#if RESOURCE_KIND == 1
#if NTEX > 0
#include "pointwise_tiled3.inc"
#endif
#include "pointwise_gradient.inc"

bool evalResourceSlot(ivec4 Packed, vec4 in0, out vec4 value) {
  return evalGradientSlot(Packed, in0, value);
}
#elif RESOURCE_KIND == 2
#if NTEX > 0
#include "pointwise_tiled2.inc"
#endif
#include "pointwise_noise.inc"

bool evalResourceSlot(ivec4 Packed, vec4 in0, out vec4 value) {
  return evalNoiseSlot(Packed, value);
}
#else
#if NTEX > 0
#include "pointwise_tiled2.inc"
#endif

bool evalResourceSlot(ivec4 Packed, vec4 in0, out vec4 value) {
  value = vec4(0.0);
  return false;
}
#endif

#include "pointwise_fragment_main.inc"
