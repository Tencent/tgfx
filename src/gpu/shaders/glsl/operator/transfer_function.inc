// Shared ColorSpaceXform transfer function. Single
// source for the pointwise operator kernels (per-slot src_tf/dst_tf wrappers) and the chain
// kernel's OP_COLOR_SPACE_XFORM slot. Parameters are passed explicitly so each caller can feed
// its own uniform names: tf0 packs (G, A, B, C), tf1 packs (D, E, F), tfType selects the curve
// family.
//
// The curve is applied to r, g and b in one call. A scalar function called once per channel would
// put six inlined copies of the four-way curve selection into every kernel. Each lane performs the
// scalar operations in the same order, and mix() with a boolean vector is a true
// component-wise select (SPIR-V OpSelect): a lane keeps exactly the arm the scalar ternary would
// have taken, and whatever the other arm computed on that lane (NaN included) is discarded.
#ifndef TGFX_TRANSFER_FUNCTION_INC
#define TGFX_TRANSFER_FUNCTION_INC

#include "fn/operator/transfer_tf.inc"

#endif  // TGFX_TRANSFER_FUNCTION_INC
