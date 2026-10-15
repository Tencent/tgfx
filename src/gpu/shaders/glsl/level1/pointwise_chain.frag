// PointwiseChainShader: ordinary pointwise chains. The fragment stage is the shared pointwise core
// with the two-recipe tiling module and no resource ops (see pointwise_fragment_config.inc).
#version 450
#define CHAIN_SLOT_CAPACITY 16

#include "pointwise_fragment_config.inc"

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#include "pointwise_fragment_uniforms.inc"
#include "pointwise_tiled2_uniforms.inc"
#include "xp_uniforms.inc"
};

#include "pointwise_fragment_interface.inc"
#include "pointwise_tiled2.inc"

bool evalResourceSlot(ivec4 Packed, vec4 in0, out vec4 value) {
  value = vec4(0.0);
  return false;
}

#include "pointwise_fragment_main.inc"
