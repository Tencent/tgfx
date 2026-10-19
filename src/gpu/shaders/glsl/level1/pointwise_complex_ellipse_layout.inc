// Varying locations of the pointwise kernels' ComplexEllipse layouts (GP_LAYOUT 3 = fill,
// 4 = stroke), shared by the vertex stage (pointwise_vertex.inc) and the fragment interface
// (pointwise_fragment_interface.inc) so both sides agree. Fixed: offsets 0, radii 1, edge
// distances 2. Then the stroke width (stroke only) and the per-vertex color (HAS_COLOR only), then
// the leaf coordinates. With both present the leaves end at location 8, past the procedural-source
// varying's usual location 7, so these layouts move SourceCoords to 9.
#ifndef TGFX_POINTWISE_COMPLEX_ELLIPSE_LAYOUT_INC
#define TGFX_POINTWISE_COMPLEX_ELLIPSE_LAYOUT_INC
#if GP_LAYOUT == 4
#define CHAIN_COMPLEX_STROKE_LOC 3
#define CHAIN_COMPLEX_COLOR_LOC 4
#if HAS_COLOR
#define CHAIN_TEX_LOC_BASE 5
#else
#define CHAIN_TEX_LOC_BASE 4
#endif
#else
#define CHAIN_COMPLEX_COLOR_LOC 3
#if HAS_COLOR
#define CHAIN_TEX_LOC_BASE 4
#else
#define CHAIN_TEX_LOC_BASE 3
#endif
#endif
#define CHAIN_SOURCE_COORDS_LOC 9
#endif
