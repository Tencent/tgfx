// Restores the base names after a chain slot's fields or eval function have been emitted, so the
// next slot can be bound. See pointwise_chain_bind.inc.
#undef Packed
#undef ConstColor
#undef ColorMatrix
#undef ColorVector
#undef LumaThresh
#undef chainSrcTF
#undef chainDstTF
#undef evalChainSlot
#undef chainTexFetch

#undef TGFX_CHAIN_PACKED
#undef TGFX_CHAIN_CONST_COLOR
#undef TGFX_CHAIN_COLOR_MATRIX
#undef TGFX_CHAIN_COLOR_VECTOR
#undef TGFX_CHAIN_LUMA_THRESH
#undef TGFX_CHAIN_SRC_TF_FUNC
#undef TGFX_CHAIN_DST_TF_FUNC
#undef TGFX_CHAIN_EVAL_FUNC
#undef TGFX_CHAIN_TEX_FETCH
