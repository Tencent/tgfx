/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "GLSLSmoothRRectGeometryProcessor.h"
#include <utility>
#include "core/utils/Log.h"
#include "gpu/ShaderBuilder.h"
#include "gpu/glsl/GLSLSmoothCorner.h"

namespace tgfx {
class SmoothCornerBaseFunctions {
 public:
  SmoothCornerBaseFunctions(ShaderBuilder* builder, int newtonIterations, bool needNormal)
      : builder(builder), newtonIterations(newtonIterations), needNormal(needNormal) {
  }

  const std::string& transitionImplicit() {
    if (transitionImplicitFunc.empty()) {
      transitionImplicitFunc = EmitTransitionImplicitFunction(builder);
    }
    return transitionImplicitFunc;
  }

  const std::string& ellipseImplicit() {
    if (ellipseImplicitFunc.empty()) {
      ellipseImplicitFunc = EmitEllipseImplicitFunction(builder);
    }
    return ellipseImplicitFunc;
  }

  const std::string& bezierNewtonDist() {
    if (bezierNewtonDistFunc.empty()) {
      bezierNewtonDistFunc =
          EmitCubicBezierNewtonDistFunction(builder, newtonIterations, needNormal);
    }
    return bezierNewtonDistFunc;
  }

  const std::string& ellipseSampsonDist() {
    if (ellipseSampsonDistFunc.empty()) {
      ellipseSampsonDistFunc = EmitEllipseSampsonDistFunction(builder);
    }
    return ellipseSampsonDistFunc;
  }

 private:
  ShaderBuilder* builder = nullptr;
  int newtonIterations = 0;
  bool needNormal = false;
  std::string transitionImplicitFunc;
  std::string ellipseImplicitFunc;
  std::string bezierNewtonDistFunc;
  std::string ellipseSampsonDistFunc;
};

/**
 * Emits the signed distance functions of smoothed circular and elliptical corners.
 * The corner contour C consists of the incoming transition curve C1, a retained circular or
 * elliptical arc C2, and the outgoing transition curve C3, running from the x-axis edge toward the
 * y-axis edge. B0 to B3 denote the control points of a curve along the direction of travel. PS is
 * the start of C2, where C1 ends, and PE is the end of C2, where C3 starts. TS and TE are the
 * tangent vectors at PS and PE along the direction of travel, and NS and NE are the normals at PS
 * and PE. The GLSL locals b0 to b3, ps and pe hold B0 to B3, PS and PE.
 * fs = -dot(q - PS, TS) and fe = -dot(q - PE, TE), so each is positive when q lies behind NS or NE
 * along the direction of travel. NS and NE divide the plane into four regions S1 to S4, which fs
 * and fe determine. S1 and S3 contain C1 and C3, S2 contains C2, and S4 contains no contour
 * segment.
 * The functions have the signatures:
 *   float circleName(vec2 q, float radius, vec4 coeffs, vec3 arcCoeffs);
 *   float ellipseName(vec2 q, vec2 radii, vec4 coeffs, vec3 arcCoeffs);
 *     q          the point in the corner frame, with the vertex at the origin and the x/y axes
 *                pointing into the shape along the edges
 *     radius     the radius of the circular corner
 *     radii      the radii on x and y of the elliptical corner
 *     coeffs     the coefficients of the corner contour with unit radius: x is transitionStart,
 *                the distance from the vertex to B0 of C1 along the x axis; y is step, the
 *                control-point spacing; z and w are arcOffsetAlong and arcOffsetCross, the offsets
 *                from B2 of C1 to PS. C1's control points are B0 = (transitionStart, 0),
 *                B1 = (transitionStart - 2*step, 0), B2 = (b2x, 0) and
 *                B3 = PS = (b2x - arcOffsetAlong, arcOffsetCross), where
 *                b2x = transitionStart - 3*step. C3 is C1 reflected across y = x. The elliptical
 *                corner scales both per axis by radii.
 *     arcCoeffs  the coefficients of C2: x and y are the sine and cosine of the angle the tangent
 *                turns through along C1, and z is the handle length with unit radius of the cubic
 *                Bezier curve that approximates C2
 * They return the signed distance from q to C, negative inside. Non-antialiased fills only need
 * the inside/outside sign, so for them the functions evaluate the implicit equations of the
 * segments other than the circular C2 and return a value with the same sign instead. When
 * unscaledGeometry and antiAlias are both true, they return a vec3 instead, whose yz carries the
 * unnormalized contour normal at the nearest point.
 * @param builder the shader builder that receives the functions
 * @param stroke whether the shape is stroked
 * @param unscaledGeometry whether the geometry keeps the unscaled local frame
 * @param antiAlias whether coverage is antialiased
 * @return the mangled names of the circular and elliptical corner functions
 */
static std::pair<std::string, std::string> EmitCornerFunctions(ShaderBuilder* builder, bool stroke,
                                                               bool unscaledGeometry,
                                                               bool antiAlias) {
  // Fills always bake the axis scales into the geometry.
  DEBUG_ASSERT(stroke || !unscaledGeometry);

  const auto nonAAFill = !stroke && !antiAlias;
  // Converting unscaled distances to the scaled frame of the AA ramp needs the contour normal.
  const auto needNormal = unscaledGeometry && antiAlias;
  const std::string returnType = needNormal ? "highp vec3 " : "highp float ";
  // Strokes need accurate distances farther from the contour than fills, which only read the
  // antialiasing ramp. The iteration error grows with that distance, so strokes take one more
  // step, which keeps the measured error subpixel within the range the stroke dispatch allows.
  SmoothCornerBaseFunctions baseFunctions(builder, stroke ? 3 : 2, needNormal);

  // In S2, the nearest point on C2 lies along the radial direction, so the distance is the
  // distance to the circle center minus the radius. It is exact and cheap, so non-antialiased
  // fills use it as well.
  // clang-format off
  const auto circleArcCode = needNormal ? R"(
    highp vec2 arcDir = q - vec2(radius);
    return vec3(length(arcDir) - radius, arcDir);
  )" : R"(
    highp vec2 arcDir = q - vec2(radius);
    return length(arcDir) - radius;
  )";
  // clang-format on

