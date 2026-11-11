// Gradient source module (OP_GRADIENT), used by the gradient resource kind (RESOURCE_KIND 1) of
// the chain interpreter. Pairs with the shared UBO declaration.
#include "pipeline/chain/pointwise_source_coords.inc"
#include "source/gradient_layout.inc"
#include "source/gradient_colorize.inc"

// Evaluates an OP_GRADIENT slot; returns false for every other op.
bool evalGradientSlot(ivec4 Packed, vec4 in0, out vec4 value) {
  value = vec4(0.0);
  if (Packed.x != OP_GRADIENT) {
    return false;
  }
  // Analytic gradient source. The layout math and border clamp are verbatim from the dedicated
  // gradient kernels; the colorizer kind selects between the single-interval mix, the
  // dual-interval scale/bias pair and the unrolled-binary tree, each byte-matching its
  // dedicated kernel. The epilogue premultiplies and modulates by the input alpha, matching
  // the runtime ClampedGradientEffect emission.
  // The gradient's block sits in the pool's chain-wide region: one gradient per chain, so the
  // offset is the region base plus the block's fixed slot, not a per-slot index. The capacity
  // macro is emitted by the build from the Chain module's declared count, so the number exists
  // once — in the declaration the assembler sizes the pool from.
  GradientData d = chainLoadGradient(CHAIN_SLOT_CAPACITY * TGFX_CHAIN_DATA_SLOT_WORDS +
                                     TGFX_CHAIN_BLOCK_GRADIENT);
  highp vec2 gradientCoord = SourceCoords.xy / SourceCoords.z;
  float gradT = gradientLayoutT(GradientLayoutType, d.bias, d.scale, gradientCoord);
  vec4 gradColor;
  if (gradT <= 0.0) {
    gradColor = d.leftBorder;
  } else if (gradT >= 1.0) {
    gradColor = d.rightBorder;
  } else if (GradientColorizerKind == 0) {
    // Keep the same arithmetic order as GLSLSingleIntervalGradientColorizer on the JIT path.
    gradColor = (1.0 - gradT) * d.start + gradT * d.end;
  } else if (GradientColorizerKind == 1) {
    vec4 gradScale = d.scale01;
    vec4 gradBias = d.bias01;
    if (gradT >= d.threshold) {
      gradScale = d.scale23;
      gradBias = d.bias23;
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
        gradT, d.thresholds1_7, d.thresholds9_13, d.scales[0], d.biases[0], d.scales[1],
        d.biases[1], d.scales[2], d.biases[2], d.scales[3], d.biases[3], d.scales[4],
        d.biases[4], d.scales[5], d.biases[5], d.scales[6], d.biases[6], d.scales[7],
        d.biases[7]);
  }
  gradColor.rgb *= gradColor.a;
  value = gradColor * in0.a;
  return true;
}
