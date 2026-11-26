//@function xpBlend_Multiply
//@op XpBlendMultiply
//@frag
vec4 xpBlend_Multiply(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  result.rgb = (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb + S.rgb * D.rgb;

  return result;
}
