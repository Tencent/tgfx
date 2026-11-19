//@function xpBlend_Modulate_coverage
vec4 xpBlend_Modulate_coverage(vec4 S, vec4 D, vec4 C) {
  return clamp(D - (vec4(1.0) - S) * C * D, 0.0, 1.0);
}
