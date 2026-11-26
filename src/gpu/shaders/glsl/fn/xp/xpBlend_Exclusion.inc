//@function xpBlend_Exclusion
//@op XpBlendExclusion
//@frag
vec4 xpBlend_Exclusion(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  result.rgb = D.rgb + S.rgb - 2.0 * D.rgb * S.rgb;

  return result;
}
