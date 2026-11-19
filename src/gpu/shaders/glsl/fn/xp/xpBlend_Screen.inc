//@function xpBlend_Screen
vec4 xpBlend_Screen(vec4 S, vec4 D, vec4 dstSrcAlpha, vec4 srcDstAlpha) {
  vec4 result;
  result = clamp(S + D - S * D, 0.0, 1.0);

  return result;
}
