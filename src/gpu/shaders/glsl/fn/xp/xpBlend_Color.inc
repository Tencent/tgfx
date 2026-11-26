//@function xpBlend_Color
//@op XpBlendColor
//@frag
vec4 xpBlend_Color(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  vec3 hueSat = srcDstAlpha.rgb;
  result.rgb = xpSetLuminance(hueSat, srcDstAlpha.a, D.rgb * S.a);
  result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
  return result;
}
