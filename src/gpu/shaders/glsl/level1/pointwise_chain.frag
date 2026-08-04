// PointwiseChainShader fragment shader.
// Evaluates an arbitrary pointwise DAG in one pass. The DAG shape is runtime uniform data: 16
// statically expanded slots each carry a Packed ivec4 (op, two input-slot indices, blend/const
// selector), so any topology with the same texture-leaf count shares one program variant. Texture
// leaves occupy slots 0..NTEX-1 and each samples its own sampler, keeping sampler indexing static.
// Leaf subset rects are runtime uniforms (Subset / Subset_1 / ... following the structural ordinal
// convention); a leaf without a real subset uploads the full texture bounds, so the clamp is a
// no-op. ColorSpaceXform parameters are one shared chain-wide block, so a chain may contain at
// most one color-space op (enforced by the matcher). There is no opcode array, loop, or VM
// dispatch.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef TEXTURE_COUNT
#define TEXTURE_COUNT 0
#endif

#if TEXTURE_COUNT == 0
#define NTEX 1
#elif TEXTURE_COUNT == 1
#define NTEX 2
#else
#define NTEX 4
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  int RootIndex;
  // Per-leaf subset rects, named for the structural ordinals the TextureEffect writers use.
  vec4 Subset;
#if NTEX >= 2
  vec4 Subset_1;
#endif
#if NTEX >= 4
  vec4 Subset_2;
  vec4 Subset_3;
#endif
  // Shared ColorSpaceXform parameters; at most one slot per chain may be OP_COLOR_SPACE_XFORM.
  int ChainCSFlags;
  vec4 ChainSrcTF0;
  vec4 ChainSrcTF1;
  int ChainSrcTFType;
  vec4 ChainSrcOOTF;
  mat3 ChainColorXform;
  vec4 ChainDstOOTF;
  vec4 ChainDstTF0;
  vec4 ChainDstTF1;
  int ChainDstTFType;

