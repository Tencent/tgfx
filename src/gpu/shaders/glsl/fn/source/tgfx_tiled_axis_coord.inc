// One axis of the tiled coordinate transform. The mode is the call site's literal: 0 none/clamp/
// border (the coordinate passes through), 1 repeat, 2 repeat with the mipmap seam, 3 mirror.
// Returns (subsetCoord, extraCoord, weight, 0): extraCoord and weight are meaningful only for
// mode 2.
//@function tgfxTiledAxisCoord
//@op TiledAxisCoord
//@frag
vec4 tgfxTiledAxisCoord(highp float coord /*@in*/, highp float lo /*@in*/, highp float hi /*@in*/,
                        int mode /*@in*/) {
  if (mode == 0) {
    return vec4(coord, 0.0, 0.0, 0.0);
  }
  highp float w = hi - lo;
  highp float d = coord - lo;
  if (mode == 1) {
    // Plain repeat wraps over the subset width itself.
    return vec4(mod(d, w) + lo, 0.0, 0.0, 0.0);
  }
  // Mirror and the mipmap seam wrap over the doubled width.
  highp float w2 = 2.0 * w;
  highp float m = mod(d, w2);
  highp float o = mix(m, w2 - m, step(w, m));
  if (mode == 2) {
    highp float hw = w / 2.0;
    highp float n = mod(d - hw, w2);
    highp float weight = clamp(mix(n, w2 - n, step(w, n)) - hw + 0.5, 0.0, 1.0);
    return vec4(o + lo, w - o + lo, weight, 0.0);
  }
  return vec4(o + lo, 0.0, 0.0, 0.0);
}
