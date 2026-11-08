// The chain's varying-location space: each geometry layout's own table, then the base the leaf
// coordinates start from. The dedicated families include the same tables, so the two sides cannot
// drift. This file must precede every varying declaration that names CHAIN_TEX_LOC_BASE.

#if GP_LAYOUT == 1
#include "geometry/gp_ellipse_layout.inc"
#define CHAIN_TEX_LOC_BASE GP_ELLIPSE_VARYING_COUNT
#elif GP_LAYOUT == 2
#include "geometry/gp_hairline_layout.inc"
#define CHAIN_TEX_LOC_BASE 1
#elif GP_LAYOUT >= 3
#include "geometry/gp_complex_ellipse_layout.inc"
#else
#define CHAIN_TEX_LOC_BASE 0
#endif
