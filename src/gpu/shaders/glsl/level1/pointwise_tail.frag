// PointwiseTailShader fragment shader.
// SourceKind is a runtime field: 0 samples the local TextureEffect coordinate and 1 samples a
// DeviceSpaceTextureEffect from gl_FragCoord. Slot0 and Slot1 are two independent operator records,
// each carrying the full parameter set of every pointwise operator. Both slots are statically
// expanded; there is no opcode array, loop, or VM dispatch.
//
// Each slot's uniform fields and its applyPointwiseSlotN function come from the shared
// pointwise_op_uniforms.inc / pointwise_op.inc, redirected to slot-prefixed names by
// pointwise_slot_bind.inc. The GLSL preprocessor has no token pasting, so the slot field names are
// spelled out in the TGFX_SLOT_* defines below.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  int SourceKind;
  mat3 DeviceCoordMatrix;
  // Always declared: a draw without a subset uploads the full texture bounds, making the clamp a
  // no-op, so subset handling needs no compile-time dimension.
  vec4 Subset;

#define TGFX_SLOT_OP_TYPE Slot0OpType
#define TGFX_SLOT_COLOR_MATRIX Slot0ColorMatrix
#define TGFX_SLOT_COLOR_VECTOR Slot0ColorVector
#define TGFX_SLOT_KR Slot0Kr
#define TGFX_SLOT_KG Slot0Kg
#define TGFX_SLOT_KB Slot0Kb
#define TGFX_SLOT_THRESHOLD Slot0Threshold
#define TGFX_SLOT_CS_FLAGS Slot0CSFlags
#define TGFX_SLOT_SRC_TF0 Slot0SrcTF0
#define TGFX_SLOT_SRC_TF1 Slot0SrcTF1
#define TGFX_SLOT_SRC_TF_TYPE Slot0SrcTFType
#define TGFX_SLOT_SRC_OOTF Slot0SrcOOTF
#define TGFX_SLOT_COLOR_XFORM Slot0ColorXform
#define TGFX_SLOT_DST_OOTF Slot0DstOOTF
#define TGFX_SLOT_DST_TF0 Slot0DstTF0
#define TGFX_SLOT_DST_TF1 Slot0DstTF1
#define TGFX_SLOT_DST_TF_TYPE Slot0DstTFType
#define TGFX_SLOT_CONST_COLOR_VALUE Slot0ConstColorValue
#define TGFX_SLOT_CONST_INPUT_MODE Slot0ConstInputMode
#define TGFX_SLOT_BLEND_MODE Slot0BlendModeValue
#define TGFX_SLOT_BLEND_CONST_FIRST Slot0BlendConstFirst
#define TGFX_SLOT_SRC_TF_FUNC pointwiseTailSrcTF0
#define TGFX_SLOT_DST_TF_FUNC pointwiseTailDstTF0
#define TGFX_SLOT_APPLY_FUNC applyPointwiseSlot0
#include "pointwise_slot_bind.inc"
#include "pointwise_op_uniforms.inc"
#include "pointwise_slot_unbind.inc"

#define TGFX_SLOT_OP_TYPE Slot1OpType
#define TGFX_SLOT_COLOR_MATRIX Slot1ColorMatrix
#define TGFX_SLOT_COLOR_VECTOR Slot1ColorVector
#define TGFX_SLOT_KR Slot1Kr
#define TGFX_SLOT_KG Slot1Kg
#define TGFX_SLOT_KB Slot1Kb
#define TGFX_SLOT_THRESHOLD Slot1Threshold
#define TGFX_SLOT_CS_FLAGS Slot1CSFlags
#define TGFX_SLOT_SRC_TF0 Slot1SrcTF0
#define TGFX_SLOT_SRC_TF1 Slot1SrcTF1
#define TGFX_SLOT_SRC_TF_TYPE Slot1SrcTFType
#define TGFX_SLOT_SRC_OOTF Slot1SrcOOTF
#define TGFX_SLOT_COLOR_XFORM Slot1ColorXform
#define TGFX_SLOT_DST_OOTF Slot1DstOOTF
#define TGFX_SLOT_DST_TF0 Slot1DstTF0
#define TGFX_SLOT_DST_TF1 Slot1DstTF1
#define TGFX_SLOT_DST_TF_TYPE Slot1DstTFType
#define TGFX_SLOT_CONST_COLOR_VALUE Slot1ConstColorValue
#define TGFX_SLOT_CONST_INPUT_MODE Slot1ConstInputMode
#define TGFX_SLOT_BLEND_MODE Slot1BlendModeValue
#define TGFX_SLOT_BLEND_CONST_FIRST Slot1BlendConstFirst
#define TGFX_SLOT_SRC_TF_FUNC pointwiseTailSrcTF1
#define TGFX_SLOT_DST_TF_FUNC pointwiseTailDstTF1
#define TGFX_SLOT_APPLY_FUNC applyPointwiseSlot1
#include "pointwise_slot_bind.inc"
#include "pointwise_op_uniforms.inc"
#include "pointwise_slot_unbind.inc"

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

