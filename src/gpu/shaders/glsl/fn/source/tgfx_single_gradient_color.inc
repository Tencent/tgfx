// The single-interval gradient color: a straight lerp between the two stops.
//@function tgfxSingleGradientColor
//@op SingleGradientColor
//@frag
vec4 tgfxSingleGradientColor(float t /*@in*/, vec4 start, vec4 end) {
  return (1.0 - t) * start + t * end;
}
