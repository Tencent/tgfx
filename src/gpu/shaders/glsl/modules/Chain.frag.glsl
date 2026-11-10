// The chain body's fragment hook: the legacy interpreter's whole evaluation, verbatim. The
// assembly order matters — the config prologue derives the legacy macros from PROFILE, the
// chain-ops pragma expands the operator table the evaluation dispatches on, and the shared
// includes supply the geometry coverage formulas, the xp forms and the resource modules before the
// main text (the same order the legacy kernel's prologue and extra includes used).
//
// TGFX_DECLARED_CHAIN switches the shared text to this assembly: the assembler declares
// fragColor, the unrolled executor comes from the module's UnrollSite macro, and the entry point
// is the hook below instead of the legacy thin main.

#define TGFX_DECLARED_CHAIN 1

#include "pipeline/chain/pointwise_declared_config.inc"
#include "source/texture_read_swizzle.inc"
#pragma tgfx_chain_ops

#include "pipeline/chain/pointwise_geometry_coverage.inc"
#include "pipeline/chain/pointwise_leaf_dispatch.inc"
#include "pipeline/chain/pointwise_xp_support.inc"
#include "pipeline/chain/pointwise_resource_eval.inc"
#include "pipeline/chain/pointwise_fragment_main.inc"

void Chain_fragment(inout Fragment f) {
  chainMain();
}
