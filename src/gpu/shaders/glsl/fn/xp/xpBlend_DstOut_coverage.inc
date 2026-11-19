//@function xpBlend_DstOut_coverage
vec4 xpBlend_DstOut_coverage(vec4 S, vec4 D, vec4 C) {
  return clamp(D * (1.0 - (S * C).a), 0.0, 1.0);
}
