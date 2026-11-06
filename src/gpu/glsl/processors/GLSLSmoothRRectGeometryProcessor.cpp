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
#include "gpu/ShaderBuilder.h"
#include "gpu/glsl/GLSLSmoothCorner.h"

namespace tgfx {
// Emits the smoothed circular and elliptical corner distance functions for antialiased fills and
// strokes and returns their mangled names. C1 and C3 are the transition curves leaving the x-axis
// and y-axis edges, and C2 is the retained arc joining C1 at ps and C3 at pe. The contour normals
// at ps and pe split the plane into four regions by the signs of fs and fe, which are positive on
// the C1 side of the normal at ps and on the C2 side of the normal at pe:
//   S1  fs > 0, fe > 0  contains C1, whose distance is returned
//   S2  fs < 0, fe > 0  contains C2, whose distance is returned
//   S3  fs < 0, fe < 0  contains C3, whose distance is returned
//   S4  fs > 0, fe < 0  contains no segment and lies inside the shape beyond the intersection of
//                       both normals, at least the safe distance R from the contour; fills
//                       return -1, strokes return the nearer distance of C1 and C3
// S2 lies below iy, the y coordinate of that intersection (the radius for a circular corner), and
// S4 above it. Near full smoothness the two normals almost coincide, and rounding can put
// fragments on the wrong side of iy into S2 or S4, so both branches also check q.y against iy and
// leave the remaining fragments to the transition curves. A fill reads S4 as fully covered, which
// is exact whenever R reaches the antialiasing ramp. The transition curves share a single Bezier
// distance call with the control points selected by region, so fragments of one warp that span
// several regions run the Newton iterations as few times as possible.
static std::pair<std::string, std::string> EmitSmoothCornerAAFillStrokeDistFunctions(
    ShaderBuilder* builder, bool stroke, bool needNormal) {
  const auto bezierDistFunc =
      EmitNewtonCubicBezierDistFunction(builder, stroke ? 3 : 2, needNormal);
  // When needNormal is true, the entry functions also report the unnormalized contour normal at
  // the nearest point.
  const auto returnType = needNormal ? "highp vec3 " : "highp float ";

  // The join-point normals pass through the arc center, so the radial projection of a fragment in
  // S2 always lands within the retained sweep and the exact circle distance applies.
  // clang-format off
  const auto circleS2BranchCode = needNormal ? R"(
    highp vec2 arcDir = q - vec2(radius);
    return vec3(length(arcDir) - radius, arcDir);
)" : R"(
    highp vec2 arcDir = q - vec2(radius);
    return length(arcDir) - radius;
)";
  // clang-format on

