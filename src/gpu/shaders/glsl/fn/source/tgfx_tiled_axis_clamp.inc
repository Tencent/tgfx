// One axis of the draw-level clamp. The flag is the call site's literal.
//@function tgfxTiledAxisClamp
//@op TiledAxisClamp
//@frag
float tgfxTiledAxisClamp(highp float coord /*@in*/, highp float lo /*@in*/, highp float hi /*@in*/,
                         int on /*@in*/) {
  if (on != 0) {
    return clamp(coord, lo, hi);
  }
  return coord;
}
