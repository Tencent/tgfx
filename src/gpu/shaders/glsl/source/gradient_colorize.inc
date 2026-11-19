// Shared gradient colorizer: maps t in [0,1] to a color using piecewise linear segments.
// All interval counts share this path at runtime. Single source for the chain kernel's
// OP_GRADIENT slot. All interval uniforms are passed explicitly so each caller
// can feed its own uniform names.
//
// Packing (see UnrolledBinaryGradientColorizer::Make): thresholds1_7 holds the interval end
// positions of intervals 0..3 in ascending order, thresholds9_13 those of intervals 4..6; slots
// beyond the real interval count are zero-padded with transparent scales/biases. The selection is
// a linear scan: crossing a real threshold advances to the next interval, and the > 0 guard stops
// the scan at the last real interval, matching the padded-transparent leaves of the binary tree
// for out-of-range t. The scan needs no explicit interval count (that is why the
// parameter was removed: it was never read). The selected (scale, bias) pair is identical to the
// tree's in every case, so results are byte-identical.
#ifndef TGFX_GRADIENT_COLORIZE_INC
#define TGFX_GRADIENT_COLORIZE_INC

#include "fn/gradient/colorize_unrolled.inc"

#endif  // TGFX_GRADIENT_COLORIZE_INC
