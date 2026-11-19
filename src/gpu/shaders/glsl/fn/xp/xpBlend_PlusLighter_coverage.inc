//@function xpBlend_PlusLighter_coverage
vec4 xpBlend_PlusLighter_coverage(vec4 S, vec4 D, vec4 C) {
  return clamp(S * C + D, 0.0, 1.0);
}
