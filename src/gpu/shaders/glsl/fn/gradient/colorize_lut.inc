//@function gradientColorizeLut
vec4 gradientColorizeLut(int lutLeaf, float gradT, sampler2D s0, sampler2D s1, sampler2D s2,
                         sampler2D s3) {
  // The baked gradient texture, sampled at the computed t; lutLeaf selects the leaf slot with
  // static indices so no driver sees a dynamic sampler index.
  vec4 gradColor = vec4(0.0);
  highp vec2 lutCoord = vec2(gradT, 0.5);
  if (lutLeaf == 0) {
    gradColor = texture(s0, lutCoord);
  }
  if (lutLeaf == 1) {
    gradColor = texture(s1, lutCoord);
  }
  if (lutLeaf == 2) {
    gradColor = texture(s2, lutCoord);
  }
  if (lutLeaf == 3) {
    gradColor = texture(s3, lutCoord);
  }
  return gradColor;
}
