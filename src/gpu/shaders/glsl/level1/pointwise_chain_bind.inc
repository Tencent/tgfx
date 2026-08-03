// Redirects every chain-slot name to one slot of the fused kernel. Include this before
// pointwise_chain_uniforms.inc (to declare that slot's fields) or before pointwise_chain_eval.inc
// (to define that slot's eval function), then include pointwise_chain_unbind.inc to restore the
// base names before binding the next slot.
//
// The caller sets TGFX_CHAIN_* to the slot-prefixed field names, because the GLSL preprocessor has
// no token-pasting operator and cannot build "Slot0Packed" from a prefix and an index.
#define Packed TGFX_CHAIN_PACKED
#define ConstColor TGFX_CHAIN_CONST_COLOR
#define ColorMatrix TGFX_CHAIN_COLOR_MATRIX
#define ColorVector TGFX_CHAIN_COLOR_VECTOR
#define LumaThresh TGFX_CHAIN_LUMA_THRESH
// The transfer-function helpers and the eval entry point are functions, not uniforms, so each slot
// needs its own names to avoid redefinition when pointwise_chain_eval.inc is included again.
#define chainSrcTF TGFX_CHAIN_SRC_TF_FUNC
#define chainDstTF TGFX_CHAIN_DST_TF_FUNC
#define evalChainSlot TGFX_CHAIN_EVAL_FUNC
#define chainTexFetch TGFX_CHAIN_TEX_FETCH
