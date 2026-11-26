//@function xpBlend_Darken
//@op XpBlendDarken
//@frag
vec4 xpBlend_Darken(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  result.rgb = min((1.0 - S.a) * D.rgb + S.rgb, (1.0 - D.a) * S.rgb + D.rgb);

  return result;
}
