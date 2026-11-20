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
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "gpu/ShaderBuilder.h"
#include "gpu/glsl/GLSLSmoothCorner.h"

namespace tgfx {
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
 *   float circleName(vec2 q, float radius, vec4 coeffs, vec3 arcCoeffs, bool antiAlias);
 *   float ellipseName(vec2 q, vec2 radii, vec4 coeffs, vec3 arcCoeffs, bool antiAlias);
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
 *     antiAlias  whether coverage is antialiased
 * They return the signed distance from q to C, negative inside. Non-antialiased coverage only
 * needs the inside/outside sign, so when antiAlias is false they evaluate the implicit equations
 * of the segments other than the circular C2 and return a value with the same sign instead.
 * @param builder the shader builder that receives the functions
 * @return the mangled names of the circular and elliptical corner functions
 */
static std::pair<std::string, std::string> EmitCornerFunctions(ShaderBuilder* builder) {
  // Antialiased coverage only reads distances within the antialiasing ramp, where this iteration
  // count keeps the measured error subpixel.
  const auto transitionDistFunc = EmitCubicBezierNewtonDistFunction(builder, 2, false);
  const auto transitionImplicitFunc = EmitTransitionImplicitFunction(builder);

  // Computes the signed distance to the smoothed circular corner, negative inside.
  // S1, S2 and S3 evaluate C1, C2 and C3 respectively. S4 also uses a transition curve: C1 is
  // nearer when q.x >= q.y, and C3 is nearer otherwise, since C1 and C3 mirror each other across
  // y = x.
  // Near full smoothness NS and NE nearly coincide, and rounding can classify S4
  // fragments beyond the circle center into S2. S2 lies entirely on the vertex side of the center,
  // so S2 also requires q.x <= radius.
  // In S2, the nearest point on C2 lies along the radial direction, so the distance is the
  // distance to the circle center minus the radius. It is exact and cheap, so non-antialiased
  // coverage uses it as well.
  // In S1, S3 and S4, the implicit equation evaluates C3 as C1 at the swapped point.
  const auto circleCornerFunc = builder->getMangledFunctionName("smoothCornerDist");
  // clang-format off
  builder->addFunction(R"(
highp float )" + circleCornerFunc + R"((highp vec2 q, highp float radius, highp vec4 coeffs,
                                        highp vec3 arcCoeffs, bool antiAlias) {
  highp float turnSin = arcCoeffs.x;
  highp float turnCos = arcCoeffs.y;
  highp float transitionStart = coeffs.x * radius;
  highp float step = coeffs.y * radius;
  highp float b2x = transitionStart - 3.0 * step;
  highp vec2 ps = vec2(b2x - coeffs.z * radius, coeffs.w * radius);
  highp vec2 pe = ps.yx;
  highp float fs = turnCos * (q.x - ps.x) - turnSin * (q.y - ps.y);
  highp float fe = turnSin * (q.x - pe.x) - turnCos * (q.y - pe.y);
  if (fs <= 0.0 && fe > 0.0 && q.x <= radius) {
    highp vec2 arcDir = q - vec2(radius);
    return length(arcDir) - radius;
  }
  bool c1 = fs > 0.0 && (fe > 0.0 || q.x >= q.y);
  if (!antiAlias) {
    return )" + transitionImplicitFunc + R"((c1 ? q : q.yx, transitionStart, step,
                                             coeffs.z * radius, coeffs.w * radius);
  }
  highp vec2 b0 = c1 ? vec2(transitionStart, 0.0) : pe;
  highp vec2 b1 = c1 ? vec2(transitionStart - 2.0 * step, 0.0) : vec2(0.0, b2x);
  highp vec2 b2 = c1 ? vec2(b2x, 0.0) : vec2(0.0, transitionStart - 2.0 * step);
  highp vec2 b3 = c1 ? ps : vec2(0.0, transitionStart);
  return )" + transitionDistFunc + R"((q, b0, b1, b2, b3);
}
)");
  // clang-format on

  // Antialiased coverage only needs accurate distances within the antialiasing ramp. The Sampson
  // distance is a cheaper approximation that gets closer to the true distance nearer the ellipse,
  // so it stays accurate enough there.
  const auto arcDistFunc = EmitEllipseSampsonDistFunction(builder);
  const auto arcImplicitFunc = EmitEllipseImplicitFunction(builder);

  // Computes the signed distance to the smoothed elliptical corner, negative inside.
  // S1, S2 and S3 evaluate C1, C2 and C3 respectively. S4 also uses a transition curve: C1 is
  // nearer when NE is nearer than NS, and C3 is nearer otherwise. TS and TE are not unit vectors,
  // so the distances to NS and NE are |fs| / |TS| and |fe| / |TE|, compared squared.
  // Near full smoothness NS and NE nearly coincide, and rounding can classify S4
  // fragments beyond their intersection into S2. S2 lies entirely on the vertex side of the
  // intersection, so S2 also requires q.x <= normalIntersectX, the x coordinate of the
  // intersection.
  // In S1, S3 and S4, C3 mirrors across the corner bisector the C1 built with the radii swapped,
  // so the implicit equation evaluates it as that C1 at the swapped point.
  const auto ellipseCornerFunc = builder->getMangledFunctionName("smoothCornerDistEllipse");
  // clang-format off
  builder->addFunction(R"(
highp float )" + ellipseCornerFunc + R"((highp vec2 q, highp vec2 radii, highp vec4 coeffs,
                                         highp vec3 arcCoeffs, bool antiAlias) {
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
  if (fs <= 0.0 && fe > 0.0 && q.x <= normalIntersectX) {
    return antiAlias ? )" + arcDistFunc + R"((q - radii, radii)
                     : )" + arcImplicitFunc + R"((q - radii, radii);
  }
  highp float tsLengthSq = rySq + (rxSq - rySq) * turnCos * turnCos;
  highp float teLengthSq = rxSq + rySq - tsLengthSq;
  bool c1 = fs > 0.0 && (fe > 0.0 || fe * fe * tsLengthSq <= fs * fs * teLengthSq);
  if (!antiAlias) {
    highp vec2 axes = c1 ? radii : radii.yx;
    return )" + transitionImplicitFunc + R"((c1 ? q : q.yx, coeffs.x * axes.x, coeffs.y * axes.x,
                                             coeffs.z * axes.x, coeffs.w * axes.y);
  }
  highp vec2 b0 = c1 ? vec2(transitionStart.x, 0.0) : pe;
  highp vec2 b1 = c1 ? vec2(transitionStart.x - 2.0 * step.x, 0.0) : vec2(0.0, b2xy.y);
  highp vec2 b2 = c1 ? vec2(b2xy.x, 0.0) : vec2(0.0, transitionStart.y - 2.0 * step.y);
  highp vec2 b3 = c1 ? ps : vec2(0.0, transitionStart.y);
  return )" + transitionDistFunc + R"((q, b0, b1, b2, b3);
}
)");
  // clang-format on

  return {circleCornerFunc, ellipseCornerFunc};
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
      const auto minRadius = std::min(r.x, r.y);
      const auto maxRadius = std::max(r.x, r.y);
      if (maxRadius > 4.0f * minRadius) {
        return nullptr;
      }
      // The normals NS and NE at PS and PE select the nearest of C1, C2 and C3 correctly only
      // within the safe distance R = minRadius^2 / (2 * maxRadius), so the half width of the AA
      // band must not exceed it.
      if (0.5f * maxRadius > 0.5f * minRadius * minRadius) {
        return nullptr;
      }
    }
  }
  return allocator->make<GLSLSmoothRRectEffect>(adjustedRRect.rect(), adjustedRRect.radii(),
                                                adjustedRRect.smoothness(), deviceToLocal,
                                                antiAlias);
}

