// Fragment stage of the pointwise kernels (PointwiseChainShader, PointwiseGraphShader): shared
// core, part 1 of 3, permutation defaults and the texture-leaf count.
//
// A kernel entry composes its fragment stage from the shared core and the resource modules it
// supports, in this order:
//   pointwise_fragment_config.inc          permutation defaults (this file)
//   uniform FragmentUniformBlock {
//     pointwise_fragment_uniforms.inc      core fields
//     <module>_uniforms.inc                fields of each resource module
//     xp_uniforms.inc
//   };
//   pointwise_fragment_interface.inc       varyings, samplers, blend and tiled-sampling helpers
//   <module>.inc                           functions of each resource module
//   evalResourceSlot()                     the entry's dispatcher to its resource ops
//   pointwise_fragment_main.inc            leaf fetch, slot evaluation and main()
// Resource modules:
//   pointwise_tiled2, pointwise_tiled3   shader-side leaf tiling with two scalar recipes or three
//                                        array recipes; every kernel includes exactly one
//   pointwise_gradient                   OP_GRADIENT source
//   pointwise_noise                      OP_PERLIN_NOISE source
// The core never tests which modules are present: a tiled module provides
// chainLoadTiledRecipe(), and the entry's evalResourceSlot() names the source ops it supports.
//
// Evaluates an arbitrary pointwise DAG in one pass. The DAG shape is runtime uniform data:
// CHAIN_SLOT_CAPACITY instruction slots (16 for Chain, 32 for Graph) each carry a Packed ivec4
// (op, two input registers, selector with the result register in bits 20-24), so topologies with
// the same texture-leaf count share a variant. Intermediate results live in 16 recycled registers
// (AOTChainRegisterAllocator), independent of the instruction count. Texture leaves occupy
// instructions 0..NTEX-1 and each samples its own sampler, keeping sampler indexing static.
// TEXTURE_COUNT encodes 0/1/2/4 leaves; a zero-leaf chain evaluates const-color and blend ops
// against the geometry color alone. With HAS_MASK_TEXTURE a device-space alpha mask child is
// sampled after the DAG and before the XP stage.
// Leaf subset rects are runtime uniforms (Subset / Subset_1 / ... following the structural ordinal
// convention); a leaf without a real subset uploads the full texture bounds, so the clamp is a
// no-op. ColorSpaceXform parameters are one shared chain-wide block, so a chain may contain at
// most one color-space op (enforced by the matcher).

#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_COLOR
#define HAS_COLOR 0
#endif
#ifndef GP_LAYOUT
#define GP_LAYOUT 0
#endif
#ifndef HAS_MASK_TEXTURE
#define HAS_MASK_TEXTURE 0
#endif
#ifndef TEXTURE_COUNT
#define TEXTURE_COUNT 0
#endif
#ifndef HAS_SUBSET
#define HAS_SUBSET 0
#endif
#define CHAIN_LEAF_SAMPLER sampler2D

#if TEXTURE_COUNT == 0
#define NTEX 0
#else
#define NTEX 4
#endif
