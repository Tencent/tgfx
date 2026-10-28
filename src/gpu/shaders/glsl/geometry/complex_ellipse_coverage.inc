// Complex rounded-rect (per-corner radii) edge coverage, shared by ComplexEllipseFillShader and the
// pointwise chain kernel's GP_LAYOUT==3 (fill) / GP_LAYOUT==4 (stroke) paths. The math is verbatim
// from GLSLComplexEllipseGeometryProcessor::emitCode (offset-based region dispatch: corner ellipse
// distance, linear edge distance, full interior; then the inner curve for strokes); keep it
// character-identical so the precompiled path stays byte-exact with the runtime. Every caller
// passes a compile-time constant for stroke, so the unused branch folds away; strokeWidth is only
// read when stroke is set.
float complexEllipseEdgeCoverage(vec2 ellipseOffsets, vec4 ellipseRadii, vec2 edgeDist,
                                 vec2 strokeWidth, bool stroke) {
  highp vec2 offset = ellipseOffsets;
  highp vec2 eDist = edgeDist;

  bool hasXCurve = offset.x > 0.0;
  bool hasYCurve = offset.y > 0.0;

  highp float outerAlpha;
  if (hasXCurve && hasYCurve) {
    highp vec2 safeOffset = max(offset, vec2(1.0 / 4096.0));
    if (stroke) {
      safeOffset *= ellipseRadii.xy;
    }
    highp float test = dot(safeOffset, safeOffset) - 1.0;
    highp vec2 grad = 2.0 * safeOffset * ellipseRadii.xy;
    highp float gradDot = max(dot(grad, grad), 1.1755e-38);
    highp float invlen = inversesqrt(gradDot);
    outerAlpha = clamp(0.5 - test * invlen, 0.0, 1.0);
  } else if (hasYCurve) {
    outerAlpha = clamp(0.5 + eDist.y, 0.0, 1.0);
  } else if (hasXCurve) {
    outerAlpha = clamp(0.5 + eDist.x, 0.0, 1.0);
  } else {
    outerAlpha = 1.0;
  }

  if (stroke) {
    highp vec2 sw = strokeWidth;
    highp float innerAlpha;
    if (hasXCurve && hasYCurve) {
      highp vec2 innerOffset = max(offset, vec2(1.0 / 4096.0)) * ellipseRadii.zw;
      highp float innerTest = dot(innerOffset, innerOffset) - 1.0;
      highp vec2 innerGrad = 2.0 * innerOffset * ellipseRadii.zw;
      highp float innerGradDot = max(dot(innerGrad, innerGrad), 1.1755e-38);
      highp float innerInvlen = inversesqrt(innerGradDot);
      innerAlpha = clamp(0.5 + innerTest * innerInvlen, 0.0, 1.0);
    } else if (hasYCurve) {
      innerAlpha = clamp(0.5 - (eDist.y - 2.0 * sw.y), 0.0, 1.0);
    } else if (hasXCurve) {
      innerAlpha = clamp(0.5 - (eDist.x - 2.0 * sw.x), 0.0, 1.0);
    } else {
      highp float innerEdgeX = eDist.x - 2.0 * sw.x;
      highp float innerEdgeY = eDist.y - 2.0 * sw.y;
      innerAlpha = clamp(0.5 - min(innerEdgeX, innerEdgeY), 0.0, 1.0);
    }
    outerAlpha *= innerAlpha;
  }
  return outerAlpha;
}
