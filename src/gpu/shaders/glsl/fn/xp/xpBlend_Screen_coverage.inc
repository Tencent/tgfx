//@function xpBlend_Screen_coverage
vec4 xpBlend_Screen_coverage(vec4 S, vec4 D, vec4 C) {
  return clamp(S * C + D * (vec4(1.0) - S * C), 0.0, 1.0);
}
