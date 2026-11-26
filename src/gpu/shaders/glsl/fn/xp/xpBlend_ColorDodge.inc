//@function xpBlend_ColorDodge
//@op XpBlendColorDodge
//@frag
vec4 xpBlend_ColorDodge(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  vec3 d = vec3(S.a) - S.rgb;
  vec3 dSafe = mix(vec3(1.0), d, notEqual(d, vec3(0.0)));
  vec3 dd = min(vec3(D.a), D.rgb * vec3(S.a) / dSafe);
  vec3 valElse = dd * vec3(S.a) + S.rgb * (1.0 - D.a) + D.rgb * (1.0 - S.a);
  vec3 valZeroD = vec3(S.a * D.a) + S.rgb * (1.0 - D.a) + D.rgb * (1.0 - S.a);
  vec3 val = mix(valElse, valZeroD, equal(d, vec3(0.0)));
  result.rgb = mix(val, S.rgb * (1.0 - D.a), equal(D.rgb, vec3(0.0)));

  return result;
}
