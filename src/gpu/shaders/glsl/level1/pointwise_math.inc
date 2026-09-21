// Explicit-parameter pointwise math shared by the plain evaluator (pointwise_op.inc, scalar
// uniforms) and the chain evaluator (pointwise_chain_eval.inc, packed selectors and slot
// arrays). Each function carries exactly the operator's math, byte-identical to the previously
// duplicated bodies; only the parameter sourcing differs per caller. Keep this file free of
// uniform names, samplers, and selector decoding so both evaluators (and any future one) can
// include it.
#ifndef TGFX_POINTWISE_MATH_INC
#define TGFX_POINTWISE_MATH_INC

vec4 pmApplyColorMatrix(vec4 color, mat4 matrix, vec4 vector) {
  // Unpremultiply, apply matrix + vector, clamp, re-premultiply.
  color = vec4(color.rgb / max(color.a, 9.9999997473787516e-05), color.a);
  color = clamp(matrix * color + vector, 0.0, 1.0);
  color.rgb *= color.a;
  return color;
}

vec4 pmApplyLuma(vec4 color, vec3 lumaCoefficients) {
  return vec4(dot(color.rgb, lumaCoefficients));
}

vec4 pmApplyAlphaThreshold(vec4 color, float threshold) {
  vec4 result = vec4(0.0);
  if (color.a > 0.0) {
    result.rgb = color.rgb / color.a;
    result.a = step(threshold, color.a);
    result = clamp(result, 0.0, 1.0);
  }
  return result;
}

// 0 = Ignore (replace), 1 = ModulateRGBA, 2 = ModulateA.
vec4 pmApplyConstColor(vec4 constColor, vec4 inputColor, int inputMode) {
  if (inputMode == 1) {
    return constColor * inputColor;
  }
  if (inputMode == 2) {
    return constColor * inputColor.a;
  }
  return constColor;
}

#endif  // TGFX_POINTWISE_MATH_INC
