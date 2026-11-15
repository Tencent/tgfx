// DeviceMask fragment hook: the device-space mask's red channel multiplies the running coverage.
// The expressions are bit-exact by contract.
void DeviceMask_fragment(inout Fragment f) {
  highp vec3 maskCoord = DeviceMask_CoordMatrix * vec3(gl_FragCoord.xy, 1.0);
  f.coverage *= vec4(texture(DeviceMask_Sampler, maskCoord.xy).r);
}
