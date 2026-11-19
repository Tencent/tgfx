//@function xpBlend_DstIn
vec4 xpBlend_DstIn(vec4 S, vec4 D, vec4 dstSrcAlpha, vec4 srcDstAlpha) {
  vec4 result;
  result = clamp(D * S.a, 0.0, 1.0);

  return result;
}
