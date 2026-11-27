// The repeat seam's far-edge coordinate: the error's sign picks the edge the mirrored tap lands
// on. It lives in a function because the IR has no select expression.
//@function tgfxTiledRepeatCoord
//@op TiledRepeatCoord
//@frag
float tgfxTiledRepeatCoord(highp float err /*@in*/, highp float lo /*@in*/, highp float hi /*@in*/) {
  return err > 0.0 ? lo : hi;
}