  std::string circleTransitionCode;
  if (nonAAFill) {
    const auto& transitionFunc = baseFunctions.transitionImplicit();
    // C1 and C3 mirror each other across the corner bisector, so C3 is evaluated as C1 at the
    // swapped point.
    // clang-format off
    circleTransitionCode = R"(
  return )" + transitionFunc + R"((c1 ? q : q.yx, transitionStart, step, coeffs.z * radius,
                                   coeffs.w * radius);)";
    // clang-format on
  } else {
    const auto& transitionFunc = baseFunctions.bezierNewtonDist();
    // clang-format off
    circleTransitionCode = R"(
  return )" + transitionFunc + R"((q, b0, b1, b2, b3);)";
    // clang-format on
  }

  // Computes the signed distance to the smoothed circular corner, negative inside.
  // S1, S2 and S3 evaluate C1, C2 and C3 respectively. S4 also uses a transition curve: C1 is
  // nearer when q.x >= q.y, and C3 is nearer otherwise, since C1 and C3 mirror each other across
  // y = x.
  // Near full smoothness NS and NE nearly coincide, and rounding can classify S4
  // fragments beyond the circle center into S2. S2 lies entirely on the vertex side of the center,
  // so S2 also requires q.x <= radius.
  const auto circleCornerFunc = builder->getMangledFunctionName("smoothCornerDist");
  // clang-format off
  builder->addFunction(
returnType + circleCornerFunc + R"((highp vec2 q, highp float radius, highp vec4 coeffs,
                                    highp vec3 arcCoeffs) {
  highp float turnSin = arcCoeffs.x;
  highp float turnCos = arcCoeffs.y;
  highp float transitionStart = coeffs.x * radius;
  highp float step = coeffs.y * radius;
  highp float b2x = transitionStart - 3.0 * step;
  highp vec2 ps = vec2(b2x - coeffs.z * radius, coeffs.w * radius);
  highp vec2 pe = ps.yx;
  highp float fs = turnCos * (q.x - ps.x) - turnSin * (q.y - ps.y);
  highp float fe = turnSin * (q.x - pe.x) - turnCos * (q.y - pe.y);
  if (fs <= 0.0 && fe > 0.0 && q.x <= radius) {)" +
    circleArcCode + R"(
  }
  bool c1 = fs > 0.0 && (fe > 0.0 || q.x >= q.y);
  highp vec2 b0 = c1 ? vec2(transitionStart, 0.0) : pe;
  highp vec2 b1 = c1 ? vec2(transitionStart - 2.0 * step, 0.0) : vec2(0.0, b2x);
  highp vec2 b2 = c1 ? vec2(b2x, 0.0) : vec2(0.0, transitionStart - 2.0 * step);
  highp vec2 b3 = c1 ? ps : vec2(0.0, transitionStart);)" +
  circleTransitionCode + R"(
}
)");
  // clang-format on

  std::string ellipseArcCode;
  if (stroke) {
    const auto& arcFunc = baseFunctions.bezierNewtonDist();
    // Approximate the elliptical C2 with a cubic Bezier curve and evaluate its distance.
    // clang-format off
    ellipseArcCode = R"(
    highp vec2 arcCtrl1 = ps + arcCoeffs.z * vec2(-turnCos, turnSin) * radii;
    highp vec2 arcCtrl2 = pe - arcCoeffs.z * vec2(-turnSin, turnCos) * radii;
    return )" + arcFunc + R"((q, ps, arcCtrl1, arcCtrl2, pe);
  )";
    // clang-format on
  } else {
    // An antialiased fill only needs accurate distances within the antialiasing ramp. The Sampson
    // distance is cheaper and approaches the true distance near the ellipse, so it stays accurate
    // enough there.
    const auto& arcFunc =
        nonAAFill ? baseFunctions.ellipseImplicit() : baseFunctions.ellipseSampsonDist();
    // clang-format off
    ellipseArcCode = R"(
    return )" + arcFunc + R"((q - radii, radii);
  )";
    // clang-format on
  }

  std::string ellipseTransitionCode;
  if (nonAAFill) {
    const auto& transitionFunc = baseFunctions.transitionImplicit();
    // C3 mirrors across the corner bisector the C1 built with the radii swapped, so it is evaluated
    // as that C1 at the swapped point.
    // clang-format off
    ellipseTransitionCode = R"(
  highp vec2 axes = c1 ? radii : radii.yx;
  return )" + transitionFunc + R"((c1 ? q : q.yx, coeffs.x * axes.x, coeffs.y * axes.x,
                                   coeffs.z * axes.x, coeffs.w * axes.y);)";
    // clang-format on
  } else {
    const auto& transitionFunc = baseFunctions.bezierNewtonDist();
    // clang-format off
    ellipseTransitionCode = R"(
  return )" + transitionFunc + R"((q, b0, b1, b2, b3);)";
    // clang-format on
  }

  // Computes the signed distance to the smoothed elliptical corner, negative inside.
  // S1, S2 and S3 evaluate C1, C2 and C3 respectively. S4 also uses a transition curve: C1 is
  // nearer when NE is nearer than NS, and C3 is nearer otherwise. TS and TE are not unit vectors,
  // so the distances to NS and NE are |fs| / |TS| and |fe| / |TE|, compared squared.
  // Near full smoothness NS and NE nearly coincide, and rounding can classify S4
  // fragments beyond their intersection into S2. S2 lies entirely on the vertex side of the
  // intersection, so S2 also requires q.x <= normalIntersectX, the x coordinate of the
  // intersection.
  const auto ellipseCornerFunc = builder->getMangledFunctionName("smoothCornerDistEllipse");
  // clang-format off
  builder->addFunction(
returnType + ellipseCornerFunc + R"((highp vec2 q, highp vec2 radii, highp vec4 coeffs,
                                     highp vec3 arcCoeffs) {
  highp float turnSin = arcCoeffs.x;
  highp float turnCos = arcCoeffs.y;
  highp float rx = radii.x;
  highp float ry = radii.y;
  highp float rxSq = rx * rx;
  highp float rySq = ry * ry;
  highp vec2 transitionStart = coeffs.x * radii;
  highp vec2 step = coeffs.y * radii;
  highp vec2 b2xy = transitionStart - 3.0 * step;
  highp vec2 ps = vec2(b2xy.x - coeffs.z * rx, coeffs.w * ry);
  highp vec2 pe = vec2(coeffs.w * rx, b2xy.y - coeffs.z * ry);
  highp float fs = rx * turnCos * (q.x - ps.x) - ry * turnSin * (q.y - ps.y);
  highp float fe = rx * turnSin * (q.x - pe.x) - ry * turnCos * (q.y - pe.y);
  highp float normalIntersectX =
      rx + (rySq - rxSq) * turnSin * turnCos / ((turnSin + turnCos) * rx);
  if (fs <= 0.0 && fe > 0.0 && q.x <= normalIntersectX) {)" +
    ellipseArcCode + R"(
  }
  highp float tsLengthSq = rySq + (rxSq - rySq) * turnCos * turnCos;
  highp float teLengthSq = rxSq + rySq - tsLengthSq;
  bool c1 = fs > 0.0 && (fe > 0.0 || fe * fe * tsLengthSq <= fs * fs * teLengthSq);
  highp vec2 b0 = c1 ? vec2(transitionStart.x, 0.0) : pe;
  highp vec2 b1 = c1 ? vec2(transitionStart.x - 2.0 * step.x, 0.0) : vec2(0.0, b2xy.y);
  highp vec2 b2 = c1 ? vec2(b2xy.x, 0.0) : vec2(0.0, transitionStart.y - 2.0 * step.y);
  highp vec2 b3 = c1 ? ps : vec2(0.0, transitionStart.y);)" +
  ellipseTransitionCode + R"(
}
)");
  // clang-format on

  return {circleCornerFunc, ellipseCornerFunc};
}

