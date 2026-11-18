// Perlin noise source module (OP_PERLIN_NOISE), used by the noise resource kind (RESOURCE_KIND 2)
// of the chain interpreter. Pairs with the shared UBO declaration. NoiseModeOctaves.w selects
// which leaf samplers hold the permutation and gradient lookup textures.
#ifndef TGSL_POINTWISE_NOISE_INCLUDED
#define TGSL_POINTWISE_NOISE_INCLUDED

#include "pipeline/chain/pointwise_source_coords.inc"

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

#include "source/perlin_noise_math.inc"

// Evaluates one noise source from its named parameter (the Chain module's
// Noise_frequencyStitch member) and the shared octaves: no pool, no compare, no capacity macro.
vec4 evalNoiseSlot() {
  vec4 frequencyStitch = Noise_frequencyStitch;
  highp vec2 coord = SourceCoords.xy / SourceCoords.z;
  return aotPerlinNoise(coord, frequencyStitch.xy, frequencyStitch.zw, NoiseModeOctaves.x,
                        NoiseModeOctaves.y, NoiseModeOctaves.z);
}


#endif  // TGSL_POINTWISE_NOISE_INCLUDED
