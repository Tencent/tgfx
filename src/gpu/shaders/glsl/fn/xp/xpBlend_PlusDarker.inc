//@function xpBlend_PlusDarker
//@op XpBlendPlusDarker
//@frag
vec4 xpBlend_PlusDarker(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  result.rgb = clamp(1.0 + S.rgb + D.rgb - D.a - S.a, 0.0, 1.0);
  result.rgb *= (result.a > 0.0) ? 1.0 : 0.0;

  return result;
}
