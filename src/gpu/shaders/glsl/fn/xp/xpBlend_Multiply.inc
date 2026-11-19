//@function xpBlend_Multiply
vec4 xpBlend_Multiply(vec4 S, vec4 D, vec4 dstSrcAlpha, vec4 srcDstAlpha) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  result.rgb = (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb + S.rgb * D.rgb;

  return result;
}
