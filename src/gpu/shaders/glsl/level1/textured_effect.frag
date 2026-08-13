// TexturedEffectShader fragment shader
// 冷结构类 uber for the (S1, none, {noXP,XP}, RGBA) class: samples one intermediate texture then
// applies exactly one pointwise operator, selected at runtime by the OpType uniform. Merges the
// former TexturedColorMatrix / TexturedLuma / TexturedAlphaThreshold / TexturedColorSpaceXform
// shaders (identical skeleton, only the operator differed) into a single precompiled shader.
// Processor layout: ComposeFragmentProcessor(TextureEffect, <pointwise op>) OR the sibling form
//                   [TextureEffect, <pointwise op>].
// Permutation dimensions (frag): HAS_XP (int, 3), HAS_COVERAGE (bool)
// Runtime uniform OpType selects the operator (set by the pointwise FP's onSetData):
//   0 = ColorMatrix, 1 = Luma, 2 = AlphaThreshold, 3 = ColorSpaceXform
// Subset is always declared and clamped; a draw without one uploads the full texture bounds.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  vec4 Subset;
#include "pointwise_op_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;
#if HAS_COVERAGE
layout(location = 1) in float vCoverage;
#endif

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

#include "pointwise_op.inc"

void main() {
  vec4 outputColor = Color;
  highp vec2 finalCoord = TransformedCoords_0;
  finalCoord = clamp(finalCoord, Subset.xy, Subset.zw);

  vec4 color = texture(TextureSampler_0, finalCoord);
  // TextureEffect post-processing: intermediate is never alpha-only or RGBAAA.
  color = color * outputColor.a;

  vec4 result = applyPointwiseOp(0, color);

#if HAS_COVERAGE
// Per-vertex AA coverage: keep the un-premultiplied result for the XferProcessor lerp and multiply
// coverage into the non-XP output (matches the coverage contract in xp_output.inc).
#define TGFX_XP_SRC_COLOR (result * vCoverage)
#define TGFX_XP_SRC_UNPREMUL result
#define TGFX_XP_COVERAGE vec4(vCoverage)
#else
#define TGFX_XP_SRC_COLOR result
#endif
#include "xp_output.inc"
}