#define TGFX_CHAIN_PACKED Slot0Packed
#define TGFX_CHAIN_CONST_COLOR Slot0ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot0ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot0ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot0LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF0
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF0
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot0
#if NTEX > 0
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_0, clamp(TransformedCoords_0, Subset.xy, Subset.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot1Packed
#define TGFX_CHAIN_CONST_COLOR Slot1ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot1ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot1ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot1LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF1
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF1
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot1
#if NTEX > 1
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_1, clamp(TransformedCoords_1, Subset_1.xy, Subset_1.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot2Packed
#define TGFX_CHAIN_CONST_COLOR Slot2ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot2ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot2ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot2LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF2
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF2
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot2
#if NTEX > 2
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_2, clamp(TransformedCoords_2, Subset_2.xy, Subset_2.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot3Packed
#define TGFX_CHAIN_CONST_COLOR Slot3ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot3ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot3ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot3LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF3
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF3
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot3
#if NTEX > 3
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_3, clamp(TransformedCoords_3, Subset_3.xy, Subset_3.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot4Packed
#define TGFX_CHAIN_CONST_COLOR Slot4ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot4ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot4ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot4LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF4
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF4
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot4
#if NTEX > 4
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_4, clamp(TransformedCoords_4, Subset_4.xy, Subset_4.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot5Packed
#define TGFX_CHAIN_CONST_COLOR Slot5ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot5ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot5ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot5LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF5
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF5
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot5
#if NTEX > 5
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_5, clamp(TransformedCoords_5, Subset_5.xy, Subset_5.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot6Packed
#define TGFX_CHAIN_CONST_COLOR Slot6ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot6ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot6ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot6LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF6
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF6
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot6
#if NTEX > 6
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_6, clamp(TransformedCoords_6, Subset_6.xy, Subset_6.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot7Packed
#define TGFX_CHAIN_CONST_COLOR Slot7ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot7ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot7ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot7LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF7
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF7
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot7
#if NTEX > 7
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_7, clamp(TransformedCoords_7, Subset_7.xy, Subset_7.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot8Packed
#define TGFX_CHAIN_CONST_COLOR Slot8ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot8ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot8ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot8LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF8
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF8
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot8
#if NTEX > 8
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_8, clamp(TransformedCoords_8, Subset_8.xy, Subset_8.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot9Packed
#define TGFX_CHAIN_CONST_COLOR Slot9ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot9ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot9ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot9LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF9
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF9
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot9
#if NTEX > 9
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_9, clamp(TransformedCoords_9, Subset_9.xy, Subset_9.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot10Packed
#define TGFX_CHAIN_CONST_COLOR Slot10ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot10ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot10ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot10LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF10
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF10
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot10
#if NTEX > 10
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_10, clamp(TransformedCoords_10, Subset_10.xy, Subset_10.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot11Packed
#define TGFX_CHAIN_CONST_COLOR Slot11ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot11ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot11ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot11LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF11
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF11
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot11
#if NTEX > 11
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_11, clamp(TransformedCoords_11, Subset_11.xy, Subset_11.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot12Packed
#define TGFX_CHAIN_CONST_COLOR Slot12ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot12ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot12ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot12LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF12
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF12
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot12
#if NTEX > 12
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_12, clamp(TransformedCoords_12, Subset_12.xy, Subset_12.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot13Packed
#define TGFX_CHAIN_CONST_COLOR Slot13ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot13ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot13ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot13LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF13
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF13
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot13
#if NTEX > 13
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_13, clamp(TransformedCoords_13, Subset_13.xy, Subset_13.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot14Packed
#define TGFX_CHAIN_CONST_COLOR Slot14ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot14ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot14ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot14LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF14
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF14
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot14
#if NTEX > 14
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_14, clamp(TransformedCoords_14, Subset_14.xy, Subset_14.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot15Packed
#define TGFX_CHAIN_CONST_COLOR Slot15ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot15ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot15ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot15LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF15
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF15
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot15
#if NTEX > 15
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_15, clamp(TransformedCoords_15, Subset_15.xy, Subset_15.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_uniforms.inc"
#include "pointwise_chain_unbind.inc"

#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;
#if NTEX >= 2
layout(location = 1) in vec2 TransformedCoords_1;
#endif
#if NTEX >= 4
layout(location = 2) in vec2 TransformedCoords_2;
layout(location = 3) in vec2 TransformedCoords_3;
#endif
#if HAS_COVERAGE
layout(location = NTEX) in float vCoverage;
#endif

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;
#if NTEX >= 2
layout(set = 1, binding = 1) uniform sampler2D TextureSampler_1;
#endif
#if NTEX >= 4
layout(set = 1, binding = 2) uniform sampler2D TextureSampler_2;
layout(set = 1, binding = 3) uniform sampler2D TextureSampler_3;
#endif

// The chain kernel blends regardless of XP state, so include the shared blend math directly;
// xp_porter_duff.inc re-includes it under HAS_XP >= 1, guarded against double definition.
#include "xp_blend_colors.inc"
#define XP_DST_TEX_BINDING NTEX
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

vec4 chainResults[16];

#define TGFX_CHAIN_PACKED Slot0Packed
#define TGFX_CHAIN_CONST_COLOR Slot0ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot0ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot0ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot0LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF0
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF0
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot0
#if NTEX > 0
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_0, clamp(TransformedCoords_0, Subset.xy, Subset.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot1Packed
#define TGFX_CHAIN_CONST_COLOR Slot1ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot1ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot1ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot1LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF1
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF1
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot1
#if NTEX > 1
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_1, clamp(TransformedCoords_1, Subset_1.xy, Subset_1.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot2Packed
#define TGFX_CHAIN_CONST_COLOR Slot2ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot2ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot2ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot2LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF2
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF2
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot2
#if NTEX > 2
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_2, clamp(TransformedCoords_2, Subset_2.xy, Subset_2.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot3Packed
#define TGFX_CHAIN_CONST_COLOR Slot3ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot3ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot3ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot3LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF3
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF3
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot3
#if NTEX > 3
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_3, clamp(TransformedCoords_3, Subset_3.xy, Subset_3.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot4Packed
#define TGFX_CHAIN_CONST_COLOR Slot4ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot4ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot4ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot4LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF4
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF4
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot4
#if NTEX > 4
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_4, clamp(TransformedCoords_4, Subset_4.xy, Subset_4.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot5Packed
#define TGFX_CHAIN_CONST_COLOR Slot5ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot5ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot5ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot5LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF5
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF5
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot5
#if NTEX > 5
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_5, clamp(TransformedCoords_5, Subset_5.xy, Subset_5.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot6Packed
#define TGFX_CHAIN_CONST_COLOR Slot6ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot6ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot6ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot6LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF6
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF6
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot6
#if NTEX > 6
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_6, clamp(TransformedCoords_6, Subset_6.xy, Subset_6.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot7Packed
#define TGFX_CHAIN_CONST_COLOR Slot7ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot7ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot7ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot7LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF7
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF7
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot7
#if NTEX > 7
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_7, clamp(TransformedCoords_7, Subset_7.xy, Subset_7.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot8Packed
#define TGFX_CHAIN_CONST_COLOR Slot8ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot8ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot8ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot8LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF8
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF8
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot8
#if NTEX > 8
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_8, clamp(TransformedCoords_8, Subset_8.xy, Subset_8.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot9Packed
#define TGFX_CHAIN_CONST_COLOR Slot9ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot9ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot9ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot9LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF9
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF9
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot9
#if NTEX > 9
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_9, clamp(TransformedCoords_9, Subset_9.xy, Subset_9.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot10Packed
#define TGFX_CHAIN_CONST_COLOR Slot10ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot10ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot10ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot10LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF10
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF10
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot10
#if NTEX > 10
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_10, clamp(TransformedCoords_10, Subset_10.xy, Subset_10.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot11Packed
#define TGFX_CHAIN_CONST_COLOR Slot11ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot11ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot11ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot11LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF11
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF11
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot11
#if NTEX > 11
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_11, clamp(TransformedCoords_11, Subset_11.xy, Subset_11.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot12Packed
#define TGFX_CHAIN_CONST_COLOR Slot12ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot12ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot12ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot12LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF12
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF12
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot12
#if NTEX > 12
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_12, clamp(TransformedCoords_12, Subset_12.xy, Subset_12.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot13Packed
#define TGFX_CHAIN_CONST_COLOR Slot13ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot13ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot13ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot13LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF13
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF13
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot13
#if NTEX > 13
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_13, clamp(TransformedCoords_13, Subset_13.xy, Subset_13.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot14Packed
#define TGFX_CHAIN_CONST_COLOR Slot14ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot14ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot14ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot14LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF14
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF14
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot14
#if NTEX > 14
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_14, clamp(TransformedCoords_14, Subset_14.xy, Subset_14.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

#define TGFX_CHAIN_PACKED Slot15Packed
#define TGFX_CHAIN_CONST_COLOR Slot15ConstColor
#define TGFX_CHAIN_COLOR_MATRIX Slot15ColorMatrix
#define TGFX_CHAIN_COLOR_VECTOR Slot15ColorVector
#define TGFX_CHAIN_LUMA_THRESH Slot15LumaThresh
#define TGFX_CHAIN_SRC_TF_FUNC chainSrcTF15
#define TGFX_CHAIN_DST_TF_FUNC chainDstTF15
#define TGFX_CHAIN_EVAL_FUNC evalChainSlot15
#if NTEX > 15
// Leaf subsets are runtime uniforms: a leaf without one uploads full bounds (no-op clamp).
#define TGFX_CHAIN_TEX_FETCH texture(TextureSampler_15, clamp(TransformedCoords_15, Subset_15.xy, Subset_15.zw))
#else
// This slot is never a texture leaf at this texture count; the fetch is dead code.
#define TGFX_CHAIN_TEX_FETCH vec4(0.0)
#endif
#include "pointwise_chain_bind.inc"
#include "pointwise_chain_eval.inc"
#include "pointwise_chain_unbind.inc"

void main() {
  chainResults[0] = evalChainSlot0();
  chainResults[1] = evalChainSlot1();
  chainResults[2] = evalChainSlot2();
  chainResults[3] = evalChainSlot3();
  chainResults[4] = evalChainSlot4();
  chainResults[5] = evalChainSlot5();
  chainResults[6] = evalChainSlot6();
  chainResults[7] = evalChainSlot7();
  chainResults[8] = evalChainSlot8();
  chainResults[9] = evalChainSlot9();
  chainResults[10] = evalChainSlot10();
  chainResults[11] = evalChainSlot11();
  chainResults[12] = evalChainSlot12();
  chainResults[13] = evalChainSlot13();
  chainResults[14] = evalChainSlot14();
  chainResults[15] = evalChainSlot15();
  vec4 result = chainResults[RootIndex];

#if HAS_COVERAGE
#define TGFX_XP_SRC_COLOR (result * vCoverage)
#define TGFX_XP_SRC_UNPREMUL result
#define TGFX_XP_COVERAGE vec4(vCoverage)
#else
#define TGFX_XP_SRC_COLOR result
#endif
#include "xp_output.inc"
}