  std::string circleS4BranchCode;
  std::string ellipseS4BranchCode;
  if (!stroke) {
    // clang-format off
    circleS4BranchCode = needNormal ? R"(
  if (fs > 0.0 && fe <= 0.0 && q.y > radius) {
    return vec3(-1.0, q - vec2(radius));
  })" : R"(
  if (fs > 0.0 && fe <= 0.0 && q.y > radius) {
    return -1.0;
  })";
    ellipseS4BranchCode = needNormal ? R"(
  if (fs > 0.0 && fe <= 0.0 && q.y > iy) {
    return vec3(-1.0, (q - radii) / (radii * radii));
  })" : R"(
  if (fs > 0.0 && fe <= 0.0 && q.y > iy) {
    return -1.0;
  })";
    // clang-format on
  }

  // Signed distance to the smoothed circular corner, negative inside.
  //   q           the point in the corner frame, with the vertex at the origin and the interior
  //               in the first quadrant
  //   radius      the corner radius
  //   coeffs      the per-unit-radius transition coefficients (footprint, step, arcOffsetX,
  //               arcOffsetY)
  //   smoothness  the corner smoothness
  //   return      the signed distance; when needNormal is true, a vec3 that also carries the
  //               unnormalized contour normal at the nearest point in yz
  const auto cornerDistFunc = builder->getMangledFunctionName("smoothCornerDist");
  // clang-format off
  builder->addFunction(
returnType + cornerDistFunc + R"((highp vec2 q, highp float radius, highp vec4 coeffs,
                                  highp float smoothness) {
  highp float clampedSmoothness = clamp(smoothness, 0.0, 1.0);
  highp float turnAngle = 3.14159265359 * 0.25 * clampedSmoothness;
  highp float turnSin = sin(turnAngle);
  highp float turnCos = cos(turnAngle);
  highp float footprint = coeffs.x * radius;
  highp float step = coeffs.y * radius;
  highp float ctrl2 = footprint - 3.0 * step;
  highp vec2 ps = vec2(ctrl2 - coeffs.z * radius, coeffs.w * radius);
  highp vec2 pe = ps.yx;
  highp float fs = turnCos * (q.x - ps.x) - turnSin * (q.y - ps.y);
  highp float fe = turnSin * (q.x - pe.x) - turnCos * (q.y - pe.y);
  if (fs <= 0.0 && fe > 0.0 && q.y < radius) {)" +
    circleS2BranchCode + R"(
  })" +
    circleS4BranchCode + R"(
  // S1, S3 or S4. C1 and C3 mirror each other across y = x, so C1 is nearer where q.x > q.y.
  bool c1 = fs > 0.0 && (fe > 0.0 || q.x >= q.y);
  highp vec2 b0 = c1 ? vec2(footprint, 0.0) : pe;
  highp vec2 b1 = c1 ? vec2(footprint - 2.0 * step, 0.0) : vec2(0.0, ctrl2);
  highp vec2 b2 = c1 ? vec2(ctrl2, 0.0) : vec2(0.0, footprint - 2.0 * step);
  highp vec2 b3 = c1 ? ps : vec2(0.0, footprint);
  return )" + bezierDistFunc + R"((q, b0, b1, b2, b3);
}
)");
  // clang-format on

  std::string ellipseS2BranchCode;
  if (stroke) {
    // The retained arc as C2b: control handles of arcHandle * rx along the endpoint tangents of
    // the ky-scaled circular construction, matching the CPU path contour exactly.
    // clang-format off
    ellipseS2BranchCode =
R"(
    highp float arcSweep = 3.14159265359 * 0.5 * (1.0 - clampedSmoothness);
    highp float arcHandle = (4.0 / 3.0) * tan(arcSweep * 0.25);
    highp float handle = arcHandle * rx;
    return )" + bezierDistFunc + R"((q, ps, ps + handle * vec2(-turnCos, ky * turnSin),
                                     pe - handle * vec2(-turnSin, ky * turnCos), pe);
)";
    // clang-format on
  } else {
    // The Sampson distance is a first-order approximation whose error grows away from the
    // contour; it stays accurate within the antialiasing ramp, which is all a fill needs.
    const auto arcDistFunc =
        EmitSampsonEllipseDistFunction(builder, "smoothCornerArcDist", needNormal);
    // clang-format off
    ellipseS2BranchCode =
R"(
    return )" + arcDistFunc + R"((q - radii, radii);
)";
    // clang-format on
  }

  std::string ellipseStrokeS4Code;
  if (stroke) {
    const auto c3NearerCondition =
        needNormal ? "abs(c3Dist.x) < abs(dist.x)" : "abs(c3Dist) < abs(dist)";
    // The boundary between the C1 and C3 nearest regions in S4 is not a straight line.
    // clang-format off
    ellipseStrokeS4Code = R"(
  if (fs > 0.0 && fe <= 0.0) {
    )" + std::string(returnType) + R"(c3Dist = )" + bezierDistFunc + R"((q, c3b0, c3b1, c3b2, c3b3);
    if ()" + c3NearerCondition + R"() {
      dist = c3Dist;
    }
  })";
    // clang-format on
  }

  // Signed distance to the smoothed elliptical corner, with fs and fe weighted by the radii.
  //   q           the point in the corner frame
  //   radii       the corner radii on x and y
  //   coeffs      the per-unit-radius transition coefficients (footprint, step, arcOffsetX,
  //               arcOffsetY)
  //   smoothness  the corner smoothness
  //   return      the signed distance; when needNormal is true, a vec3 that also carries the
  //               unnormalized contour normal at the nearest point in yz
  const auto ellipseDistFunc = builder->getMangledFunctionName("smoothCornerDistEllipse");
  // clang-format off
  builder->addFunction(
returnType + ellipseDistFunc + R"((highp vec2 q, highp vec2 radii, highp vec4 coeffs,
                                   highp float smoothness) {
  highp float clampedSmoothness = clamp(smoothness, 0.0, 1.0);
  highp float turnAngle = 3.14159265359 * 0.25 * clampedSmoothness;
  highp float turnSin = sin(turnAngle);
  highp float turnCos = cos(turnAngle);
  highp float rx = radii.x;
  highp float ry = radii.y;
  highp float ky = ry / rx;
  highp float footprint = coeffs.x * rx;
  highp float step = coeffs.y * rx;
  highp float ctrl2 = footprint - 3.0 * step;
  highp vec2 ps = vec2(ctrl2 - coeffs.z * rx, coeffs.w * ry);
  highp vec2 pe = vec2(ps.y / ky, ps.x * ky);
  highp float fs = rx * turnCos * (q.x - ps.x) - ry * turnSin * (q.y - ps.y);
  highp float fe = rx * turnSin * (q.x - pe.x) - ry * turnCos * (q.y - pe.y);
  highp float iy = ry + (rx * rx - ry * ry) * turnSin * turnCos / ((turnSin + turnCos) * ry);
  if (fs <= 0.0 && fe > 0.0 && q.y < iy) {)" +
    ellipseS2BranchCode + R"(
  })" +
    ellipseS4BranchCode + R"(
  // S1, S3 or S4. S4 starts with C1, and strokes also evaluate C3 below.
  highp vec2 c3b0 = pe;
  highp vec2 c3b1 = vec2(0.0, ctrl2 * ky);
  highp vec2 c3b2 = vec2(0.0, (footprint - 2.0 * step) * ky);
  highp vec2 c3b3 = vec2(0.0, footprint * ky);
  bool c1 = fs > 0.0;
  highp vec2 b0 = c1 ? vec2(footprint, 0.0) : c3b0;
  highp vec2 b1 = c1 ? vec2(footprint - 2.0 * step, 0.0) : c3b1;
  highp vec2 b2 = c1 ? vec2(ctrl2, 0.0) : c3b2;
  highp vec2 b3 = c1 ? ps : c3b3;
  )" + returnType + R"(dist = )" + bezierDistFunc + R"((q, b0, b1, b2, b3);)" +
    ellipseStrokeS4Code + R"(
  return dist;
}
)");
  // clang-format on

  return {cornerDistFunc, ellipseDistFunc};
}

