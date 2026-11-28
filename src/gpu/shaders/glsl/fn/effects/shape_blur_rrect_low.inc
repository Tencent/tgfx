// Computes the coverage of a rounded rectangle with the low quadrature count.
//   coord     the sigma-normalized point, relative to the shape center
//   halfSize  the sigma-normalized half size on x and y
//   corner    the sigma-normalized corner semi-axes on x and y
//   return    the blurred coverage at that point, in [0, 1]
//
// The coverage is a nested integral over the kernel g, with w(y) the shape's half width on the
// slice at y:
//   C(p) = int [ int_{-w(y)}^{+w(y)} g(px - x) dx ] * g(py - y) dy
// The inner integral is closed form (shapeBlurSliceCoverage). The outer one is a fixed
// midpoint sum over N quadrature points s_i = start + (i + 0.5) * stepSize, where slice_i (the
// code's span) is the inner integral on the slice offset s_i from p, and
// weight_i = exp(-0.5 * s_i^2); the sum is rescaled by the exact window mass:
//   C(p) = [sum_i weight_i * slice_i] * (CDF(end) - CDF(start)) / sum_i weight_i
//
// The quadrature axis follows the smaller coordinate component so the outer interval is more
// likely to span the full kernel window, which lowers the quadrature error. Swapping the axes
// only reorders the two integrals, so the choice affects that error and not the value being
// approximated.
//
// Coverage is always 1 inside the shape inset by the kernel support of 2, so the quadrature can
// be skipped there. Subtracting the corner keeps this box test inside that inset shape.
//
// The count is a compile-time constant, not a parameter: the ES constant-loop rules keep the
// bound a literal, so the low and high counts are two separate functions the caller picks by
// shape.
//@function shapeBlurRRectCoverageLow
//@op ShapeBlurRRectCoverageLow
//@frag
highp float shapeBlurRRectCoverageLow(highp vec2 coord /*@in*/, highp vec2 halfSize /*@in*/,
                                      highp vec2 corner /*@in*/) {
  if (all(lessThan(abs(coord), halfSize - (corner + vec2(2.0))))) {
    return 1.0;
  }

  highp float lo;
  highp float hi;
  bool swapAxes;
  if (abs(coord.x) > abs(coord.y)) {
    lo = coord.y - halfSize.y;
    hi = coord.y + halfSize.y;
    swapAxes = false;
  } else {
    lo = coord.x - halfSize.x;
    hi = coord.x + halfSize.x;
    swapAxes = true;
  }
  highp float start = min(max(-2.0, lo), hi);
  highp float end = min(max(2.0, lo), hi);
  if (start == end) {
    return 0.0;
  }
  highp float stepSize = (end - start) / 4.0;
  highp float s = start + stepSize * 0.5;
  highp float accum = 0.0;
  highp float weightSum = 0.0;
  for (int i = 0; i < 4; i++) {
    highp float weight = exp(-0.5 * s * s);
    highp float span;
    if (swapAxes) {
      span = shapeBlurSliceCoverage(coord.y, coord.x - s, corner.y, corner.x, halfSize.y,
                                    halfSize.x);
    } else {
      span = shapeBlurSliceCoverage(coord.x, coord.y - s, corner.x, corner.y, halfSize.x,
                                    halfSize.y);
    }
    accum += span * weight;
    weightSum += weight;
    s += stepSize;
  }
  return accum * (shapeBlurCDF(end) - shapeBlurCDF(start)) / weightSum;
}