PlacementPtr<SmoothRRectGeometryProcessor> SmoothRRectGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, bool antiAlias,
    std::optional<PMColor> commonColor, bool stroke, bool unscaledGeometry) {
  return allocator->make<GLSLSmoothRRectGeometryProcessor>(width, height, antiAlias, commonColor,
                                                           stroke, unscaledGeometry);
}

GLSLSmoothRRectGeometryProcessor::GLSLSmoothRRectGeometryProcessor(
    int width, int height, bool antiAlias, std::optional<PMColor> commonColor, bool stroke,
    bool unscaledGeometry)
    : SmoothRRectGeometryProcessor(width, height, antiAlias, commonColor, stroke,
                                   unscaledGeometry) {
}

void GLSLSmoothRRectGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;
  const auto aaUnscaledGeometry = unscaledGeometry && antiAlias;

  // Attributes
  varyingHandler->emitAttributes(*this);
  auto localCoordVarying = varyingHandler->addVarying("localCoord", SLType::Float2);
  vertBuilder->codeAppendf("%s = %s;", localCoordVarying.vsOut().c_str(),
                           inLocalCoord.name().c_str());
  auto xRadiiVarying = varyingHandler->addVarying("xRadii", SLType::Float4, true);
  vertBuilder->codeAppendf("%s = %s;", xRadiiVarying.vsOut().c_str(), inXRadii.name().c_str());
  auto yRadiiVarying = varyingHandler->addVarying("yRadii", SLType::Float4, true);
  vertBuilder->codeAppendf("%s = %s;", yRadiiVarying.vsOut().c_str(), inYRadii.name().c_str());
  auto boundsVarying = varyingHandler->addVarying("rectBounds", SLType::Float4, true);
  vertBuilder->codeAppendf("%s = %s;", boundsVarying.vsOut().c_str(), inRectBounds.name().c_str());
  Varying halfStrokeWidthVarying;
  if (stroke) {
    halfStrokeWidthVarying = varyingHandler->addVarying("halfStrokeWidth", SLType::Float, true);
    vertBuilder->codeAppendf("%s = %s;", halfStrokeWidthVarying.vsOut().c_str(),
                             inHalfStrokeWidth.name().c_str());
  }
  Varying invScaleVarying;
  if (aaUnscaledGeometry) {
    invScaleVarying = varyingHandler->addVarying("invScale", SLType::Float2, true);
    vertBuilder->codeAppendf("%s = %s;", invScaleVarying.vsOut().c_str(),
                             inInvScale.name().c_str());
  }

  // Color
  if (commonColor.has_value()) {
    auto colorName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  } else {
    auto color = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", color.vsOut().c_str(), inColor.name().c_str());
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), color.fsIn().c_str());
  }

  // Position
  vertBuilder->emitNormalizedPosition(inPosition.name());

  // Corner contour coefficients
  // Precompute the per-shape coefficients to avoid recomputing them per fragment.
  auto curveCoeffsVarying = varyingHandler->addVarying("curveCoeffs", SLType::Float4, true);
  auto arcCoeffsVarying = varyingHandler->addVarying("arcCoeffs", SLType::Float3, true);
  vertBuilder->codeAppendf("highp float smoothness = clamp(%s, 0.0, 1.0);",
                           inSmoothness.name().c_str());
  vertBuilder->codeAppend("highp float turnAngle = 3.14159265359 * 0.25 * smoothness;");
  vertBuilder->codeAppend("highp float turnSin = sin(turnAngle);");
  vertBuilder->codeAppend("highp float turnCos = cos(turnAngle);");
  vertBuilder->codeAppend("highp float tanTurn = tan(turnAngle);");
  vertBuilder->codeAppend("highp float start = 1.0 + smoothness;");
  vertBuilder->codeAppend("highp float arcOffsetAlong = tan(turnAngle * 0.5) * turnCos;");
  vertBuilder->codeAppend("highp float step = (smoothness + turnSin - arcOffsetAlong) / 3.0;");
  vertBuilder->codeAppend("highp float arcOffsetCross = arcOffsetAlong * tanTurn;");
  vertBuilder->codeAppendf("%s = vec4(start, step, arcOffsetAlong, arcOffsetCross);",
                           curveCoeffsVarying.vsOut().c_str());
  vertBuilder->codeAppend("highp float arcSweep = 3.14159265359 * 0.5 * (1.0 - smoothness);");
  vertBuilder->codeAppend("highp float arcHandle = (4.0 / 3.0) * tan(arcSweep * 0.25);");
  vertBuilder->codeAppendf("%s = vec3(turnSin, turnCos, arcHandle);",
                           arcCoeffsVarying.vsOut().c_str());

  // Transforms
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                 ShaderVar(inPosition.name(), SLType::Float2));

  // Coverage
  const auto [circleCornerFunc, ellipseCornerFunc] =
      EmitCornerFunctions(fragBuilder, stroke, unscaledGeometry, antiAlias);
  fragBuilder->codeAppendf("highp vec4 coeffs = %s;", curveCoeffsVarying.fsIn().c_str());
  fragBuilder->codeAppendf("highp vec3 arcCoeffs = %s;", arcCoeffsVarying.fsIn().c_str());
  fragBuilder->codeAppendf("highp vec2 localCoord = %s;", localCoordVarying.fsIn().c_str());
  fragBuilder->codeAppendf("highp vec4 xRadii = %s;", xRadiiVarying.fsIn().c_str());
  fragBuilder->codeAppendf("highp vec4 yRadii = %s;", yRadiiVarying.fsIn().c_str());
  fragBuilder->codeAppendf("highp vec4 bounds = %s;", boundsVarying.fsIn().c_str());

  // Compute the fragment position in each corner frame, with the vertex at the origin and the x/y
  // axes pointing into the shape along the edges. Order matches the radii: TL, TR, BR, BL.
  fragBuilder->codeAppend("highp vec4 cornersX = vec4(bounds.x, bounds.z, bounds.z, bounds.x);");
  fragBuilder->codeAppend("highp vec4 cornersY = vec4(bounds.y, bounds.y, bounds.w, bounds.w);");
  fragBuilder->codeAppend(
      "highp vec4 dx = (vec4(localCoord.x) - cornersX) * vec4(1.0, -1.0, -1.0, 1.0);");
  fragBuilder->codeAppend(
      "highp vec4 dy = (vec4(localCoord.y) - cornersY) * vec4(1.0, 1.0, -1.0, -1.0);");
  // inCorner is 1.0 for each corner whose region, extended by smoothing to transitionStart along
  // both edges, contains the fragment. RRect limits the transitionStart sum of the two corners on
  // each edge to the edge length, so adjacent regions never overlap.
  fragBuilder->codeAppend(
      "vec4 inCorner = step(dx, xRadii * coeffs.x) * step(dy, yRadii * coeffs.x);");
  // A zero-radius corner has no corner region, but step() still accepts fragments past its vertex,
  // so clear them.
  fragBuilder->codeAppend(
      "inCorner *= vec4(greaterThan(xRadii, vec4(0.0))) * vec4(greaterThan(yRadii, vec4(0.0)));");
  // One-hot collapse: when adjacent corner regions share a fragment along their boundary, keep
  // the first hit so subsequent queries collapse to a single corner.
  fragBuilder->codeAppend(
      "vec4 prefixSum = vec4(0.0, inCorner.x, inCorner.x + inCorner.y, "
      "inCorner.x + inCorner.y + inCorner.z);");
  fragBuilder->codeAppend("inCorner *= step(prefixSum, vec4(0.5));");
  fragBuilder->codeAppend("highp float rx = dot(inCorner, xRadii);");

  // Fragments in a corner region have a positive rx and take the distance to the corner contour;
  // other fragments take the distance to the nearest of the four edges.
  if (aaUnscaledGeometry) {
    fragBuilder->codeAppend("highp float dist;");
    fragBuilder->codeAppend("highp vec2 normal;");
    fragBuilder->codeAppend("if (rx > 0.0) {");
    fragBuilder->codeAppend("highp float ry = dot(inCorner, yRadii);");
    fragBuilder->codeAppend("highp vec2 q = vec2(dot(inCorner, dx), dot(inCorner, dy));");
    fragBuilder->codeAppend("bool isCircle = abs(rx - ry) <= 1.0 / 4096.0;");
    fragBuilder->codeAppendf(
        "highp vec3 distNormal = isCircle ? %s(q, rx, coeffs, arcCoeffs) : "
        "%s(q, vec2(rx, ry), coeffs, arcCoeffs);",
        circleCornerFunc.c_str(), ellipseCornerFunc.c_str());
    fragBuilder->codeAppend("dist = distNormal.x;");
    fragBuilder->codeAppend("normal = distNormal.yz;");
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppend("highp vec2 d0 = localCoord - bounds.xy;");
    fragBuilder->codeAppend("highp vec2 d1 = bounds.zw - localCoord;");
    fragBuilder->codeAppend("highp float edgeX = min(d0.x, d1.x);");
    fragBuilder->codeAppend("highp float edgeY = min(d0.y, d1.y);");
    fragBuilder->codeAppend("dist = -min(edgeX, edgeY);");
    // Only the length of invScale * normal is used, so the normal's sign does not matter.
    fragBuilder->codeAppend("normal = edgeX <= edgeY ? vec2(1.0, 0.0) : vec2(0.0, 1.0);");
    fragBuilder->codeAppend("}");
    if (stroke) {
      fragBuilder->codeAppendf("dist = abs(dist) - %s;", halfStrokeWidthVarying.fsIn().c_str());
    }
    // The distances so far are in the unscaled local frame. Convert to the scaled local frame,
    // where the coverage ramp is defined: scaled distance = local distance /
    // |invScale * unitNormal|.
    fragBuilder->codeAppend("highp float normalLength = max(length(normal), 1.1755e-38);");
    fragBuilder->codeAppendf("dist /= length(%s * normal) / normalLength;",
                             invScaleVarying.fsIn().c_str());
  } else {
    fragBuilder->codeAppend("highp float dist;");
    fragBuilder->codeAppend("if (rx > 0.0) {");
    fragBuilder->codeAppend("highp float ry = dot(inCorner, yRadii);");
    fragBuilder->codeAppend("highp vec2 q = vec2(dot(inCorner, dx), dot(inCorner, dy));");
    fragBuilder->codeAppend("bool isCircle = abs(rx - ry) <= 1.0 / 4096.0;");
    fragBuilder->codeAppendf(
        "dist = isCircle ? %s(q, rx, coeffs, arcCoeffs) : %s(q, vec2(rx, ry), coeffs, arcCoeffs);",
        circleCornerFunc.c_str(), ellipseCornerFunc.c_str());
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppend("highp vec2 d0 = localCoord - bounds.xy;");
    fragBuilder->codeAppend("highp vec2 d1 = bounds.zw - localCoord;");
    fragBuilder->codeAppend("dist = -min(min(d0.x, d0.y), min(d1.x, d1.y));");
    fragBuilder->codeAppend("}");
    if (stroke) {
      fragBuilder->codeAppendf("dist = abs(dist) - %s;", halfStrokeWidthVarying.fsIn().c_str());
    }
  }

  if (antiAlias) {
    fragBuilder->codeAppend("highp float coverage = clamp(0.5 - dist, 0.0, 1.0);");
  } else {
    fragBuilder->codeAppend("highp float coverage = step(dist, 0.0);");
  }
  fragBuilder->codeAppendf("%s = vec4(coverage);", args.outputCoverage.c_str());
}

void GLSLSmoothRRectGeometryProcessor::setData(UniformData* vertexUniformData,
                                               UniformData* fragmentUniformData,
                                               FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}
}  // namespace tgfx