GLSLSmoothRRectEffect::GLSLSmoothRRectEffect(const Rect& rect, const std::array<Point, 4>& radii,
                                             float smoothness, const Matrix& deviceToLocal,
                                             bool antiAlias)
    : SmoothRRectEffect(rect, radii, smoothness, deviceToLocal, antiAlias) {
}

void GLSLSmoothRRectEffect::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;

  const auto rectName =
      uniformHandler->addUniform("Rect", UniformFormat::Float4, ShaderStage::Fragment);
  const auto radiiXName =
      uniformHandler->addUniform("RadiiX", UniformFormat::Float4, ShaderStage::Fragment);
  const auto radiiYName =
      uniformHandler->addUniform("RadiiY", UniformFormat::Float4, ShaderStage::Fragment);
  const auto antiAliasName =
      uniformHandler->addUniform("AntiAlias", UniformFormat::Float, ShaderStage::Fragment);
  const auto curveCoeffsName =
      uniformHandler->addUniform("CurveCoeffs", UniformFormat::Float4, ShaderStage::Fragment);
  const auto arcCoeffsName =
      uniformHandler->addUniform("ArcCoeffs", UniformFormat::Float3, ShaderStage::Fragment);

  // Local coordinate
  if (needTransform()) {
    const auto deviceToLocalName =
        uniformHandler->addUniform("DeviceToLocal", UniformFormat::Float3x3, ShaderStage::Fragment);
    fragBuilder->codeAppendf("highp vec3 hl = %s * vec3(gl_FragCoord.xy, 1.0);",
                             deviceToLocalName.c_str());
    fragBuilder->codeAppend("highp vec2 localCoord = hl.xy / hl.z;");
  } else {
    fragBuilder->codeAppend("highp vec2 localCoord = gl_FragCoord.xy;");
  }

  // Coverage
  const auto [circleCornerFunc, ellipseCornerFunc] = EmitCornerFunctions(fragBuilder);
  fragBuilder->codeAppendf("highp vec4 bounds = %s;", rectName.c_str());
  fragBuilder->codeAppendf("highp vec4 xRadii = %s;", radiiXName.c_str());
  fragBuilder->codeAppendf("highp vec4 yRadii = %s;", radiiYName.c_str());
  fragBuilder->codeAppendf("highp vec4 coeffs = %s;", curveCoeffsName.c_str());
  fragBuilder->codeAppendf("highp vec3 arcCoeffs = %s;", arcCoeffsName.c_str());
  fragBuilder->codeAppendf("bool antiAlias = %s > 0.5;", antiAliasName.c_str());

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
  fragBuilder->codeAppend("highp float dist;");
  fragBuilder->codeAppend("if (rx > 0.0) {");
  fragBuilder->codeAppend("highp float ry = dot(inCorner, yRadii);");
  fragBuilder->codeAppend("highp vec2 q = vec2(dot(inCorner, dx), dot(inCorner, dy));");
  fragBuilder->codeAppend("bool isCircle = abs(rx - ry) <= 1.0 / 4096.0;");
  fragBuilder->codeAppendf(
      "dist = isCircle ? %s(q, rx, coeffs, arcCoeffs, antiAlias) : "
      "%s(q, vec2(rx, ry), coeffs, arcCoeffs, antiAlias);",
      circleCornerFunc.c_str(), ellipseCornerFunc.c_str());
  fragBuilder->codeAppend("} else {");
  fragBuilder->codeAppend("highp vec2 d0 = localCoord - bounds.xy;");
  fragBuilder->codeAppend("highp vec2 d1 = bounds.zw - localCoord;");
  fragBuilder->codeAppend("dist = -min(min(d0.x, d0.y), min(d1.x, d1.y));");
  fragBuilder->codeAppend("}");

  fragBuilder->codeAppend(
      "highp float coverage = antiAlias ? clamp(0.5 - dist, 0.0, 1.0) : step(dist, 0.0);");
  fragBuilder->codeAppendf("%s = %s * coverage;", args.outputColor.c_str(),
                           args.inputColor.c_str());
}

