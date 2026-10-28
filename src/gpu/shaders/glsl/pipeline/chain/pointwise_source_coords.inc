// The procedural-source coordinate of the pointwise kernels: one varying carrying the source
// coordinate transform of the kernel's gradient or noise source. The vertex stage writes it when
// built with TGFX_POINTWISE_SOURCE_COORDS (pointwise_chain.vert). Included by the source modules,
// so it is declared once whichever source the kernel carries.
#ifndef TGFX_POINTWISE_SOURCE_COORDS_INC
#define TGFX_POINTWISE_SOURCE_COORDS_INC
// Location 7 sits past every layout's leaf coordinates except the ComplexEllipse ones, which
// define their own (gp_complex_ellipse_layout.inc).
#ifndef CHAIN_SOURCE_COORDS_LOC
#define CHAIN_SOURCE_COORDS_LOC 7
#endif
layout(location = CHAIN_SOURCE_COORDS_LOC) in vec3 SourceCoords;
#endif
