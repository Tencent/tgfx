//@function xpBlend_Dst_coverage
vec4 xpBlend_Dst_coverage(vec4 S, vec4 D, vec4 C) {
  return clamp(D, 0.0, 1.0);
}
