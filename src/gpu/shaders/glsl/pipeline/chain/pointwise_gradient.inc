// Gradient source module (OP_GRADIENT), used by the gradient resource kind (RESOURCE_KIND 1) of
// the chain interpreter. Every input arrives as an explicit argument — one spelling shared by the
// interpreter's variant text, the GL assembly and the Vulkan body — except the leaf samplers the
// LUT colorizer dispatches over, which stay file-scope globals every consumer declares itself.
#ifndef TGSL_POINTWISE_GRADIENT_INCLUDED
#define TGSL_POINTWISE_GRADIENT_INCLUDED

#include "source/gradient_layout.inc"
#include "source/gradient_colorize.inc"
#include "fn/gradient/colorize_mix.inc"
#include "fn/gradient/colorize_dual.inc"
#include "fn/gradient/colorize_lut.inc"

// Evaluates one gradient. The layout math and border clamp are verbatim from the dedicated
// gradient kernels; the colorizer kind selects between the single-interval mix, the dual-interval
// scale/bias pair and the unrolled-binary tree, each byte-matching its dedicated kernel. The
// epilogue premultiplies and modulates by the input alpha, matching the runtime
// ClampedGradientEffect emission.
#include "fn/entry/gradient.inc"


#endif  // TGSL_POINTWISE_GRADIENT_INCLUDED
