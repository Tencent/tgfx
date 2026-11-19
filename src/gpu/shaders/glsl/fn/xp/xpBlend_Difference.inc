//@function xpBlend_Difference
vec4 xpBlend_Difference(vec4 S, vec4 D, vec4 dstSrcAlpha, vec4 srcDstAlpha) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  result.rgb = S.rgb + D.rgb - 2.0 * min(S.rgb * D.a, D.rgb * S.a);

  return result;
}
