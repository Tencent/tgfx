// Shared gradient layout math: computes the gradient parameter t from transformed coordinates
// based on the runtime layout type (0=LINEAR, 1=RADIAL, 2=CONIC, 3=DIAMOND). Single source for
// the dedicated gradient kernels, the instanced-shape gradient coverage kernel, and the chain
// kernel's OP_GRADIENT slot. Parameters are passed explicitly so each caller can feed its own
// uniform names.
#ifndef TGFX_GRADIENT_LAYOUT_INC
#define TGFX_GRADIENT_LAYOUT_INC

#include "fn/gradient/layout.inc"

#endif  // TGFX_GRADIENT_LAYOUT_INC
