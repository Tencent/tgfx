// The final noise color: clamped, then premultiplied with its fourth channel as alpha.
//@function tgfxNoisePremul
vec4 tgfxNoisePremul(vec4 color) {
  vec4 c = clamp(color, 0.0, 1.0);
  return vec4(c.rgb * c.aaa, c.a);
}
