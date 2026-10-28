// Ellipse edge coverage, shared by EllipseFillShader and the pointwise chain kernel's
// GP_LAYOUT==1 path. Evaluates the outer curve (and the inner curve for strokes) from the
// ellipse varyings. The math is verbatim from the runtime EllipseGeometryProcessor emission;
// keep it character-identical so the precompiled path stays byte-exact with JIT.
float ellipseEdgeCoverage(vec2 ellipseOffsets, vec4 ellipseRadii, int strokeEnabled) {
  highp vec2 offset = ellipseOffsets;
  if (strokeEnabled != 0) {
    offset *= ellipseRadii.xy;
  }
  highp float test = dot(offset, offset) - 1.0;
  highp vec2 grad = 2.0 * offset * ellipseRadii.xy;
  highp float gradDot = dot(grad, grad);
  gradDot = max(gradDot, 1.1755e-38);
  highp float invlen = inversesqrt(gradDot);
  highp float edgeAlpha = clamp(0.5 - test * invlen, 0.0, 1.0);

  if (strokeEnabled != 0) {
    highp vec2 innerOffset = ellipseOffsets * ellipseRadii.zw;
    highp float innerTest = dot(innerOffset, innerOffset) - 1.0;
    highp vec2 innerGrad = 2.0 * innerOffset * ellipseRadii.zw;
    highp float innerGradDot = dot(innerGrad, innerGrad);
    innerGradDot = max(innerGradDot, 1.1755e-38);
    highp float innerInvlen = inversesqrt(innerGradDot);
    edgeAlpha *= clamp(0.5 + innerTest * innerInvlen, 0.0, 1.0);
  }
  return edgeAlpha;
}
