// Hairline line edge coverage, shared by the HairlineLine family and the pointwise chain kernel's
// GP_LAYOUT==2 path. The math is the edge alpha, the non-AA step, then the Coverage scale; both
// callers share this file so they stay byte-exact with each other.
#ifndef TGSL_HAIRLINE_LINE_COVERAGE_INCLUDED
#define TGSL_HAIRLINE_LINE_COVERAGE_INCLUDED

float hairlineLineEdgeCoverage(float edgeDistance, float coverageScale, int aaEnabled) {
  highp float edgeAlpha = abs(edgeDistance);
  edgeAlpha = clamp(edgeAlpha, 0.0, 1.0);
  if (aaEnabled == 0) {
    edgeAlpha = edgeAlpha >= 0.5 ? 1.0 : 0.0;
  }
  return coverageScale * edgeAlpha;
}


#endif  // TGSL_HAIRLINE_LINE_COVERAGE_INCLUDED
