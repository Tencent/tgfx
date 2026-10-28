// NonAARRectFillShader fragment entry: NonAARRectGeometryProcessor + EmptyXferProcessor/PorterDuffXP.
// The pipeline is the shared dedicated one (fill_fragment_main.inc); this file declares the family's
// interface and selects the steps. The shape's hard rounded-rect step coverage (the stroke's inner
// SDF included) is rrect_coverage.inc; the analytic draw clip (AA rect or rounded rect) rides the
// shared runtime clip contract uniforms, so it costs no variant: the draw carries the clip FP only
// for its uniforms. The block is unconditional because the contract fields must always be declared.
// Fragment dimensions: STROKE, TEXTURED, HAS_XP (int, 3). The color source is a runtime select: the
// exact Color uniform when HasCommonColor != 0, the (8-bit quantized) vColor varying otherwise.
#version 450

#ifndef STROKE
#define STROKE 0
#endif
#ifndef TEXTURED
#define TEXTURED 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#define HAS_RUNTIME_CLIP 1

layout(std140, set = TGFX_FRAGMENT_UBO_SET, binding = TGFX_FRAGMENT_UBO_BINDING) uniform FragmentUniformBlock {
  vec4 Color;
  int HasCommonColor;
#if TEXTURED
  // Tiled-sampling recipe, written by GLSLTiledTextureEffect::onSetData (runtime uniforms, same
  // names as TiledTextureFillShader).
  int ShaderModeX;
  int ShaderModeY;
  vec4 Subset;
  vec4 Clamp;
  vec2 Dimension;
  int AlphaOnly;
  int Strict;
#endif
#include "coverage/coverage_uniforms.inc"
#include "xp/xp_uniforms.inc"
};

#if TEXTURED
layout(location = 5) in vec2 TransformedCoords_0;
layout(set = TGFX_TEXTURE_SET, binding = 0) uniform sampler2D TextureSampler_0;
#endif

layout(location = 0) in vec2 vLocalCoord;
layout(location = 1) in vec2 vRadii;
layout(location = 2) in vec4 vRectBounds;
layout(location = 3) in vec4 vColor;
#if STROKE
layout(location = 4) in vec2 vStrokeWidth;
#endif

#if TEXTURED
#define XP_DST_TEX_BINDING 1
#else
#define XP_DST_TEX_BINDING 0
#endif

#define FILL_COLOR FILL_COLOR_VARYING_OR_COMMON
#if TEXTURED
#define FILL_SOURCE FILL_SOURCE_TILED
#endif
#define FILL_COVERAGE FILL_COVERAGE_RRECT
#define FILL_CLIP 1
#include "pipeline/fill/fill_fragment_main.inc"
