// The math and evaluation modules the chain's interface binds to: the geometry's own coverage
// formula for the layouts that carry one, the shared XferProcessor blend, and the texture sampling
// helpers the evaluation loop references in every variant (the dispatch is runtime-uniform, so
// texture_sampling.inc stays unconditional; only the tiled state needs leaves).

#if GP_LAYOUT == 1
#include "geometry/ellipse_coverage.inc"
#elif GP_LAYOUT == 2
#include "geometry/hairline_line_coverage.inc"
#elif GP_LAYOUT >= 3
#include "geometry/complex_ellipse_coverage.inc"
#endif
#include "xp/xp_blend_colors.inc"
#include "xp/xp_porter_duff.inc"
#include "xp/xp_porter_duff_fbf.inc"
#include "xp/xp_blend_pass.inc"
#include "source/texture_sampling.inc"
#if NTEX > 0
#include "pipeline/chain/pointwise_tiled_sampling.inc"
#endif
