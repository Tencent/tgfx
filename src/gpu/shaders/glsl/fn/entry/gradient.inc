//@function evalGradientSlot Gradient
vec4 evalGradientSlot(vec4 in0 /*@in*/, vec3 sourceCoords /*@in*/, int layoutType, int colorizerKind, int lutLeaf,
                      vec4 leftBorder, vec4 rightBorder, vec4 start, vec4 end, vec4 scale01,
                      vec4 bias01, vec4 scale23, vec4 bias23, float threshold, float bias,
                      float scale, vec4 thresholds1_7, vec4 thresholds9_13, vec4 scales[8], vec4 biases[8]) {
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
#if NTEX > 0
    gradColor = gradientColorizeLut(lutLeaf, gradT, TextureSampler_0, TextureSampler_1,
                                    TextureSampler_2, TextureSampler_3);
#else
    gradColor = vec4(0.0);
#endif
  } else {
    gradColor = gradientColorizeUnrolled(gradT, thresholds1_7, thresholds9_13, scales, biases);
  }
  gradColor.rgb *= gradColor.a;
  return gradColor * in0.a;
}
