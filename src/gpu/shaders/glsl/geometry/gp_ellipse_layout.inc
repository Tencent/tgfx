// The single location table of EllipseGeometryProcessor's vertex interface: the attribute and
// varying locations the dedicated ellipse kernel (kernels/ellipse_fill.vert / .frag) and the
// chain's GP_LAYOUT 1 both reference, so the two sides cannot drift apart. Attribute order:
// position, color, ellipseOffset, ellipseRadii. Varying order: offsets, radii, color.
#ifndef TGFX_GP_ELLIPSE_LAYOUT_INC
#define TGFX_GP_ELLIPSE_LAYOUT_INC
#define GP_ELLIPSE_IN_COLOR 1
#define GP_ELLIPSE_IN_OFFSET 2
#define GP_ELLIPSE_IN_RADII 3
#define GP_ELLIPSE_V_OFFSETS 0
#define GP_ELLIPSE_V_RADII 1
#define GP_ELLIPSE_V_COLOR 2
#define GP_ELLIPSE_VARYING_COUNT 3
#endif
