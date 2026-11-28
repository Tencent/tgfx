// Computes the closed-form coverage of a rectangle.
//   coord     the sigma-normalized point, relative to the rectangle center
//   halfSize  the sigma-normalized half size on x and y
//   return    the blurred coverage at that point, in [0, 1]
//
// The blurred coverage of a rectangle spanning [x0, x1] x [y0, y1] is the product of one CDF
// difference per axis:
//   C(p) = (CDF(px - x0) - CDF(px - x1)) * (CDF(py - y0) - CDF(py - y1))
// The rectangle is centered at the origin, so x0 = -halfSize.x, x1 = +halfSize.x: the arguments
// below are px - x0 = lo.x and px - x1 = hi.x, and likewise for y.
//@function shapeBlurRectCoverage
//@op ShapeBlurRectCoverage
//@frag
highp float shapeBlurRectCoverage(highp vec2 coord /*@in*/, highp vec2 halfSize /*@in*/) {
  highp vec2 lo = coord + halfSize;
  highp vec2 hi = coord - halfSize;
  return (shapeBlurCDF(lo.x) - shapeBlurCDF(hi.x)) * (shapeBlurCDF(lo.y) - shapeBlurCDF(hi.y));
}
