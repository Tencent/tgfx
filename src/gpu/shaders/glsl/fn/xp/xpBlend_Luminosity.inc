//@function xpBlend_Luminosity
//@requires xpSetLuminance,xpSetSaturation
vec4 xpBlend_Luminosity(vec4 S, vec4 D, vec4 dstSrcAlpha, vec4 srcDstAlpha) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  vec3 hueSat = D.rgb * S.a;
  result.rgb = xpSetLuminance(hueSat, srcDstAlpha.a, srcDstAlpha.rgb);
  result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
  return result;
}
