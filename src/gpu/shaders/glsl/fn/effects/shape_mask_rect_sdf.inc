// Returns the signed distance to a rectangle.
//   p         the point in content pixels, relative to the rectangle center
//   halfSize  the half size on x and y, in content pixels
//   return    the distance in content pixels, positive outside and negative inside
//
// The mask helpers work in content pixels rather than the sigma-normalized space the blur forms
// use.
//@function shapeMaskRectSDF
//@op ShapeMaskRectSDF
//@frag
highp float shapeMaskRectSDF(highp vec2 p /*@in*/, highp vec2 halfSize /*@in*/) {
  highp vec2 d = abs(p) - halfSize;
  return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
}
