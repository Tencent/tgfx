//@function xpBlend_SrcOut_coverage
vec4 xpBlend_SrcOut_coverage(vec4 S, vec4 D, vec4 C) {
  return clamp(S * C * (1.0 - D.a) + D * (1.0 - C.a), 0.0, 1.0);
}
