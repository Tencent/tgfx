//@function xpBlend_Difference
//@op XpBlendDifference
//@frag
vec4 xpBlend_Difference(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  result.rgb = S.rgb + D.rgb - 2.0 * min(S.rgb * D.a, D.rgb * S.a);

  return result;
}
