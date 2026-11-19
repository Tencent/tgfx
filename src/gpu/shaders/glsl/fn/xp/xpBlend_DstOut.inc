//@function xpBlend_DstOut
vec4 xpBlend_DstOut(vec4 S, vec4 D, vec4 dstSrcAlpha, vec4 srcDstAlpha) {
  vec4 result;
  result = clamp(D * (1.0 - S.a), 0.0, 1.0);

  return result;
}
