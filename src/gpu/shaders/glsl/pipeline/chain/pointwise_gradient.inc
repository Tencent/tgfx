// Gradient source module (OP_GRADIENT), used by the gradient resource kind (RESOURCE_KIND 1) of
// PointwiseChainShader. Pairs with pointwise_gradient_uniforms.inc.
#include "operator/op_codes.inc"
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
  highp vec2 gradientCoord = SourceCoords.xy / SourceCoords.z;
  float gradT = gradientLayoutT(GradientLayoutType, GradientBias, GradientScale, gradientCoord);
  vec4 gradColor;
  if (gradT <= 0.0) {
    gradColor = GradientLeftBorder;
  } else if (gradT >= 1.0) {
    gradColor = GradientRightBorder;
  } else if (GradientColorizerKind == 0) {
    // Keep the same arithmetic order as GLSLSingleIntervalGradientColorizer on the JIT path.
    gradColor = (1.0 - gradT) * GradientStart + gradT * GradientEnd;
  } else if (GradientColorizerKind == 1) {
    vec4 gradScale = GradientScale01;
    vec4 gradBias = GradientBias01;
    if (gradT >= GradientThreshold) {
      gradScale = GradientScale23;
      gradBias = GradientBias23;
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
#if NTEX > 1
    if (GradientLUTLeaf == 1) {
      gradColor = texture(TextureSampler_1, lutCoord);
    }
#endif
#if NTEX > 2
    if (GradientLUTLeaf == 2) {
      gradColor = texture(TextureSampler_2, lutCoord);
    }
    if (GradientLUTLeaf == 3) {
      gradColor = texture(TextureSampler_3, lutCoord);
    }
#endif
#endif
  } else {
    gradColor = gradientColorizeUnrolled(
        gradT, GradientThresholds1_7, GradientThresholds9_13, GradientScale0_1, GradientBias0_1,
        GradientScale2_3, GradientBias2_3, GradientScale4_5, GradientBias4_5, GradientScale6_7,
        GradientBias6_7, GradientScale8_9, GradientBias8_9, GradientScale10_11,
        GradientBias10_11, GradientScale12_13, GradientBias12_13, GradientScale14_15,
        GradientBias14_15);
  }
  gradColor.rgb *= gradColor.a;
  value = gradColor * in0.a;
  return true;
}
