//@function pmApplyLuma
vec4 pmApplyLuma(vec4 color, vec3 lumaCoefficients) {
  float luma = dot(color.rgb, lumaCoefficients);
  return vec4(luma);
}
