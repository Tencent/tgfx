// One axis of the draw-level clamp. The flag is the call site's literal.
//@function tgfxTiledAxisClamp
float tgfxTiledAxisClamp(highp float coord, highp float lo, highp float hi, int on) {
  if (on != 0) {
    return clamp(coord, lo, hi);
  }
  return coord;
}
