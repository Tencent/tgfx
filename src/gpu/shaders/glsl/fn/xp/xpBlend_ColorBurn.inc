//@function xpBlend_ColorBurn
//@op XpBlendColorBurn
//@frag
vec4 xpBlend_ColorBurn(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  vec3 sSafe = mix(vec3(1.0), S.rgb, notEqual(S.rgb, vec3(0.0)));
  vec3 dd = max(vec3(0.0), vec3(D.a) - (vec3(D.a) - D.rgb) * vec3(S.a) / sSafe);
  vec3 valElse = vec3(S.a) * dd + S.rgb * (1.0 - D.a) + D.rgb * (1.0 - S.a);
  vec3 valSrc0 = D.rgb * (1.0 - S.a);
  vec3 valFull = vec3(S.a * D.a) + S.rgb * (1.0 - D.a) + D.rgb * (1.0 - S.a);
  vec3 val = mix(valElse, valSrc0, equal(S.rgb, vec3(0.0)));
  result.rgb = mix(val, valFull, equal(D.rgb, vec3(D.a)));

  return result;
}
