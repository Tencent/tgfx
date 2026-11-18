// Pure pointwise fragment: luma of a premultiplied color. See ops/ColorMatrix.glsl for the rules.
vec4 pmApplyLuma(vec4 color, vec3 lumaCoefficients) {
  return vec4(dot(color.rgb, lumaCoefficients));
}
