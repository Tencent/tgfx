// One corner's noise gradient dot: the packed lattice texel decodes to a gradient in [-1, 1].
//@function tgfxNoiseGradDot
float tgfxNoiseGradDot(vec4 lattice, highp vec2 fractVal, highp vec2 offset) {
  return dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0), fractVal - offset);
}
