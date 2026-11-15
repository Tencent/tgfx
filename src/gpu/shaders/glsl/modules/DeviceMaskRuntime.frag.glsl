// DeviceMaskRuntime fragment hook: the runtime-gated device-space mask. The expressions are
// bit-exact by contract.
void DeviceMaskRuntime_fragment(inout Fragment f) {
  if (DeviceMaskRuntime_HasDeviceMask != 0) {
    highp vec3 maskCoord =
        DeviceMaskRuntime_CoordMatrix * vec3(gl_FragCoord.xy, 1.0);
    f.coverage *= vec4(texture(DeviceMaskRuntime_Sampler, maskCoord.xy).r);
  }
}
