//@function evalGradientSlot
//@members Gradient_
//@member layoutType GradientLayoutType
//@member colorizerKind GradientColorizerKind
//@member lutLeaf GradientLUTLeaf
//@source lutLeaf GradientLutLeaf
//@when lutLeaf lutForm
//@member scale0 Gradient_scales[0]
//@member scale1 Gradient_scales[1]
//@member scale2 Gradient_scales[2]
//@member scale3 Gradient_scales[3]
//@member scale4 Gradient_scales[4]
//@member scale5 Gradient_scales[5]
//@member scale6 Gradient_scales[6]
//@member scale7 Gradient_scales[7]
//@member bias0 Gradient_biases[0]
//@member bias1 Gradient_biases[1]
//@member bias2 Gradient_biases[2]
//@member bias3 Gradient_biases[3]
//@member bias4 Gradient_biases[4]
//@member bias5 Gradient_biases[5]
//@member bias6 Gradient_biases[6]
//@member bias7 Gradient_biases[7]
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
    gradColor = gradientColorizeMix(start, end, gradT);
  } else if (colorizerKind == 1) {
    gradColor = gradientColorizeDual(scale01, bias01, scale23, bias23, threshold, gradT);
  } else if (colorizerKind == 3) {
    // LUT colorizer, matching the dedicated texture_gradient kernel: sample the baked gradient
    // texture at the computed t. The LUT child occupies a leaf slot for sampler binding only;
    // lutLeaf selects it with static indices. Unreachable when NTEX == 0 (a LUT chain always
    // carries the child), but the branch must still compile for those variants.
    gradColor = gradientColorizeLut(lutLeaf, gradT);
  } else {
    gradColor = gradientColorizeUnrolled(gradT, thresholds1_7, thresholds9_13, scale0, bias0,
                                         scale1, bias1, scale2, bias2, scale3, bias3, scale4,
                                         bias4, scale5, bias5, scale6, bias6, scale7, bias7);
  }
  gradColor.rgb *= gradColor.a;
  return gradColor * in0.a;
}
