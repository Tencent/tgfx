// Perlin noise source module (OP_PERLIN_NOISE), used by the NoiseGraph profile of
// PointwiseGraphShader. Pairs with pointwise_noise_uniforms.inc. NoiseModeOctaves.w selects which
// leaf samplers hold the permutation and gradient lookup textures.
#include "pointwise_chain_ops.inc"
#include "pointwise_source_coords.inc"

vec4 aotPerlinPermutation(vec2 coord) {
  if (NoiseModeOctaves.w == 0) {
    return texture(TextureSampler_0, coord);
  }
  if (NoiseModeOctaves.w == 1) {
    return texture(TextureSampler_1, coord);
  }
  return texture(TextureSampler_2, coord);
}

vec4 aotPerlinGradient(vec2 coord) {
  if (NoiseModeOctaves.w == 0) {
    return texture(TextureSampler_1, coord);
  }
  if (NoiseModeOctaves.w == 1) {
    return texture(TextureSampler_2, coord);
  }
  return texture(TextureSampler_3, coord);
}

#include "perlin_noise_math.inc"

// Evaluates an OP_PERLIN_NOISE slot; returns false for every other op.
bool evalNoiseSlot(ivec4 Packed, out vec4 value) {
  value = vec4(0.0);
  if (Packed.x != OP_PERLIN_NOISE) {
    return false;
  }
  highp vec2 coord = GradientCoords.xy / GradientCoords.z;
  value = aotPerlinNoise(coord, NoiseFrequencyStitch.xy, NoiseFrequencyStitch.zw,
                         NoiseModeOctaves.x, NoiseModeOctaves.y, NoiseModeOctaves.z);
  return true;
}
