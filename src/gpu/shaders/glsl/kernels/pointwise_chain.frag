// PointwiseChainShader: single-pass pointwise chains and graphs. RESOURCE_KIND selects the
// resource profile (PointwiseChainShader::FragDims, AOTResourceProfile):
//   0 = plain chain: no resource module, two-recipe tiling (16 slots)
//   1 = TiledGraph: gradient source, three-recipe tiling (16 slots)
//   2 = NoiseGraph: Perlin noise source, two-recipe tiling (32 slots)
// Everything else is the shared pointwise core (see pointwise_fragment_config.inc).
#version 450

// The capacities are injected by shader_build_tool from the C++ resource profiles
// (AOTResourceProfile.h), so no literal here can drift from what the runtime uploads.
#if RESOURCE_KIND == 2
#define CHAIN_SLOT_CAPACITY TGFX_NOISE_SLOT_CAPACITY
#else
#define CHAIN_SLOT_CAPACITY TGFX_CHAIN_SLOT_CAPACITY
#endif

#include "pipeline/chain/pointwise_fragment_config.inc"

layout(std140, set = TGFX_FRAGMENT_UBO_SET, binding = TGFX_FRAGMENT_UBO_BINDING) uniform FragmentUniformBlock {
#include "pipeline/chain/pointwise_fragment_uniforms.inc"
#if RESOURCE_KIND == 1
#include "pipeline/chain/pointwise_gradient_uniforms.inc"
#include "pipeline/chain/pointwise_tiled3_uniforms.inc"
#elif RESOURCE_KIND == 2
#include "pipeline/chain/pointwise_noise_uniforms.inc"
#include "pipeline/chain/pointwise_tiled2_uniforms.inc"
#else
#include "pipeline/chain/pointwise_tiled2_uniforms.inc"
#endif
#include "xp/xp_uniforms.inc"
};

// The operator parameter layouts and their pool readers, generated from contract/ChainOps.h.
#pragma tgfx_chain_ops

// The specialization include point: shader_build_tool replaces this line with the generated
// SPEC_<field>() accessors (see compile/SpecCodegen). Every structural int field the kernel wants
// the runtime to freeze is read through SPEC_<field>() only, so a specialized compile folds it
// while the generic compile reads the uniform. The pragma never reaches a compiler: every compile
// path consumes the already-replaced text.
#pragma tgfx_spec_include

#include "pipeline/chain/pointwise_fragment_interface.inc"

#if RESOURCE_KIND == 1
#if NTEX > 0
#include "pipeline/chain/pointwise_tiled3.inc"
#endif
#include "pipeline/chain/pointwise_gradient.inc"

bool evalResourceSlot(ivec4 Packed, vec4 in0, out vec4 value) {
  return evalGradientSlot(Packed, in0, value);
}
#elif RESOURCE_KIND == 2
#if NTEX > 0
#include "pipeline/chain/pointwise_tiled2.inc"
#endif
#include "pipeline/chain/pointwise_noise.inc"

bool evalResourceSlot(ivec4 Packed, vec4 in0, out vec4 value) {
  return evalNoiseSlot(Packed, value);
}
#else
#if NTEX > 0
#include "pipeline/chain/pointwise_tiled2.inc"
#endif

bool evalResourceSlot(ivec4 Packed, vec4 in0, out vec4 value) {
  value = vec4(0.0);
  return false;
}
#endif

#include "pipeline/chain/pointwise_fragment_main.inc"
