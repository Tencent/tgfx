//@function xpBlend_SoftLight
vec4 xpBlend_SoftLight(vec4 S, vec4 D, vec4 dstSrcAlpha, vec4 srcDstAlpha) {
  vec4 result;
  result.a = S.a + (1.0 - S.a) * D.a;
  if (D.a == 0.0) {
  result.rgb = S.rgb;
  } else {
  vec3 sa2 = S.a - 2.0 * S.rgb;
  vec3 arm1 = (D.rgb * D.rgb * sa2) / vec3(D.a) + (1.0 - D.a) * S.rgb + D.rgb * (1.0 - sa2);
  vec3 DSqd = D.rgb * D.rgb;
  vec3 DCub = DSqd * D.rgb;
  float DaSqd = D.a * D.a;
  float DaCub = DaSqd * D.a;
  vec3 arm2 = (vec3(DaSqd) * (S.rgb - D.rgb * (3.0 * S.a - 6.0 * S.rgb - 1.0)) +
  12.0 * D.a * DSqd * sa2 - 16.0 * DCub * sa2 - vec3(DaCub) * S.rgb) /
  vec3(DaSqd);
  vec3 arm3 = D.rgb * (sa2 + 1.0) + S.rgb - sqrt(vec3(D.a) * D.rgb) * sa2 - vec3(D.a) * S.rgb;
  vec3 val = mix(arm3, arm2, lessThanEqual(4.0 * D.rgb, vec3(D.a)));
  result.rgb = mix(val, arm1, lessThanEqual(2.0 * S.rgb, vec3(S.a)));
  }

  return result;
}
