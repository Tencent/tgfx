// The procedural-source coordinate of the pointwise kernels: one varying carrying the source
// coordinate transform of the kernel's gradient or noise source. The vertex stage writes it when
// built with TGFX_POINTWISE_SOURCE_COORDS (the legacy vertex entry). Included by the source modules,
// so it is declared once whichever source the kernel carries.
#ifndef TGFX_POINTWISE_SOURCE_COORDS_INC
#define TGFX_POINTWISE_SOURCE_COORDS_INC
// Location 7 sits past every layout's leaf coordinates except the ComplexEllipse ones, which
// define their own (gp_complex_ellipse_layout.inc).
#ifndef CHAIN_SOURCE_COORDS_LOC
#define CHAIN_SOURCE_COORDS_LOC 7
#endif
// The declared Chain assembly declares the varying itself (the Chain module's SourceCoords
// element, numbered by the assembler), so only the legacy kernels declare it here.
#ifndef TGFX_DECLARED_CHAIN
layout(location = CHAIN_SOURCE_COORDS_LOC) in vec3 SourceCoords;
#endif
#endif
