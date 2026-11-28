// Computes the CDF of the Gaussian truncated to +/- 2 and renormalized over that window.
//   u       the sigma-normalized coordinate
//   return  the fraction of the kernel mass below u, in [0, 1]
//
// Writing Phi for the untruncated CDF, Phi(u) = 0.5 * (1 + erf(u / sqrt(2))), the result is
// (Phi(u) - Phi(-2)) / (Phi(2) - Phi(-2)). Sigma is fixed at 1, so the two endpoint terms are
// constants, written as literals in the code: 0.02275013 is Phi(-2), the tail mass cut off at the
// left end, and 0.95449974 is Phi(2) - Phi(-2), the mass kept inside the window; 0.70710678 is
// 1 / sqrt(2).
//@function shapeBlurCDF
highp float shapeBlurCDF(highp float u) {
  if (u <= -2.0) {
    return 0.0;
  }
  if (u >= 2.0) {
    return 1.0;
  }
  return (0.5 * (1.0 + shapeBlurErf(u * 0.70710678)) - 0.02275013) / 0.95449974;
}
