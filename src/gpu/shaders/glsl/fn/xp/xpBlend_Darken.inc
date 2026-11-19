//@function xpBlend_Darken
vec4 xpBlend_Darken(vec4 S, vec4 D, vec4 dstSrcAlpha, vec4 srcDstAlpha) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  result.rgb = min((1.0 - S.a) * D.rgb + S.rgb, (1.0 - D.a) * S.rgb + D.rgb);

  return result;
}
