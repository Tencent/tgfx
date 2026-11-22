// The per-octave noise lattice position: the frequency scale plus the sub-lattice bias that keeps
// an integer baseFrequency off a lattice point (see the runtime emission's note).
//@function tgfxNoiseCoord
highp vec2 tgfxNoiseCoord(highp vec2 coord, vec2 baseFreq) {
  return coord * baseFreq + vec2(0.0078125);
}
