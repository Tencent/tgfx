// Perlin noise source module (OP_PERLIN_NOISE), used by the noise resource kind (RESOURCE_KIND 2)
// of the chain interpreter. The block members and the coordinate arrive as explicit arguments —
// one spelling shared by the interpreter's variant text, the GL assembly and the Vulkan body —
// while the leaf samplers stay file-scope globals: every consumer declares them at the same
// bindings, and modeOctaves.w picks the pair that holds the permutation and gradient lookups.
#ifndef TGSL_POINTWISE_NOISE_INCLUDED
#define TGSL_POINTWISE_NOISE_INCLUDED

#include "source/perlin_noise_math.inc"
#include "fn/noise/sample_0.inc"
#include "fn/noise/sample_1.inc"
#include "fn/noise/sample_2.inc"

// Evaluates one noise source: the layout math and the octaves loop live in the shared noise
// module, byte-matching the runtime kernel.
#include "fn/entry/noise.inc"


#endif  // TGSL_POINTWISE_NOISE_INCLUDED
