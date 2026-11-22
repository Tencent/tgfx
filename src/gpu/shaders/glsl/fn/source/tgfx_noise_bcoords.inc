// The noise-texture row coordinates for the four corners: floorVal packs (floor, floor+1), so the
// yyww swizzle reads the two corner rows of both axes.
//@function tgfxNoiseBcoords
highp vec4 tgfxNoiseBcoords(highp vec2 latticeIdx, highp vec4 floorVal) {
  return mod(latticeIdx.xyxy + floorVal.yyww, 256.0);
}
