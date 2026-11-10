// The geometry layouts' own per-pixel edge coverage formulas, selected by GP_LAYOUT: the ellipse,
// hairline-line and complex rounded-rect layouts evaluate coverage per fragment (the rect layouts
// carry the vCoverage varying instead). Shared verbatim by the two assemblies: the legacy kernel
// (through pointwise_interface_support.inc) and the declared Chain module's fragment hook.

#if GP_LAYOUT == 1
#include "geometry/ellipse_coverage.inc"
#elif GP_LAYOUT == 2
#include "geometry/hairline_line_coverage.inc"
#elif GP_LAYOUT >= 3
#include "geometry/complex_ellipse_coverage.inc"
#endif
