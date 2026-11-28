// Returns the signed distance to a rounded rectangle.
//   p         the point in content pixels, relative to the rectangle center
//   halfSize  the half size on x and y, in content pixels
//   corner    the corner semi-axes on x and y, in content pixels
//   return    the distance in content pixels, positive outside and negative inside
//
// Outside the corner regions the bounding rectangle gives the exact distance. Inside one, d is
// the offset from the corner ellipse center and r its semi-axes, so F = dot(d / r, d / r) - 1
// vanishes on the ellipse; near the boundary F grows as length(grad F) times the distance, so
// F / length(grad F) approximates it. An exact distance to an ellipse has no closed form.
//
// The radii are clamped so a radius larger than the shape degenerates to a stadium rather than
// inverting the inner rectangle, and kept away from zero so the division stays finite.
//@function shapeMaskRRectSDF
//@op ShapeMaskRRectSDF
//@frag
highp float shapeMaskRRectSDF(highp vec2 p /*@in*/, highp vec2 halfSize /*@in*/,
                              highp vec2 corner /*@in*/) {
  highp vec2 r = max(min(corner, halfSize), vec2(1e-6));
  highp vec2 inner = halfSize - r;
  highp vec2 d = abs(p) - inner;
  if (d.x <= 0.0 || d.y <= 0.0) {
    return shapeMaskRectSDF(p, halfSize);
  }
  highp vec2 offset = d / r;
  highp float test = dot(offset, offset) - 1.0;
  highp vec2 grad = 2.0 * offset / r;
  return test / max(length(grad), 1e-20);
}
