//@function xpBlend_DstOver_coverage
vec4 xpBlend_DstOver_coverage(vec4 S, vec4 D, vec4 C) {
  return clamp(S * C * (1.0 - D.a) + D, 0.0, 1.0);
}