void GLSLSmoothRRectEffect::onSetData(UniformData*, UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("Rect", rect);
  // Pack the per-corner radii into two vec4 uniforms (RadiiX, RadiiY) in [TL, TR, BR, BL]
  // order to match the shader.
  const std::array<float, 4> radiiXValues = {radii[0].x, radii[1].x, radii[2].x, radii[3].x};
  const std::array<float, 4> radiiYValues = {radii[0].y, radii[1].y, radii[2].y, radii[3].y};
  fragmentUniformData->setData("RadiiX", radiiXValues);
  fragmentUniformData->setData("RadiiY", radiiYValues);
  const float antiAliasValue = antiAlias ? 1.0f : 0.0f;
  fragmentUniformData->setData("AntiAlias", antiAliasValue);
  const auto coeffs = RRectUtils::ComputeUnitSmoothCornerCoeffs(smoothness);
  const std::array<float, 4> coeffsValues = {coeffs.start, coeffs.step, coeffs.arcOffsetAlong,
                                             coeffs.arcOffsetCross};
  fragmentUniformData->setData("CurveCoeffs", coeffsValues);
  // Angle the tangent turns through along C1.
  const auto turnAngle = M_PI_F * 0.25f * smoothness;
  const std::array<float, 3> arcCoeffsValues = {sinf(turnAngle), cosf(turnAngle), coeffs.arcHandle};
  fragmentUniformData->setData("ArcCoeffs", arcCoeffsValues);
  if (needTransform()) {
    fragmentUniformData->setData("DeviceToLocal", deviceToLocal());
  }
}

}  // namespace tgfx
