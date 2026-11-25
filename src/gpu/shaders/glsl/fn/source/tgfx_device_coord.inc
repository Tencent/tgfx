// The device-space coordinate a layer's content is sampled at: the fragment position through the
// draw's matrix. One call so the coordinate is a value a bytecode route can name.
//@function tgfxDeviceCoord
//@op DeviceCoord
//@frag
vec2 tgfxDeviceCoord(mat3 matrix /*@in*/, highp vec2 fragCoord /*@in*/) {
  return (matrix * vec3(fragCoord, 1.0)).xy;
}