// For non-antialiased fills only the sign of the returned functions' value is meaningful.
static std::pair<std::string, std::string> EmitSmoothCornerDistFunctions(ShaderBuilder* builder,
                                                                         bool stroke,
                                                                         bool unscaledGeometry,
                                                                         bool antiAlias) {
  if (!stroke && !antiAlias) {
    return EmitSmoothCornerNonAAFillFunctions(builder);
  }
  return EmitSmoothCornerAAFillStrokeDistFunctions(builder, stroke, unscaledGeometry && antiAlias);
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
  auto smoothnessVarying = varyingHandler->addVarying("smoothness", SLType::Float, true);
  vertBuilder->codeAppendf("%s = %s;", smoothnessVarying.vsOut().c_str(),
                           inSmoothness.name().c_str());
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

  // Transition curve coefficients
  // Precompute the per-shape coefficients to avoid recomputing them per fragment.
  auto curveCoeffsVarying = varyingHandler->addVarying("curveCoeffs", SLType::Float4, true);
  vertBuilder->codeAppendf("highp float smoothness = clamp(%s, 0.0, 1.0);",
                           inSmoothness.name().c_str());
  vertBuilder->codeAppend("highp float turnAngle = 3.14159265359 * 0.25 * smoothness;");
  vertBuilder->codeAppend("highp float tanTurn = tan(turnAngle);");
  vertBuilder->codeAppend("highp float start = 1.0 + smoothness;");
  vertBuilder->codeAppend("highp float arcOffsetX = tan(turnAngle * 0.5) * cos(turnAngle);");
  vertBuilder->codeAppend(
      "highp float step = (smoothness + sin(turnAngle) - arcOffsetX) / 3.0;");
  vertBuilder->codeAppend("highp float arcOffsetY = arcOffsetX * tanTurn;");
  vertBuilder->codeAppendf("%s = vec4(start, step, arcOffsetX, arcOffsetY);",
                           curveCoeffsVarying.vsOut().c_str());

  // Transforms
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                 ShaderVar(inPosition.name(), SLType::Float2));

  // Coverage
  const auto [circleCornerDistFunc, ellipseCornerDistFunc] =
      EmitSmoothCornerDistFunctions(fragBuilder, stroke, unscaledGeometry, antiAlias);

  fragBuilder->codeAppendf("highp vec4 coeffs = %s;", curveCoeffsVarying.fsIn().c_str());
  fragBuilder->codeAppendf("highp float smoothness = %s;", smoothnessVarying.fsIn().c_str());
  fragBuilder->codeAppendf("highp vec2 localCoord = %s;", localCoordVarying.fsIn().c_str());
  fragBuilder->codeAppendf("highp vec4 xRadii = %s;", xRadiiVarying.fsIn().c_str());
  fragBuilder->codeAppendf("highp vec4 yRadii = %s;", yRadiiVarying.fsIn().c_str());
  fragBuilder->codeAppendf("highp vec4 bounds = %s;", boundsVarying.fsIn().c_str());

  // Corner-box dispatch. Each corner owns a quadrant-local box widened to the smoothed corner
  // footprint. Order matches the vertex data: TL, TR, BR, BL.
  fragBuilder->codeAppend("highp vec4 cornersX = vec4(bounds.x, bounds.z, bounds.z, bounds.x);");
  fragBuilder->codeAppend("highp vec4 cornersY = vec4(bounds.y, bounds.y, bounds.w, bounds.w);");
  // signsX/Y flip the local coordinate so that all four corner-box queries become first-quadrant.
  fragBuilder->codeAppend("vec4 signsX = vec4(1.0, -1.0, -1.0, 1.0);");
  fragBuilder->codeAppend("vec4 signsY = vec4(1.0, 1.0, -1.0, -1.0);");
  fragBuilder->codeAppend("highp vec4 dx = (vec4(localCoord.x) - cornersX) * signsX;");
  fragBuilder->codeAppend("highp vec4 dy = (vec4(localCoord.y) - cornersY) * signsY;");
  // inBox is 1.0 for each corner whose widened box contains the fragment. Edge competition in
  // RRect keeps the widened boxes of adjacent corners disjoint.
  fragBuilder->codeAppendf("vec4 inBox = step(dx, xRadii * %s.x) * step(dy, yRadii * %s.x);",
                           "coeffs", "coeffs");
  // Zero-radius corners own no box: without this, step() would claim the outside quadrant past
  // the vertex. Those fragments take the edge distance, which keeps the corner sharp. This must
  // run before the one-hot collapse so a discarded box cannot suppress a valid neighbor.
  fragBuilder->codeAppend(
      "inBox *= vec4(greaterThan(xRadii, vec4(0.0))) * vec4(greaterThan(yRadii, vec4(0.0)));");
  // One-hot collapse: when adjacent corner boxes share a fragment along their boundary, keep
  // the first hit so subsequent queries collapse to a single corner.
  fragBuilder->codeAppend(
      "vec4 prefixSum = vec4(0.0, inBox.x, inBox.x + inBox.y, "
      "inBox.x + inBox.y + inBox.z);");
  fragBuilder->codeAppend("inBox *= step(prefixSum, vec4(0.5));");

  fragBuilder->codeAppend("highp float r = dot(inBox, xRadii);");
  fragBuilder->codeAppend("highp float ry = dot(inBox, yRadii);");
  // Map the fragment into the selected corner's frame: vertex at the origin, edges along the
  // +x/+y axes, interior in the first quadrant. The signs double up as the mirroring factors.
  fragBuilder->codeAppend(
      "highp vec2 cornerVertex = vec2(dot(inBox, cornersX), dot(inBox, cornersY));");
  fragBuilder->codeAppend("highp vec2 cornerSigns = vec2(dot(inBox, signsX), dot(inBox, signsY));");
  fragBuilder->codeAppend("highp vec2 q = (localCoord - cornerVertex) * cornerSigns;");

  // Edge / center distance (negative inside, positive outside).
  fragBuilder->codeAppend("highp vec2 d0 = localCoord - bounds.xy;");
  fragBuilder->codeAppend("highp vec2 d1 = bounds.zw - localCoord;");
  fragBuilder->codeAppend("highp float minEdge = min(min(d0.x, d0.y), min(d1.x, d1.y));");
  fragBuilder->codeAppend("highp float dist = -minEdge;");

  if (stroke) {
    // Stroke: the same signed distance field is reused, and the coverage is computed from the
    // ring distance |dist| - halfStroke, so the ring's outer edge follows the true offset curve
    // of the contour.
    fragBuilder->codeAppendf("highp float halfStroke = %s;",
                             halfStrokeWidthVarying.fsIn().c_str());
  }
  // r is zero only for fragments outside every corner box, which keep the edge distance. The
  // corner functions must not run there: the elliptical one divides by the radius.
  if (aaUnscaledGeometry) {
    fragBuilder->codeAppend(
        "highp vec2 edgeNormal = minEdge == d0.x ? vec2(-1.0, 0.0) "
        ": (minEdge == d0.y ? vec2(0.0, -1.0) "
        ": (minEdge == d1.x ? vec2(1.0, 0.0) : vec2(0.0, 1.0)));");
    fragBuilder->codeAppend("highp vec3 distNormal = vec3(dist, edgeNormal);");
    fragBuilder->codeAppend("if (r > 0.0) {");
    fragBuilder->codeAppend("if (abs(r - ry) <= 1.0 / 4096.0) {");
    fragBuilder->codeAppendf("distNormal = %s(q, r, coeffs, smoothness);",
                             circleCornerDistFunc.c_str());
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppendf("distNormal = %s(q, vec2(r, ry), coeffs, smoothness);",
                             ellipseCornerDistFunc.c_str());
    fragBuilder->codeAppend("}");
    fragBuilder->codeAppend("}");
    fragBuilder->codeAppend("dist = distNormal.x;");
    if (stroke) {
      fragBuilder->codeAppend("dist = abs(dist) - halfStroke;");
    }
    // The distances so far are in the unscaled local frame. Convert to the scaled local frame,
    // where the coverage ramp is defined: scaled distance = local distance /
    // |invScale * unitNormal|.
    fragBuilder->codeAppend("highp float normalLength = max(length(distNormal.yz), 1.1755e-38);");
    fragBuilder->codeAppendf("dist /= length(%s * distNormal.yz) / normalLength;",
                             invScaleVarying.fsIn().c_str());
  } else {
    fragBuilder->codeAppend("if (r > 0.0) {");
    fragBuilder->codeAppend("if (abs(r - ry) <= 1.0 / 4096.0) {");
    fragBuilder->codeAppendf("dist = %s(q, r, coeffs, smoothness);",
                             circleCornerDistFunc.c_str());
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppendf("dist = %s(q, vec2(r, ry), coeffs, smoothness);",
                             ellipseCornerDistFunc.c_str());
    fragBuilder->codeAppend("}");
    fragBuilder->codeAppend("}");
    if (stroke) {
      fragBuilder->codeAppend("dist = abs(dist) - halfStroke;");
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
