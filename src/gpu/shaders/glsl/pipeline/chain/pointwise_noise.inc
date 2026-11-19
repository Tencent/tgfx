// Perlin noise source module (OP_PERLIN_NOISE), used by the noise resource kind (RESOURCE_KIND 2)
// of the chain interpreter. The block members and the coordinate arrive as explicit arguments —
// one spelling shared by the interpreter's variant text, the GL assembly and the Vulkan body —
// while the leaf samplers stay file-scope globals: every consumer declares them at the same
// bindings, and modeOctaves.w picks the pair that holds the permutation and gradient lookups.
#ifndef TGSL_POINTWISE_NOISE_INCLUDED
#define TGSL_POINTWISE_NOISE_INCLUDED

#include "source/perlin_noise_math.inc"

// Evaluates one noise source: the layout math and the octaves loop live in the shared noise
// module, byte-matching the runtime kernel.
vec4 evalNoiseSlot(vec3 sourceCoords, vec4 frequencyStitch, ivec4 modeOctaves) {
  highp vec2 coord = sourceCoords.xy / sourceCoords.z;
  if (modeOctaves.w == 0) {
    return aotPerlinNoise(coord, frequencyStitch.xy, frequencyStitch.zw, modeOctaves.x,
                          modeOctaves.y, modeOctaves.z, TextureSampler_0, TextureSampler_1);
  }
  if (modeOctaves.w == 1) {
    return aotPerlinNoise(coord, frequencyStitch.xy, frequencyStitch.zw, modeOctaves.x,
                          modeOctaves.y, modeOctaves.z, TextureSampler_1, TextureSampler_2);
  }
  return aotPerlinNoise(coord, frequencyStitch.xy, frequencyStitch.zw, modeOctaves.x,
                        modeOctaves.y, modeOctaves.z, TextureSampler_2, TextureSampler_3);
}

#endif  // TGSL_POINTWISE_NOISE_INCLUDED
