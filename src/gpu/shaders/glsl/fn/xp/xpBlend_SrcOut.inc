//@function xpBlend_SrcOut
vec4 xpBlend_SrcOut(vec4 S, vec4 D, vec4 dstSrcAlpha, vec4 srcDstAlpha) {
  vec4 result;
  result = clamp(S * (1.0 - D.a), 0.0, 1.0);

  return result;
}
