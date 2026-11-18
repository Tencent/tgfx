// Gradient source module (OP_GRADIENT), used by the gradient resource kind (RESOURCE_KIND 1) of
// the chain interpreter. Pairs with the shared UBO declaration.
#ifndef TGSL_POINTWISE_GRADIENT_INCLUDED
#define TGSL_POINTWISE_GRADIENT_INCLUDED

#include "pipeline/chain/pointwise_source_coords.inc"
#include "source/gradient_layout.inc"
#include "source/gradient_colorize.inc"

// Evaluates one gradient from its named parameter block (the Chain module's Gradient_ members)
// and the shared layout/colorizer fields: no pool, no operator compare, no capacity macro.
vec4 evalGradientSlot(vec4 in0) {
  // Analytic gradient source. The layout math and border clamp are verbatim from the dedicated
  // gradient kernels; the colorizer kind selects between the single-interval mix, the
  // dual-interval scale/bias pair and the unrolled-binary tree, each byte-matching its
  // dedicated kernel. The epilogue premultiplies and modulates by the input alpha, matching
  // the runtime ClampedGradientEffect emission.
  // The gradient's block sits in the pool's chain-wide region: one gradient per chain, so the
  // offset is the region base plus the block's fixed slot, not a per-slot index. The capacity
  // macro is emitted by the build from the Chain module's declared count, so the number exists
  // once — in the declaration the assembler sizes the pool from.
  highp vec2 gradientCoord = SourceCoords.xy / SourceCoords.z;
  float gradT = gradientLayoutT(GradientLayoutType, Gradient_bias, Gradient_scale, gradientCoord);
  vec4 gradColor;
  if (gradT <= 0.0) {
    gradColor = Gradient_leftBorder;
  } else if (gradT >= 1.0) {
    gradColor = Gradient_rightBorder;
  } else if (GradientColorizerKind == 0) {
    // Keep the same arithmetic order as GLSLSingleIntervalGradientColorizer on the JIT path.
    gradColor = (1.0 - gradT) * Gradient_start + gradT * Gradient_end;
  } else if (GradientColorizerKind == 1) {
    vec4 gradScale = Gradient_scale01;
    vec4 gradBias = Gradient_bias01;
    if (gradT >= Gradient_threshold) {
      gradScale = Gradient_scale23;
      gradBias = Gradient_bias23;
    }
    gradColor = vec4(gradT * gradScale + gradBias);
  } else if (GradientColorizerKind == 3) {
    // LUT colorizer, matching the dedicated texture_gradient kernel: sample the baked gradient
    // texture at the computed t. The LUT child occupies a leaf slot for sampler binding only;
    // GradientLUTLeaf selects it with static indices. Unreachable when NTEX == 0 (a LUT chain
    // always carries the child), but the branch must still compile for those variants.
    gradColor = vec4(0.0);
#if NTEX > 0
    highp vec2 lutCoord = vec2(gradT, 0.5);
    if (GradientLUTLeaf == 0) {
      gradColor = texture(TextureSampler_0, lutCoord);
    }
    if (GradientLUTLeaf == 1) {
      gradColor = texture(TextureSampler_1, lutCoord);
    }
    if (GradientLUTLeaf == 2) {
      gradColor = texture(TextureSampler_2, lutCoord);
    }
    if (GradientLUTLeaf == 3) {
      gradColor = texture(TextureSampler_3, lutCoord);
    }
#endif
  } else {
    gradColor = gradientColorizeUnrolled(
        gradT, Gradient_thresholds1_7, Gradient_thresholds9_13, Gradient_scales[0], Gradient_biases[0], Gradient_scales[1],
        Gradient_biases[1], Gradient_scales[2], Gradient_biases[2], Gradient_scales[3], Gradient_biases[3], Gradient_scales[4],
        Gradient_biases[4], Gradient_scales[5], Gradient_biases[5], Gradient_scales[6], Gradient_biases[6], Gradient_scales[7],
        Gradient_biases[7]);
  }
  gradColor.rgb *= gradColor.a;
  return gradColor * in0.a;
}


#endif  // TGSL_POINTWISE_GRADIENT_INCLUDED
