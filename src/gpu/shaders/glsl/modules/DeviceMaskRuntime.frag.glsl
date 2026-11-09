// DeviceMaskRuntime fragment hook: the runtime-gated device-space mask (the former
// FILL_DEVICE_MASK_RUNTIME). The expressions are verbatim from the former fill pipeline's
// fillMaskStage.
void DeviceMaskRuntime_fragment(inout Fragment f) {
  if (DeviceMaskRuntime_HasDeviceMask != 0) {
    highp vec3 maskCoord =
        DeviceMaskRuntime_CoordMatrix * vec3(gl_FragCoord.xy, 1.0);
    f.coverage *= vec4(texture(DeviceMaskRuntime_Sampler, maskCoord.xy).r);
  }
}
