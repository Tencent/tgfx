// The procedural-source coordinate of the pointwise kernels: one varying carrying the source
// coordinate transform of the kernel's gradient or noise source. The vertex stage writes it when
// built with TGFX_POINTWISE_SOURCE_COORDS (pointwise_graph.vert). Included by the source modules,
// so it is declared once whichever source the kernel carries.
#ifndef TGFX_POINTWISE_SOURCE_COORDS_INC
#define TGFX_POINTWISE_SOURCE_COORDS_INC
layout(location = 7) in vec3 SourceCoords;
#endif
