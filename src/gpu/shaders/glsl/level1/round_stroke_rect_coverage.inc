// Round-stroke-rect edge coverage, shared by RoundStrokeRectFillShader's AA and non-AA forms (a
// runtime uniform selects between them). The math is verbatim from the family's geometry emission
// (GLSLRoundStrokeRectGeometryProcessor::emitCode): the normalized offset-space ellipse SDF scales
// the per-vertex coverage inside the ramp band when AA is on, a hard inside step otherwise. A
// caller may pass a runtime value for aa; ellipseRadii and vertexCoverage are only read when it is
// set, so a non-AA draw's filler values are inert.
float roundStrokeRectCoverage(vec2 offsets, vec2 ellipseRadii, float vertexCoverage, bool aa) {
  highp vec2 offset = offsets;
  if (!aa) {
    highp float test = dot(offset, offset);
    return step(test, 1.0);
  }
  highp float test = dot(offset, offset) - 1.0;
  if (test <= -0.5) {
    return vertexCoverage;
  }
  highp vec2 grad = 2.0 * offset * ellipseRadii;
  highp float gradDot = dot(grad, grad);
  gradDot = max(gradDot, 1.1755e-38);
  highp float invlen = inversesqrt(gradDot);
  highp float edgeAlpha = clamp(0.5 - test * invlen, 0.0, 1.0);
  return vertexCoverage * edgeAlpha;
}
