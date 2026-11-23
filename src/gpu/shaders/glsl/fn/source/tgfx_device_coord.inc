// The device-space coordinate a layer's content is sampled at: the fragment position through the
// draw's matrix. One call so the coordinate is a value a bytecode route can name.
//@function tgfxDeviceCoord
vec2 tgfxDeviceCoord(mat3 matrix, highp vec2 fragCoord) {
  return (matrix * vec3(fragCoord, 1.0)).xy;
}
