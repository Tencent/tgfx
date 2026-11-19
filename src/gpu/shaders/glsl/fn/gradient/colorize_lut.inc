//@function gradientColorizeLut
vec4 gradientColorizeLut(int lutLeaf, float gradT) {
  // The baked gradient texture, sampled at the computed t; lutLeaf selects the leaf slot with
  // static indices so no driver sees a dynamic sampler index.
  vec4 gradColor = vec4(0.0);
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
  return gradColor;
}
