// Hairline line edge coverage, shared by HairlineLineShader and the pointwise chain kernel's
// GP_LAYOUT==2 path. The math is verbatim from GLSLHairlineLineGeometryProcessor::emitCode
// (edge alpha, the non-AA step, then the Coverage scale); keep it character-identical so the
// precompiled path stays byte-exact with the runtime.
float hairlineLineEdgeCoverage(float edgeDistance, float coverageScale, int aaEnabled) {
  highp float edgeAlpha = abs(edgeDistance);
  edgeAlpha = clamp(edgeAlpha, 0.0, 1.0);
  if (aaEnabled == 0) {
    edgeAlpha = edgeAlpha >= 0.5 ? 1.0 : 0.0;
  }
  return coverageScale * edgeAlpha;
}
