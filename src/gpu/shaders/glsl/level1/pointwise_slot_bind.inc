// Redirects every pointwise-operator name to one slot of a multi-slot kernel. Include this before
// pointwise_op_uniforms.inc (to declare that slot's fields) or before pointwise_op.inc (to define
// that slot's applyPointwiseOp function), then include pointwise_slot_unbind.inc to restore the base
// names before binding the next slot.
//
// The caller sets TGFX_SLOT_* to the slot-prefixed field names, because the GLSL preprocessor has no
// token-pasting operator and cannot build "Slot0OpType" from a prefix and an index.
//
// Usage:
//   #define TGFX_SLOT_PREFIX_0
//   #include "pointwise_slot_bind.inc"
//   #include "pointwise_op.inc"
//   #include "pointwise_slot_unbind.inc"
#define OpType TGFX_SLOT_OP_TYPE
#define ColorMatrix TGFX_SLOT_COLOR_MATRIX
#define ColorVector TGFX_SLOT_COLOR_VECTOR
#define Kr TGFX_SLOT_KR
#define Kg TGFX_SLOT_KG
#define Kb TGFX_SLOT_KB
#define Threshold TGFX_SLOT_THRESHOLD
#define CSFlags TGFX_SLOT_CS_FLAGS
#define SrcTF0 TGFX_SLOT_SRC_TF0
#define SrcTF1 TGFX_SLOT_SRC_TF1
#define SrcTFType TGFX_SLOT_SRC_TF_TYPE
#define SrcOOTF TGFX_SLOT_SRC_OOTF
#define ColorXform TGFX_SLOT_COLOR_XFORM
#define DstOOTF TGFX_SLOT_DST_OOTF
#define DstTF0 TGFX_SLOT_DST_TF0
#define DstTF1 TGFX_SLOT_DST_TF1
#define DstTFType TGFX_SLOT_DST_TF_TYPE
#define ConstColorValue TGFX_SLOT_CONST_COLOR_VALUE
#define ConstInputMode TGFX_SLOT_CONST_INPUT_MODE
#define BlendModeValue TGFX_SLOT_BLEND_MODE
#define BlendConstFirst TGFX_SLOT_BLEND_CONST_FIRST
// The two transfer-function helpers and the operator entry point are functions, not uniforms, so
// each slot needs its own names to avoid redefinition when pointwise_op.inc is included again.
#define src_tf TGFX_SLOT_SRC_TF_FUNC
#define dst_tf TGFX_SLOT_DST_TF_FUNC
#define applyPointwiseOp TGFX_SLOT_APPLY_FUNC
