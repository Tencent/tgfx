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

#include "GLSLSmoothRRectEffect.h"
#include <algorithm>
#include <utility>
#include "core/RRectUtils.h"
#include "core/SmoothCornerCoeffs.h"
#include "core/utils/Log.h"
#include "gpu/ShaderBuilder.h"
#include "gpu/glsl/GLSLSmoothCorner.h"

namespace tgfx {
/**
 * Emits the signed distance functions of smoothed circular and elliptical corners.
 * Non-antialiased coverage only needs the inside/outside sign and evaluates the implicit equations
 * of the corner segments; antialiased coverage evaluates distances.
 * @param builder the shader builder that receives the functions
 * @return the mangled names of the circular and elliptical corner distance functions
 */
static std::pair<std::string, std::string> EmitSmoothCornerFunctions(ShaderBuilder* builder) {
  // The corner contour consists of an incoming transition curve C1, a retained circular or
  // elliptical arc C2, and an outgoing transition curve C3. The normals at the arc start ps and
  // end pe divide the plane into four regions S1-S4.
  // S1 and S3 contain the transition curves C1 and C3, S2 contains the retained arc C2, and S4
  // contains no contour segment. PS and PE are the C1-C2 and C2-C3 join points. fs and fe are the
  // signed values of the join-point normals evaluated at q and determine the region.

  // Antialiased fills only read distances within the antialiasing ramp, where this iteration count
  // keeps the measured error subpixel.
  const auto transitionDistFunc = EmitCubicBezierNewtonDistFunction(builder, 2, false);
  const auto transitionImplicitFunc = EmitTransitionImplicitFunction(builder);

  // In S2, the nearest point on the retained arc lies along the radial direction, so the distance
  // is the distance to the circle center minus the radius.
  // clang-format off
  const auto circleS2BranchCode = R"(
    highp vec2 arcDir = q - vec2(radius);
    return length(arcDir) - radius;
  )";
  // clang-format on

