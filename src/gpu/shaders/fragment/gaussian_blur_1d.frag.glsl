// Copyright (C) 2026 Tencent. All rights reserved.
// gaussian_blur_1d.frag.glsl - GaussianBlur1DFragmentProcessor modular shader function.
// Performs 1D Gaussian blur by sampling a child FP at offset coordinates and weighting.
//
// This is a container FP with a loop structure. The child FP sampling is performed
// by the caller at each offset; this function computes the weighted sum.
//
// Parameters:
//   sigma   - Gaussian sigma value
//   step    - texel offset direction (vec2, e.g. (1/w, 0) for horizontal)
//   maxLoop - maximum loop count (2 * ceil(2 * maxSigma)), compile-time constant
//
// Required macros:
//   TGFX_GB1D_MAX_LOOP - maximum loop iterations (4 * maxSigma)
//
// Note: In practice, the child FP is emitted inline inside the loop by the builder.
// This .glsl file documents the loop structure for reference. The actual loop with
// child FP calls is expanded by ModularProgramBuilder::emitGaussianBlur1DFragmentProcessor().

#ifndef TGFX_GB1D_MAX_LOOP
#define TGFX_GB1D_MAX_LOOP 24
#endif

// The blur loop structure (pseudo-code for documentation):
// vec4 TGFX_GaussianBlur1D(float sigma, vec2 step, childFP) {
//     vec2 offset = step;
//     int radius = int(ceil(2.0 * sigma));
//     vec4 sum = vec4(0.0);
//     float total = 0.0;
//     for (int j = 0; j <= TGFX_GB1D_MAX_LOOP; ++j) {
//         int i = j - radius;
//         float weight = exp(-float(i*i) / (2.0*sigma*sigma));
//         total += weight;
//         // Sample child FP at (texCoord + offset * float(i)):
//         vec4 childColor = childFP(texCoord + offset * float(i));
//         sum += childColor * weight;
//         if (i == radius) { break; }
//     }
//     return sum / total;
// }
