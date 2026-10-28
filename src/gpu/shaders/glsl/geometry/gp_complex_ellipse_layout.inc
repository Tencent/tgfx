// The single location table of ComplexEllipseGeometryProcessor's vertex interface: the attribute
// and varying locations the dedicated kernel (kernels/complex_ellipse_fill.vert / .frag) and the
// chain's GP_LAYOUT 3/4 both reference, so the two sides cannot drift apart. Attribute order:
// position, color, ellipseOffset, ellipseRadii, edgeDist, [strokeWidth]. Varying order: offsets 0,
// radii 1, edge distances 2, color 3, stroke width 4 (stroke variant only). The chain's leaf
// coordinates follow the geometry varyings; with all leaves present they end at location 8, past
// the procedural-source varying's usual location 7, so the chain moves SourceCoords to 9.
#ifndef TGFX_GP_COMPLEX_ELLIPSE_LAYOUT_INC
#define TGFX_GP_COMPLEX_ELLIPSE_LAYOUT_INC
#define GP_COMPLEX_IN_COLOR 1
#define GP_COMPLEX_IN_OFFSET 2
#define GP_COMPLEX_IN_RADII 3
#define GP_COMPLEX_IN_EDGE_DIST 4
#define GP_COMPLEX_IN_STROKE 5
#define GP_COMPLEX_V_OFFSETS 0
#define GP_COMPLEX_V_RADII 1
#define GP_COMPLEX_V_EDGE_DIST 2
#define GP_COMPLEX_V_COLOR 3
#define GP_COMPLEX_V_STROKE 4
// The chain's leaf coordinates start right after the geometry varyings (GP_LAYOUT is a chain
// dimension; the dedicated kernel leaves these two unused).
#if GP_LAYOUT == 4
#define CHAIN_TEX_LOC_BASE 5
#else
#define CHAIN_TEX_LOC_BASE 4
#endif
#define CHAIN_SOURCE_COORDS_LOC 9
#endif
