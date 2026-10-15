// PerlinNoiseFillShader fragment shader
// Ported from GLSLPerlinNoiseFragmentProcessor::emitCode. The runtime path bakes noiseType, octave
// count and tile stitching into generated code; here they are runtime uniforms (NoiseType,
// NumOctaves, StitchTiles) so one shader covers all of them:
//   - the octave loop runs a uniform number of iterations (NumOctaves, clamped [1,255]);
//   - NoiseType (0=FractalNoise, 1=Turbulence) and StitchTiles are uniform conditionals.
// Two per-draw lookup textures are bound as samplers: TextureSampler_0 = permutations (256x1 A8,
// ForRead swizzle RRRR so the single stored channel reads through .r) and TextureSampler_1 = noise
// (256x4 RGBA8888, identity swizzle). Output is premultiplied RGBA.
// Up to three runtime-selected pointwise operators follow the noise in the same pass.
// Permutation dimensions (frag): HAS_XP (int, 3), HAS_COVERAGE (bool).
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

#define NOISE_FRACTAL 0

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec2 baseFrequency;
  vec2 stitchData;
  int NoiseType;
  int NumOctaves;
  int StitchTiles;
  int PointwiseSlotCount;

#define TGFX_SLOT_ARRAY_SUFFIX [3]
#include "pointwise_op_uniforms.inc"
#undef TGFX_SLOT_ARRAY_SUFFIX

#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;
#if HAS_COVERAGE
layout(location = 1) in float vCoverage;
#endif

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;  // permutations (A8)
layout(set = 1, binding = 1) uniform sampler2D TextureSampler_1;  // noise (RGBA)

// The two noise lookup samplers occupy bindings 0 and 1, so the XferProcessor dst texture (DST_TEX
// mode) lands on binding 2.
#define XP_DST_TEX_BINDING 2
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "xp_blend_pass.inc"

layout(location = 0) out vec4 fragColor;

#include "pointwise_slot_array_bind.inc"
#include "pointwise_op.inc"
#include "pointwise_slot_array_unbind.inc"
#include "pointwise_slot_loop.inc"

vec4 aotPerlinPermutation(vec2 coord) {
  return texture(TextureSampler_0, coord);
}

vec4 aotPerlinGradient(vec2 coord) {
  return texture(TextureSampler_1, coord);
}

#include "perlin_noise_math.inc"

void main() {
  vec4 result = aotPerlinNoise(TransformedCoords_0, baseFrequency, stitchData,
                               NoiseType, NumOctaves, StitchTiles);
#if HAS_XP
  // The XferProcessor's advanced blend runs as the tail's last pass (xp_blend_pass.inc).
  vec4 xpDst = xpReadDstColor();
  vec4 xpBlended;
  result = applyPointwiseSlots(result, xpDst, xpBlended);
#define TGFX_XP_DST_COLOR xpDst
#define TGFX_XP_BLENDED xpBlended
#else
  result = applyPointwiseSlots(result);
#endif

#if HAS_COVERAGE
#define TGFX_XP_SRC_COLOR (result * vCoverage)
#define TGFX_XP_SRC_UNPREMUL result
#define TGFX_XP_COVERAGE vec4(vCoverage)
#else
#define TGFX_XP_SRC_COLOR result
#endif
#include "xp_output.inc"
}
