// Pure pointwise fragment: the alpha step function. See ops/ColorMatrix.glsl for the rules.
//@function pmApplyAlphaThreshold
//@op AlphaThreshold fragment
vec4 pmApplyAlphaThreshold(vec4 color /*@in*/, float threshold) {
  vec4 result = vec4(0.0);
  if (color.a > 0.0) {
    result.rgb = color.rgb / color.a;
    result.a = step(threshold, color.a);
    result = clamp(result, 0.0, 1.0);
  }
  return result;
}