#define TGFX_SLOT_OP_TYPE Slot0OpType
#define TGFX_SLOT_COLOR_MATRIX Slot0ColorMatrix
#define TGFX_SLOT_COLOR_VECTOR Slot0ColorVector
#define TGFX_SLOT_KR Slot0Kr
#define TGFX_SLOT_KG Slot0Kg
#define TGFX_SLOT_KB Slot0Kb
#define TGFX_SLOT_THRESHOLD Slot0Threshold
#define TGFX_SLOT_CS_FLAGS Slot0CSFlags
#define TGFX_SLOT_SRC_TF0 Slot0SrcTF0
#define TGFX_SLOT_SRC_TF1 Slot0SrcTF1
#define TGFX_SLOT_SRC_TF_TYPE Slot0SrcTFType
#define TGFX_SLOT_SRC_OOTF Slot0SrcOOTF
#define TGFX_SLOT_COLOR_XFORM Slot0ColorXform
#define TGFX_SLOT_DST_OOTF Slot0DstOOTF
#define TGFX_SLOT_DST_TF0 Slot0DstTF0
#define TGFX_SLOT_DST_TF1 Slot0DstTF1
#define TGFX_SLOT_DST_TF_TYPE Slot0DstTFType
#define TGFX_SLOT_CONST_COLOR_VALUE Slot0ConstColorValue
#define TGFX_SLOT_CONST_INPUT_MODE Slot0ConstInputMode
#define TGFX_SLOT_BLEND_MODE Slot0BlendModeValue
#define TGFX_SLOT_BLEND_CONST_FIRST Slot0BlendConstFirst
#define TGFX_SLOT_SRC_TF_FUNC pointwiseTailSrcTF0
#define TGFX_SLOT_DST_TF_FUNC pointwiseTailDstTF0
#define TGFX_SLOT_APPLY_FUNC applyPointwiseSlot0
#include "pointwise_slot_bind.inc"
#include "pointwise_op.inc"
#include "pointwise_slot_unbind.inc"

#define TGFX_SLOT_OP_TYPE Slot1OpType
#define TGFX_SLOT_COLOR_MATRIX Slot1ColorMatrix
#define TGFX_SLOT_COLOR_VECTOR Slot1ColorVector
#define TGFX_SLOT_KR Slot1Kr
#define TGFX_SLOT_KG Slot1Kg
#define TGFX_SLOT_KB Slot1Kb
#define TGFX_SLOT_THRESHOLD Slot1Threshold
#define TGFX_SLOT_CS_FLAGS Slot1CSFlags
#define TGFX_SLOT_SRC_TF0 Slot1SrcTF0
#define TGFX_SLOT_SRC_TF1 Slot1SrcTF1
#define TGFX_SLOT_SRC_TF_TYPE Slot1SrcTFType
#define TGFX_SLOT_SRC_OOTF Slot1SrcOOTF
#define TGFX_SLOT_COLOR_XFORM Slot1ColorXform
#define TGFX_SLOT_DST_OOTF Slot1DstOOTF
#define TGFX_SLOT_DST_TF0 Slot1DstTF0
#define TGFX_SLOT_DST_TF1 Slot1DstTF1
#define TGFX_SLOT_DST_TF_TYPE Slot1DstTFType
#define TGFX_SLOT_CONST_COLOR_VALUE Slot1ConstColorValue
#define TGFX_SLOT_CONST_INPUT_MODE Slot1ConstInputMode
#define TGFX_SLOT_BLEND_MODE Slot1BlendModeValue
#define TGFX_SLOT_BLEND_CONST_FIRST Slot1BlendConstFirst
#define TGFX_SLOT_SRC_TF_FUNC pointwiseTailSrcTF1
#define TGFX_SLOT_DST_TF_FUNC pointwiseTailDstTF1
#define TGFX_SLOT_APPLY_FUNC applyPointwiseSlot1
#include "pointwise_slot_bind.inc"
#include "pointwise_op.inc"
#include "pointwise_slot_unbind.inc"

void main() {
  highp vec2 finalCoord = TransformedCoords_0;
  if (SourceKind != 0) {
    finalCoord = (DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0)).xy;
  }
  finalCoord = clamp(finalCoord, Subset.xy, Subset.zw);
  vec4 color = texture(TextureSampler_0, finalCoord) * Color.a;
  vec4 result = applyPointwiseSlot1(applyPointwiseSlot0(color));

#if HAS_COVERAGE
#define TGFX_XP_SRC_COLOR (result * vCoverage)
#define TGFX_XP_SRC_UNPREMUL result
#define TGFX_XP_COVERAGE vec4(vCoverage)
#else
#define TGFX_XP_SRC_COLOR result
#endif
#include "xp_output.inc"
}
