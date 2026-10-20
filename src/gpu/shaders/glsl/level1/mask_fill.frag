// MaskFillShader fragment entry: DefaultGeometryProcessor + one alpha-only TextureEffect coverage
// FP + XP. The fill color is the DefaultGP Color uniform; the coverage is the alpha-only mask
// sampled at the per-vertex transformed coordinate, and it feeds the XferProcessor's coverage, so
// every blend mode composites correctly. The pipeline is the shared dedicated one
// (fill_fragment_main.inc); this file declares the family's interface and selects the steps.
// Fragment dimensions: HAS_XP (int, 3).
//
// The coverage sampler MUST be named TextureSampler_0: the precompiled path binds a fragment
// processor's texture by its (suffix-stripped) runtime name, which for the first TextureEffect is
// "TextureSampler_0".
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  int OutputAlphaSwizzle;
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

#define XP_DST_TEX_BINDING 1

#define FILL_COVERAGE FILL_COVERAGE_MASK
#define FILL_OUTPUT_SWIZZLE 1
#include "fill_fragment_main.inc"