  // C1 and C3 mirror each other across y = x, so the implicit equation evaluates C3 as C1 at the
  // swapped point. The control points run from the x-axis edge toward the y-axis edge, which the
  // distance sign relies on.
  // clang-format off
  const auto circleS134BranchCode = R"(
  if (!antiAlias) {
    return )" + transitionImplicitFunc + R"((c1 ? q : q.yx, transitionStart, step,
                                             coeffs.z * radius, coeffs.w * radius);
  }
  highp vec2 b0 = c1 ? vec2(transitionStart, 0.0) : pe;
  highp vec2 b1 = c1 ? vec2(transitionStart - 2.0 * step, 0.0) : vec2(0.0, b2x);
  highp vec2 b2 = c1 ? vec2(b2x, 0.0) : vec2(0.0, transitionStart - 2.0 * step);
  highp vec2 b3 = c1 ? ps : vec2(0.0, transitionStart);
  return )" + transitionDistFunc + R"((q, b0, b1, b2, b3);)";
  // clang-format on

  // Computes the signed distance to the smoothed circular corner, negative inside.
  // Non-antialiased coverage evaluates the implicit equations of the transition curves instead, so
  // only the sign of the result is meaningful.
  // S1, S2 and S3 evaluate C1, C2 and C3 respectively. S4 also uses a transition curve: C1 is
  // nearer when q.x >= q.y, and C3 is nearer otherwise, since C1 and C3 mirror each other across
  // y = x.
  // Near full smoothness the join-point normals nearly coincide, and rounding can classify S4
  // fragments beyond the circle center into S2. S2 lies entirely on the vertex side of the center,
  // so S2 also requires q.x <= radius.
  //   q           the point in the corner frame, with the vertex at the origin and the x/y axes
  //               pointing into the shape along the edges
  //   radius      the corner radius
  //   coeffs      coefficients of the unit-radius corner contour:
  //               transitionStart is the distance from the vertex to C1's start along the x axis;
  //               step is the control-point spacing; arcOffsetX/Y are the offsets from B2 to ps.
  //               C1's control points are (transitionStart, 0), (transitionStart - 2*step, 0),
  //               (b2x, 0) and (b2x - arcOffsetX, arcOffsetY), where b2x =
  //               transitionStart - 3*step. C3 is C1 reflected across y = x.
  //   smoothness  the corner smoothness
  //   antiAlias   whether coverage is antialiased
  //   return      the signed distance, or the implicit value for non-antialiased coverage
  const auto cornerDistFunc = builder->getMangledFunctionName("smoothCornerDist");
  // clang-format off
  builder->addFunction(R"(
highp float )" + cornerDistFunc + R"((highp vec2 q, highp float radius, highp vec4 coeffs,
                                      highp float smoothness, bool antiAlias) {
  highp float clampedSmoothness = clamp(smoothness, 0.0, 1.0);
  highp float turnAngle = 3.14159265359 * 0.25 * clampedSmoothness;
  highp float turnSin = sin(turnAngle);
  highp float turnCos = cos(turnAngle);
  highp float transitionStart = coeffs.x * radius;
  highp float step = coeffs.y * radius;
  highp float b2x = transitionStart - 3.0 * step;
  highp vec2 ps = vec2(b2x - coeffs.z * radius, coeffs.w * radius);
  highp vec2 pe = ps.yx;
  highp float fs = turnCos * (q.x - ps.x) - turnSin * (q.y - ps.y);
  highp float fe = turnSin * (q.x - pe.x) - turnCos * (q.y - pe.y);
  if (fs <= 0.0 && fe > 0.0 && q.x <= radius) {)" +
    circleS2BranchCode + R"(
  }
  bool c1 = fs > 0.0 && (fe > 0.0 || q.x >= q.y);)" +
  circleS134BranchCode + R"(
}
)");
  // clang-format on

  // Antialiased coverage only needs accurate distances within the antialiasing ramp. The Sampson
  // distance is a cheaper first-order approximation that stays accurate enough there.
  const auto arcDistFunc = EmitEllipseSampsonDistFunction(builder);
  const auto arcImplicitFunc = EmitEllipseImplicitFunction(builder);
  // clang-format off
  const auto ellipseS2BranchCode = R"(
    return antiAlias ? )" + arcDistFunc + R"((q - radii, radii)
                     : )" + arcImplicitFunc + R"((q - radii, radii);
  )";
  // clang-format on

  // C3 mirrors across y = x the C1 built with the radii swapped, so the implicit equation evaluates
  // it as that C1 at the swapped point. The control points run from the x-axis edge toward the
  // y-axis edge, which the distance sign relies on.
  // clang-format off
  const auto ellipseS134BranchCode = R"(
  if (!antiAlias) {
    highp vec2 axes = c1 ? radii : radii.yx;
    return )" + transitionImplicitFunc + R"((c1 ? q : q.yx, coeffs.x * axes.x, coeffs.y * axes.x,
                                             coeffs.z * axes.x, coeffs.w * axes.y);
  }
  highp vec2 b0 = c1 ? vec2(transitionStart.x, 0.0) : pe;
  highp vec2 b1 = c1 ? vec2(transitionStart.x - 2.0 * step.x, 0.0) : vec2(0.0, b2xy.y);
  highp vec2 b2 = c1 ? vec2(b2xy.x, 0.0) : vec2(0.0, transitionStart.y - 2.0 * step.y);
  highp vec2 b3 = c1 ? ps : vec2(0.0, transitionStart.y);
  return )" + transitionDistFunc + R"((q, b0, b1, b2, b3);)";
  // clang-format on

  // Computes the signed distance to the smoothed elliptical corner, negative inside.
  // Non-antialiased coverage evaluates the implicit equations of the segments instead, so only the
  // sign of the result is meaningful.
  // S1, S2 and S3 evaluate C1, C2 and C3 respectively. S4 also uses a transition curve: C1 is
  // nearer when the normal at pe is nearer than the normal at ps, and C3 is nearer otherwise.
  // The distances to the normals are |fe| and |fs| divided by the lengths of the contour tangent
  // vectors at pe and ps, compared squared.
  // Near full smoothness the join-point normals nearly coincide, and rounding can classify S4
  // fragments beyond their intersection into S2. S2 lies entirely on the vertex side of the
  // intersection, so S2 also requires q.x <= normalIntersectX, the x coordinate of the
  // intersection.
  //   q           the point in the corner frame, with the vertex at the origin and the x/y axes
  //               pointing into the shape along the edges
  //   radii       the corner radii on x and y
  //   coeffs      coefficients of the unit-radius corner contour:
  //               transitionStart is the distance from the vertex to C1's start along the x axis;
  //               step is the control-point spacing; arcOffsetX/Y are the offsets from B2 to ps.
  //               C1's control points are (transitionStart, 0), (transitionStart - 2*step, 0),
  //               (b2x, 0) and (b2x - arcOffsetX, arcOffsetY), where b2x =
  //               transitionStart - 3*step. C3 is C1 reflected across y = x. Both are then
  //               scaled per axis by radii.
  //   smoothness  the corner smoothness
  //   antiAlias   whether coverage is antialiased
  //   return      the signed distance, or the implicit value for non-antialiased coverage
  const auto ellipseDistFunc = builder->getMangledFunctionName("smoothCornerDistEllipse");
  // clang-format off
  builder->addFunction(R"(
highp float )" + ellipseDistFunc + R"((highp vec2 q, highp vec2 radii, highp vec4 coeffs,
                                       highp float smoothness, bool antiAlias) {
  highp float clampedSmoothness = clamp(smoothness, 0.0, 1.0);
  highp float turnAngle = 3.14159265359 * 0.25 * clampedSmoothness;
  highp float turnSin = sin(turnAngle);
  highp float turnCos = cos(turnAngle);
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
    ellipseS2BranchCode + R"(
  }
  highp float tsLengthSq = rySq + (rxSq - rySq) * turnCos * turnCos;
  highp float teLengthSq = rxSq + rySq - tsLengthSq;
  bool c1 = fs > 0.0 && (fe > 0.0 || fe * fe * tsLengthSq <= fs * fs * teLengthSq);)" +
  ellipseS134BranchCode + R"(
}
)");
  // clang-format on

  return {cornerDistFunc, ellipseDistFunc};
}

