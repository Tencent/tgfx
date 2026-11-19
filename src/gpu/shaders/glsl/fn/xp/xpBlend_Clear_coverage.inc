//@function xpBlend_Clear_coverage
vec4 xpBlend_Clear_coverage(vec4 S, vec4 D, vec4 C) {
  return clamp(D - C * D, 0.0, 1.0);
}
