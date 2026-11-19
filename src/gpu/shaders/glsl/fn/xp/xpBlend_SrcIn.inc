//@function xpBlend_SrcIn
vec4 xpBlend_SrcIn(vec4 S, vec4 D, vec4 dstSrcAlpha, vec4 srcDstAlpha) {
  vec4 result;
  result = clamp(S * D.a, 0.0, 1.0);

  return result;
}
