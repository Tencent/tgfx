//@op Luma
//@frag
vec4 pmApplyLuma(vec4 color /*@in*/, float kr, float kg, float kb) {
  float luma = dot(color.rgb, vec3(kr, kg, kb));
  return vec4(luma);
}
