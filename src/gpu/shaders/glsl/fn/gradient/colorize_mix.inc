//@function gradientColorizeMix
vec4 gradientColorizeMix(vec4 start, vec4 end, float gradT) {
  // The arithmetic order matches GLSLSingleIntervalGradientColorizer on the JIT path.
  return (1.0 - gradT) * start + gradT * end;
}