PlacementPtr<SmoothRRectEffect> SmoothRRectEffect::Make(BlockAllocator* allocator,
                                                        const RRect& rRect, const Matrix& matrix,
                                                        bool antiAlias) {
  DEBUG_ASSERT(rRect.smoothness() > 0.0f);
  // The analytic AA relies on a linear device-to-local inverse map, which cannot represent a
  // perspective matrix.
  DEBUG_ASSERT(!matrix.hasPerspective());
  if (matrix.hasPerspective()) {
    return nullptr;
  }
  // The shader does not handle fragments shared by two corner regions.
  if (RRectUtils::HasDiagonalCornerAxisOverlap(rRect)) {
    return nullptr;
  }

  RRect adjustedRRect = rRect;
  Matrix deviceToLocal = Matrix::I();
  if (!matrix.isIdentity()) {
    const auto scales = matrix.getAxisScales();
    if (scales.x == 0 || scales.y == 0) {
      return nullptr;
    }
    // Bake axisScales into the rrect so the shader's local-space distance comparison
    // matches device-space half-pixel AA semantics.
    adjustedRRect.scale(scales.x, scales.y);
    Matrix adjustedMatrix = matrix;
    adjustedMatrix.preScale(1.0f / scales.x, 1.0f / scales.y);
    if (!adjustedMatrix.invert(&deviceToLocal)) {
      return nullptr;
    }
  }
  // AA coverage uses the fixed-iteration cubic distance solver. The corner's axis ratio affects
  // its accuracy; the measured limit keeps the error subpixel.
  if (antiAlias) {
    // Non-Complex types have identical radii across all four corners, so only the first needs
    // check.
    const auto& cornerRadii = adjustedRRect.radii();
    const auto cornerCount = adjustedRRect.isComplex() ? cornerRadii.size() : size_t{1};
    for (size_t i = 0; i < cornerCount; ++i) {
      const auto& r = cornerRadii[i];
      if (std::max(r.x, r.y) > 4.0f * std::min(r.x, r.y)) {
        return nullptr;
      }
    }
  }
  return allocator->make<GLSLSmoothRRectEffect>(adjustedRRect.rect(), adjustedRRect.radii(),
                                                adjustedRRect.smoothness(), deviceToLocal,
                                                antiAlias);
}

GLSLSmoothRRectEffect::GLSLSmoothRRectEffect(const Rect& localRect,
                                             const std::array<Point, 4>& radii, float smoothness,
                                             const Matrix& deviceToLocal, bool antiAlias)
    : SmoothRRectEffect(localRect, radii, smoothness, deviceToLocal, antiAlias) {
}

