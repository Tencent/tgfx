//@function xpBlend_Src_coverage
vec4 xpBlend_Src_coverage(vec4 S, vec4 D, vec4 C) {
  return clamp(S * C + D * (1.0 - C.a), 0.0, 1.0);
}
