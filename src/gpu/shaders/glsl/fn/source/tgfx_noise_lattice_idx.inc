// The permutation indices recovered from the [0,1] texture values.
//@function tgfxNoiseLatticeIdx
highp vec2 tgfxNoiseLatticeIdx(highp vec2 perm) {
  return floor(perm * 255.0 + 0.5);
}
