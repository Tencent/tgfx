//@function xpBlend_Lighten
//@op XpBlendLighten
//@frag
vec4 xpBlend_Lighten(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  result.rgb = max((1.0 - S.a) * D.rgb + S.rgb, (1.0 - D.a) * S.rgb + D.rgb);

  return result;
}
