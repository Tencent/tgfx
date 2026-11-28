// Computes the coverage at a point convolved along one axis alone, bounded by the shape's half
// extent at that point's cross coordinate. "Along" names the axis being convolved and "cross" the
// perpendicular one; the caller decides which shape axis plays each role.
//   alongCoord   the sigma-normalized coordinate on the convolved axis
//   crossCoord   the sigma-normalized coordinate on the perpendicular axis
//   cornerAlong  the sigma-normalized corner semi-axis on the convolved axis
//   cornerCross  the sigma-normalized corner semi-axis on the perpendicular axis
//   halfAlong    the sigma-normalized half size on the convolved axis
//   halfCross    the sigma-normalized half size on the perpendicular axis
//   return       the coverage at that point, in [0, 1]
//
// The shape's half width at cross coordinate c is, with d = |c| - (hc - rc):
//   straight region (d <= 0):  w = ha
//   corner region (d > 0):     w = ha - ra + (ra / rc) * sqrt(rc^2 - d^2)
// and the coverage is CDF(alongCoord + w) - CDF(alongCoord - w). The short names stand for the
// parameters: c = crossCoord, ha = halfAlong, hc = halfCross, ra = cornerAlong, rc = cornerCross.
// In the code, curved is w and delta is min(-d, 0), so the straight region needs no branch:
// delta = 0 makes the sqrt term rc and w collapses to ha.
//
// Callers guarantee cornerAlong <= halfAlong and cornerCross <= halfCross.
//@function shapeBlurSliceCoverage
highp float shapeBlurSliceCoverage(highp float alongCoord, highp float crossCoord,
                                    highp float cornerAlong, highp float cornerCross,
                                    highp float halfAlong, highp float halfCross) {
  highp float delta = min(halfCross - cornerCross - abs(crossCoord), 0.0);
  highp float ratio = cornerAlong / max(cornerCross, 1e-6);
  highp float curved = halfAlong - cornerAlong +
                       ratio * sqrt(max(0.0, cornerCross * cornerCross - delta * delta));
  return shapeBlurCDF(alongCoord + curved) - shapeBlurCDF(alongCoord - curved);
}
