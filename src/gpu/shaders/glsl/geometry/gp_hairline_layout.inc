// The single location table of HairlineLineGeometryProcessor's vertex interface: the attribute
// and varying locations the dedicated hairline kernel (kernels/hairline_line.vert / .frag) and
// the chain's GP_LAYOUT 2 both reference, so the two sides cannot drift apart. Attribute order:
// position, edgeDistance, color — the color attribute is unconditional in the vertex buffers, but
// only the chain reads it (its color pipeline starts from the geometric color; the dedicated
// kernel takes the color from a uniform). The edge-distance varying occupies location 0, so the
// chain's leaf coordinates start at 1.
#ifndef TGFX_GP_HAIRLINE_LAYOUT_INC
#define TGFX_GP_HAIRLINE_LAYOUT_INC
#define GP_HAIRLINE_IN_POSITION 0
#define GP_HAIRLINE_IN_EDGE 1
#define GP_HAIRLINE_IN_COLOR 2
#define GP_HAIRLINE_V_EDGE 0
#endif
