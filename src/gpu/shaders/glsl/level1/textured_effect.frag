// TexturedEffectShader fragment entry: one texture followed by exactly one pointwise operator,
// selected at runtime by the OpType uniform (0 = ColorMatrix, 1 = Luma, 2 = AlphaThreshold,
// 3 = ColorSpaceXform, set by the pointwise FP's onSetData).
// Processor layout: ComposeFragmentProcessor(TextureEffect, <pointwise op>) OR the sibling form
//                   [TextureEffect, <pointwise op>].
// The pipeline is the shared dedicated one (fill_fragment_main.inc); this file declares the
// family's interface and selects the steps. Subset is always declared and clamped; a draw without
// one uploads the full texture bounds. The coverage source is the vCoverage varying (a constant
// 1.0 for non-AA path triangles, so the modulation is the identity there).
// Fragment dimensions: HAS_XP (int, 3).
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = TGFX_FRAGMENT_UBO_SET, binding = TGFX_FRAGMENT_UBO_BINDING) uniform FragmentUniformBlock {
  vec4 Color;
  vec4 Subset;
#include "pointwise_op_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;
layout(location = 1) in float vCoverage;

layout(set = TGFX_TEXTURE_SET, binding = 0) uniform sampler2D TextureSampler_0;

#define XP_DST_TEX_BINDING 1
#define FILL_SOURCE FILL_SOURCE_CLAMPED_TEXTURE
#define FILL_OPERATOR 1
#define FILL_COVERAGE FILL_COVERAGE_VERTEX
#include "fill_fragment_main.inc"
