// The XferProcessor blend forms and the texture sampling helpers the chain's evaluation loop
// references in every variant (the dispatch is runtime-uniform, so texture_sampling.inc stays
// unconditional; only the tiled state needs leaves). Shared verbatim by the two assemblies: the
// legacy kernel (through pointwise_interface_support.inc) and the declared Chain module's
// fragment hook.

#ifndef TGSL_POINTWISE_XP_SUPPORT_INCLUDED
#define TGSL_POINTWISE_XP_SUPPORT_INCLUDED

#include "xp/xp_blend_colors.inc"
#include "xp/xp_porter_duff.inc"
#include "xp/xp_porter_duff_fbf.inc"
#include "xp/xp_blend_pass.inc"
#include "source/texture_sampling.inc"
#if NTEX > 0
#include "pipeline/chain/pointwise_tiled_sampling.inc"
#endif


#endif  // TGSL_POINTWISE_XP_SUPPORT_INCLUDED
