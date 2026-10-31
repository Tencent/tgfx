// The single composition site where a pipeline's source color and its true coverage meet the
// XferProcessor (xp_output.inc). Every kernel's main() ends here.
//
// The caller defines, before including:
//   TGFX_COMPOSE_SRC_UNPREMUL  the source color WITHOUT the coverage premultiplied in
//   TGFX_COMPOSE_COVERAGE      the true coverage (a vec4; composites as c*blend(S,D)+(1-c)*D)
// and, when the kernel runs the XferProcessor's advanced blend as a pass of its evaluation loop
// (xp_blend_pass.inc), also the pair TGFX_COMPOSE_DST (the dst read ahead of the loop) and
// TGFX_COMPOSE_BLENDED (the pass result).
//
// The coverage value itself is the caller's left fold — the geometry's own coverage first, then
// the device mask, then the local mask, then the clip. Floating-point multiplication commutes
// exactly but does not associate, so that fold order is each pipeline's documented contract (see
// fill_fragment_main.inc, pointwise_fragment_main.inc and coverage_output.inc, which pre-folds
// the mask and clip terms for the opaque kernels). This file only fixes the meeting point, once,
// for every kernel: no pipeline may define the TGFX_XP_* macros itself.
#define TGFX_XP_SRC_COLOR (TGFX_COMPOSE_SRC_UNPREMUL * TGFX_COMPOSE_COVERAGE)
#define TGFX_XP_SRC_UNPREMUL TGFX_COMPOSE_SRC_UNPREMUL
#define TGFX_XP_COVERAGE TGFX_COMPOSE_COVERAGE
#ifdef TGFX_COMPOSE_DST
#define TGFX_XP_DST_COLOR TGFX_COMPOSE_DST
#define TGFX_XP_BLENDED TGFX_COMPOSE_BLENDED
#endif
#include "xp/xp_output.inc"
#undef TGFX_COMPOSE_SRC_UNPREMUL
#undef TGFX_COMPOSE_COVERAGE
#ifdef TGFX_COMPOSE_DST
#undef TGFX_COMPOSE_DST
#undef TGFX_COMPOSE_BLENDED
#endif
