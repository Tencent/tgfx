// TiledTextureFillShader fragment entry: QuadPerEdgeAAGeometryProcessor + TiledTextureEffect +
// EmptyXferProcessor/PorterDuffXP. The tiling math lives in tiled_full_sample.inc (shared with the
// blur kernel through tiled_sample.inc) and mirrors the runtime GLSLTiledTextureEffect codegen;
// the pipeline is the shared dedicated one (fill_fragment_main.inc). ShaderModeX/ShaderModeY are
// runtime uniforms: all nine modes are supported here. The coverage source is the vCoverage
// varying (a constant 1.0 for non-AA path triangles and non-AA quads, so the modulation is the
// identity there).
// Fragment dimensions: HAS_XP (int, 3).
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(location = 0) in vec2 TransformedCoords_0;
layout(location = 1) in float vCoverage;

layout(set = TGFX_TEXTURE_SET, binding = 0) uniform sampler2D TextureSampler_0;

layout(std140, set = TGFX_FRAGMENT_UBO_SET, binding = TGFX_FRAGMENT_UBO_BINDING) uniform FragmentUniformBlock {
  vec4 Color;
  int ShaderModeX;
  int ShaderModeY;
  vec4 Subset;
  vec4 Clamp;
  vec2 Dimension;
#include "xp_uniforms.inc"
  // Alpha-only source (R8 texture) and strict-subset clamping are runtime uniforms rather than
  // compile-time permutations; both are pure fragment math (coherent branches).
  int AlphaOnly;
  int Strict;
  int ReadSwizzle;
};

#define XP_DST_TEX_BINDING 1

#define FILL_SOURCE FILL_SOURCE_TILED_FULL
#define FILL_COVERAGE FILL_COVERAGE_VERTEX
#include "fill_fragment_main.inc"
