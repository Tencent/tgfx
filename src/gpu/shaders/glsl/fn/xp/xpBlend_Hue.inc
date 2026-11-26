//@function xpBlend_Hue
//@op XpBlendHue
//@frag
vec4 xpBlend_Hue(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  vec3 hueSat = xpSetSaturation(S.rgb * D.a, dstSrcAlpha.rgb);
  result.rgb = xpSetLuminance(hueSat, dstSrcAlpha.a, dstSrcAlpha.rgb);
  result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
  return result;
}
