//@function xpBlend_DstOver
vec4 xpBlend_DstOver(vec4 S, vec4 D, vec4 dstSrcAlpha, vec4 srcDstAlpha) {
  vec4 result;
  result = clamp(S * (1.0 - D.a) + D, 0.0, 1.0);

  return result;
}
