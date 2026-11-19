//@function xpSetLuminance
#pragma tgfx_spec_reader
#pragma tgfx_spec_gate BlendLuminance : SPEC_NeedsBlendMath() != 0
vec3 xpSetLuminance(vec3 hueSat, float alpha, vec3 lumColor) {
  // Match GLSLBlend's set_luminance: subtract the colors before computing luminance so the
  // floating-point reduction has the same rounding path on the runtime and precompiled routes.
  float diff = xpLuminance(lumColor - hueSat);
  vec3 outColor = hueSat + diff;
  float outLum = xpLuminance(outColor);
  float minComp = min(min(outColor.r, outColor.g), outColor.b);
  float maxComp = max(max(outColor.r, outColor.g), outColor.b);
  float lowDen = (outLum != minComp) ? (outLum - minComp) : 1.0;
  vec3 lowClamped = vec3(outLum) + ((outColor - vec3(outLum)) * outLum) / vec3(lowDen);
  outColor = mix(outColor, lowClamped, bvec3(minComp < 0.0 && outLum != minComp));
  float highDen = (maxComp != outLum) ? (maxComp - outLum) : 1.0;
  vec3 highClamped = vec3(outLum) + ((outColor - vec3(outLum)) * (alpha - outLum)) / vec3(highDen);
  return mix(outColor, highClamped, bvec3(maxComp > alpha && maxComp != outLum));
}
