//@function xpBlend_Exclusion
vec4 xpBlend_Exclusion(vec4 S, vec4 D, vec4 dstSrcAlpha, vec4 srcDstAlpha) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  result.rgb = D.rgb + S.rgb - 2.0 * D.rgb * S.rgb;

  return result;
}
