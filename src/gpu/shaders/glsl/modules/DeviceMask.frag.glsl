// DeviceMask fragment hook: the device-space mask's red channel multiplies the running coverage.
// The expression is verbatim from the former fill pipeline's fillDeviceMaskCoverage().
void DeviceMask_fragment(inout Fragment f) {
  highp vec3 maskCoord = DeviceMask_CoordMatrix * vec3(gl_FragCoord.xy, 1.0);
  f.coverage *= vec4(texture(DeviceMask_Sampler, maskCoord.xy).r);
}