void GLSLSmoothRRectEffect::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;

  const auto rectName =
      uniformHandler->addUniform("LocalRect", UniformFormat::Float4, ShaderStage::Fragment);
  const auto radiiXName =
      uniformHandler->addUniform("RadiiX", UniformFormat::Float4, ShaderStage::Fragment);
  const auto radiiYName =
      uniformHandler->addUniform("RadiiY", UniformFormat::Float4, ShaderStage::Fragment);
  const auto antiAliasName =
      uniformHandler->addUniform("AntiAlias", UniformFormat::Float, ShaderStage::Fragment);
  const auto curveCoeffsName =
      uniformHandler->addUniform("CurveCoeffs", UniformFormat::Float4, ShaderStage::Fragment);
  const auto smoothnessName =
      uniformHandler->addUniform("Smoothness", UniformFormat::Float, ShaderStage::Fragment);

  // Local coordinate
  if (needTransform()) {
    const auto deviceToLocalName =
        uniformHandler->addUniform("DeviceToLocal", UniformFormat::Float3x3, ShaderStage::Fragment);
    fragBuilder->codeAppendf("highp vec3 hl = %s * vec3(gl_FragCoord.xy, 1.0);",
                             deviceToLocalName.c_str());
    fragBuilder->codeAppend("highp vec2 local = hl.xy / hl.z;");
  } else {
    fragBuilder->codeAppend("highp vec2 local = gl_FragCoord.xy;");
  }

  // Coverage
  const auto [circleCornerDistFunc, ellipseCornerDistFunc] =
      EmitSmoothCornerFunctions(fragBuilder);

  fragBuilder->codeAppendf("highp vec4 bounds = %s;", rectName.c_str());
  fragBuilder->codeAppendf("highp vec4 xRadii = %s;", radiiXName.c_str());
  fragBuilder->codeAppendf("highp vec4 yRadii = %s;", radiiYName.c_str());
  fragBuilder->codeAppendf("highp vec4 coeffs = %s;", curveCoeffsName.c_str());
  fragBuilder->codeAppendf("highp float smoothness = %s;", smoothnessName.c_str());
  fragBuilder->codeAppendf("bool antiAlias = %s > 0.5;", antiAliasName.c_str());

  // Compute the fragment position in each corner frame, with the vertex at the origin and the x/y
  // axes pointing into the shape along the edges. Order matches the C++ side: TL, TR, BR, BL.
  fragBuilder->codeAppend("highp vec4 cornersX = vec4(bounds.x, bounds.z, bounds.z, bounds.x);");
  fragBuilder->codeAppend("highp vec4 cornersY = vec4(bounds.y, bounds.y, bounds.w, bounds.w);");
  fragBuilder->codeAppend("vec4 signsX = vec4(1.0, -1.0, -1.0, 1.0);");
  fragBuilder->codeAppend("vec4 signsY = vec4(1.0, 1.0, -1.0, -1.0);");
  fragBuilder->codeAppend("highp vec4 dx = (vec4(local.x) - cornersX) * signsX;");
  fragBuilder->codeAppend("highp vec4 dy = (vec4(local.y) - cornersY) * signsY;");
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
  fragBuilder->codeAppend("highp float dist;");
  fragBuilder->codeAppend("if (rx > 0.0) {");
  fragBuilder->codeAppend("highp float ry = dot(inCorner, yRadii);");
  fragBuilder->codeAppend("highp vec2 q = vec2(dot(inCorner, dx), dot(inCorner, dy));");
  fragBuilder->codeAppend("bool isCircle = abs(rx - ry) <= 1.0 / 4096.0;");
  fragBuilder->codeAppendf(
      "dist = isCircle ? %s(q, rx, coeffs, smoothness, antiAlias) : "
      "%s(q, vec2(rx, ry), coeffs, smoothness, antiAlias);",
      circleCornerDistFunc.c_str(), ellipseCornerDistFunc.c_str());
  fragBuilder->codeAppend("} else {");
  fragBuilder->codeAppend("highp vec2 d0 = local - bounds.xy;");
  fragBuilder->codeAppend("highp vec2 d1 = bounds.zw - local;");
  fragBuilder->codeAppend("dist = -min(min(d0.x, d0.y), min(d1.x, d1.y));");
  fragBuilder->codeAppend("}");

  fragBuilder->codeAppend(
      "highp float coverage = antiAlias ? clamp(0.5 - dist, 0.0, 1.0) : step(dist, 0.0);");
  fragBuilder->codeAppendf("%s = %s * coverage;", args.outputColor.c_str(),
                           args.inputColor.c_str());
}

void GLSLSmoothRRectEffect::onSetData(UniformData*, UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("LocalRect", localRect);
  // Pack the per-corner radii into two vec4 uniforms (RadiiX, RadiiY) in [TL, TR, BR, BL]
  // order to match the shader.
  const std::array<float, 4> radiiXValues = {radii[0].x, radii[1].x, radii[2].x, radii[3].x};
  const std::array<float, 4> radiiYValues = {radii[0].y, radii[1].y, radii[2].y, radii[3].y};
  fragmentUniformData->setData("RadiiX", radiiXValues);
  fragmentUniformData->setData("RadiiY", radiiYValues);
  const float antiAliasValue = antiAlias ? 1.0f : 0.0f;
  fragmentUniformData->setData("AntiAlias", antiAliasValue);
  const auto coeffs = ComputeSmoothCornerCoeffs(smoothness);
  const std::array<float, 4> coeffsValues = {coeffs.start, coeffs.step, coeffs.arcOffsetX,
                                             coeffs.arcOffsetY};
  fragmentUniformData->setData("CurveCoeffs", coeffsValues);
  fragmentUniformData->setData("Smoothness", smoothness);
  if (needTransform()) {
    fragmentUniformData->setData("DeviceToLocal", deviceToLocal());
  }
}

}  // namespace tgfx