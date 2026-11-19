// Gradient source module (OP_GRADIENT), used by the gradient resource kind (RESOURCE_KIND 1) of
// the chain interpreter. Every input arrives as an explicit argument — one spelling shared by the
// interpreter's variant text, the GL assembly and the Vulkan body — except the leaf samplers the
// LUT colorizer dispatches over, which stay file-scope globals every consumer declares itself.
#ifndef TGSL_POINTWISE_GRADIENT_INCLUDED
#define TGSL_POINTWISE_GRADIENT_INCLUDED

#include "source/gradient_layout.inc"
#include "source/gradient_colorize.inc"

// Evaluates one gradient. The layout math and border clamp are verbatim from the dedicated
// gradient kernels; the colorizer kind selects between the single-interval mix, the dual-interval
// scale/bias pair and the unrolled-binary tree, each byte-matching its dedicated kernel. The
// epilogue premultiplies and modulates by the input alpha, matching the runtime
// ClampedGradientEffect emission.
vec4 evalGradientSlot(vec4 in0, vec3 sourceCoords, int layoutType, int colorizerKind, int lutLeaf,
                      vec4 leftBorder, vec4 rightBorder, vec4 start, vec4 end, vec4 scale01,
                      vec4 bias01, vec4 scale23, vec4 bias23, float threshold, float bias,
                      float scale, vec4 thresholds1_7, vec4 thresholds9_13, vec4 scale0,
                      vec4 scale1, vec4 scale2, vec4 scale3, vec4 scale4, vec4 scale5, vec4 scale6,
                      vec4 scale7, vec4 bias0, vec4 bias1, vec4 bias2, vec4 bias3, vec4 bias4,
                      vec4 bias5, vec4 bias6, vec4 bias7) {
  highp vec2 gradientCoord = sourceCoords.xy / sourceCoords.z;
  float gradT = gradientLayoutT(layoutType, bias, scale, gradientCoord);
  vec4 gradColor;
  if (gradT <= 0.0) {
    gradColor = leftBorder;
  } else if (gradT >= 1.0) {
    gradColor = rightBorder;
  } else if (colorizerKind == 0) {
    // Keep the same arithmetic order as GLSLSingleIntervalGradientColorizer on the JIT path.
    gradColor = (1.0 - gradT) * start + gradT * end;
  } else if (colorizerKind == 1) {
    vec4 gradScale = scale01;
    vec4 gradBias = bias01;
    if (gradT >= threshold) {
      gradScale = scale23;
      gradBias = bias23;
    }
    gradColor = vec4(gradT * gradScale + gradBias);
  } else if (colorizerKind == 3) {
    // LUT colorizer, matching the dedicated texture_gradient kernel: sample the baked gradient
    // texture at the computed t. The LUT child occupies a leaf slot for sampler binding only;
    // lutLeaf selects it with static indices. Unreachable when NTEX == 0 (a LUT chain always
    // carries the child), but the branch must still compile for those variants.
    gradColor = vec4(0.0);
#if NTEX > 0
    highp vec2 lutCoord = vec2(gradT, 0.5);
    if (lutLeaf == 0) {
      gradColor = texture(TextureSampler_0, lutCoord);
    }
    if (lutLeaf == 1) {
      gradColor = texture(TextureSampler_1, lutCoord);
    }
    if (lutLeaf == 2) {
      gradColor = texture(TextureSampler_2, lutCoord);
    }
    if (lutLeaf == 3) {
      gradColor = texture(TextureSampler_3, lutCoord);
    }
#endif
  } else {
    gradColor = gradientColorizeUnrolled(gradT, thresholds1_7, thresholds9_13, scale0, bias0,
                                         scale1, bias1, scale2, bias2, scale3, bias3, scale4,
                                         bias4, scale5, bias5, scale6, bias6, scale7, bias7);
  }
  gradColor.rgb *= gradColor.a;
  return gradColor * in0.a;
}

#endif  // TGSL_POINTWISE_GRADIENT_INCLUDED
