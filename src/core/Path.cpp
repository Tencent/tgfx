/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/Path.h"
#include <algorithm>
#include <cmath>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#include <include/core/SkPath.h>
#pragma clang diagnostic pop
#include <include/core/SkPathTypes.h>
#include <include/core/SkRect.h>
#include <include/pathops/SkPathOps.h>
#include "core/PathRef.h"
#include "core/RRectUtils.h"
#include "core/utils/AtomicCache.h"
#include "core/utils/MathExtra.h"

namespace tgfx {
using namespace pk;

static SkPathDirection ToSkDirection(bool reversed) {
  return reversed ? SkPathDirection::kCCW : SkPathDirection::kCW;
}

static SkPoint ToSkPoint(const Point& point) {
  return {point.x, point.y};
}

#define DistanceToControlPoint(radStep) (4 * tanf((radStep)*0.25f) / 3)

class PointIterator {
 public:
  PointIterator(const std::vector<Point>& points, bool reversed, unsigned startIndex)
      : points(points), index(startIndex % points.size()),
        advance(reversed ? points.size() - 1 : 1) {
  }

  const SkPoint& current() const {
    return *reinterpret_cast<const SkPoint*>(&points[index]);
  }

  const SkPoint& next() {
    index = (index + advance) % points.size();
    return this->current();
  }

 protected:
  const std::vector<Point>& points = {};

 private:
  size_t index = 0;
  size_t advance = 0;
};

static SkRect ToSkRect(const Rect& rect) {
  return {rect.left, rect.top, rect.right, rect.bottom};
}

Path::Path() : pathRef(std::make_shared<PathRef>()) {
}

bool operator==(const Path& a, const Path& b) {
  return a.pathRef->path == b.pathRef->path;
}

bool operator!=(const Path& a, const Path& b) {
  return a.pathRef->path != b.pathRef->path;
}

PathFillType Path::getFillType() const {
  auto fillType = pathRef->path.getFillType();
  switch (fillType) {
    case SkPathFillType::kEvenOdd:
      return PathFillType::EvenOdd;
    case SkPathFillType::kInverseEvenOdd:
      return PathFillType::InverseEvenOdd;
    case SkPathFillType::kInverseWinding:
      return PathFillType::InverseWinding;
    default:
      return PathFillType::Winding;
  }
}

void Path::setFillType(PathFillType fillType) {
  if (getFillType() == fillType) {
    return;
  }
  SkPathFillType type;
  switch (fillType) {
    case PathFillType::EvenOdd:
      type = SkPathFillType::kEvenOdd;
      break;
    case PathFillType::InverseEvenOdd:
      type = SkPathFillType::kInverseEvenOdd;
      break;
    case PathFillType::InverseWinding:
      type = SkPathFillType::kInverseWinding;
      break;
    default:
      type = SkPathFillType::kWinding;
      break;
  }
  writableRef()->path.setFillType(type);
}

bool Path::isInverseFillType() const {
  auto fillType = pathRef->path.getFillType();
  return fillType == SkPathFillType::kInverseWinding || fillType == SkPathFillType::kInverseEvenOdd;
}

void Path::toggleInverseFillType() {
  auto& path = writableRef()->path;
  switch (path.getFillType()) {
    case SkPathFillType::kWinding:
      path.setFillType(SkPathFillType::kInverseWinding);
      break;
    case SkPathFillType::kEvenOdd:
      path.setFillType(SkPathFillType::kInverseEvenOdd);
      break;
    case SkPathFillType::kInverseWinding:
      path.setFillType(SkPathFillType::kWinding);
      break;
    case SkPathFillType::kInverseEvenOdd:
      path.setFillType(SkPathFillType::kEvenOdd);
      break;
  }
}

bool Path::isLine(Point line[2]) const {
  return pathRef->path.isLine(reinterpret_cast<SkPoint*>(line));
}

bool Path::isRect(Rect* rect, bool* closed, bool* reversed) const {
  std::unique_ptr<SkRect> rectPointer = rect ? std::make_unique<SkRect>() : nullptr;
  std::unique_ptr<SkPathDirection> directionPointer =
      reversed ? std::make_unique<SkPathDirection>() : nullptr;

  if (!pathRef->path.isRect(rectPointer.get(), closed, directionPointer.get())) {
    return false;
  }

  if (rectPointer) {
    rect->setLTRB(rectPointer->fLeft, rectPointer->fTop, rectPointer->fRight, rectPointer->fBottom);
  }
  if (directionPointer) {
    *reversed = *directionPointer != SkPathDirection::kCW;
  }
  return true;
}

bool Path::isOval(Rect* bounds) const {
  if (!bounds) {
    return pathRef->path.isOval(nullptr);
  }
  SkRect skRect = {};
  if (!pathRef->path.isOval(&skRect)) {
    return false;
  }
  bounds->setLTRB(skRect.fLeft, skRect.fTop, skRect.fRight, skRect.fBottom);
  return true;
}

bool Path::isRRect(RRect* rRect) const {
  SkRRect skRRect = {};
  if (!pathRef->path.isRRect(&skRRect) || skRRect.isEmpty() || skRRect.isRect() ||
      skRRect.isOval()) {
    return false;
  }
  if (rRect) {
    const auto& skRect = skRRect.rect();
    const auto rect = Rect::MakeLTRB(skRect.fLeft, skRect.fTop, skRect.fRight, skRect.fBottom);
    std::array<Point, 4> cornerRadii = {};
    for (size_t i = 0; i < 4; ++i) {
      const auto r = skRRect.radii(static_cast<SkRRect::Corner>(i));
      cornerRadii[i] = {r.fX, r.fY};
    }
    // Re-normalize to stay within RRect's invariants.
    rRect->setRectRadii(rect, cornerRadii);
  }
  return true;
}

Rect Path::getBounds() const {
  return pathRef->getBounds();
}

Rect Path::computeTightBounds() const {
  auto skRect = pathRef->path.computeTightBounds();
  return {skRect.fLeft, skRect.fTop, skRect.fRight, skRect.fBottom};
}

bool Path::isEmpty() const {
  return pathRef->path.isEmpty();
}

bool Path::contains(float x, float y) const {
  return pathRef->path.contains(x, y);
}

bool Path::contains(const Rect& rect) const {
  return pathRef->path.conservativelyContainsRect(ToSkRect(rect));
}

bool Path::isConvex() const {
  return pathRef->path.isConvex();
}

bool Path::isLastContourClosed() const {
  return pathRef->path.isLastContourClosed();
}

void Path::moveTo(float x, float y) {
  writableRef()->path.moveTo(x, y);
}

void Path::moveTo(const Point& point) {
  moveTo(point.x, point.y);
}

void Path::lineTo(float x, float y) {
  writableRef()->path.lineTo(x, y);
}

void Path::lineTo(const Point& point) {
  lineTo(point.x, point.y);
}

void Path::quadTo(float controlX, float controlY, float x, float y) {
  writableRef()->path.quadTo(controlX, controlY, x, y);
}

void Path::quadTo(const Point& control, const Point& point) {
  quadTo(control.x, control.y, point.x, point.y);
}

void Path::cubicTo(float controlX1, float controlY1, float controlX2, float controlY2, float x,
                   float y) {
  writableRef()->path.cubicTo(controlX1, controlY1, controlX2, controlY2, x, y);
}

void Path::cubicTo(const Point& control1, const Point& control2, const Point& point) {
  cubicTo(control1.x, control1.y, control2.x, control2.y, point.x, point.y);
}

void Path::conicTo(float controlX, float controlY, float x, float y, float weight) {
  writableRef()->path.conicTo(controlX, controlY, x, y, weight);
}

void Path::conicTo(const Point& control, const Point& point, float weight) {
  conicTo(control.x, control.y, point.x, point.y, weight);
}

void Path::arcTo(float x1, float y1, float x2, float y2, float radius) {
  writableRef()->path.arcTo(x1, y1, x2, y2, radius);
}

void Path::arcTo(const Point& p1, const Point& p2, float radius) {
  arcTo(p1.x, p1.y, p2.x, p2.y, radius);
}

// This converts the SVG arc to conics based on the SVG standard.
// Code source:
// 1. kdelibs/kdecore/svgicons Niko's code
// 2. webkit/chrome SVGPathNormalizer::decomposeArcToCubic()
// See also SVG implementation notes:
// http://www.w3.org/TR/SVG/implnote.html#ArcConversionEndpointToCenter
// Note that arcSweep bool value is flipped from the original implementation.
void Path::arcTo(float rx, float ry, float xAxisRotate, PathArcSize largeArc, bool reversed,
                 Point endPoint) {
  std::array<Point, 2> srcPoints;
  this->getLastPoint(&srcPoints[0]);
  // If rx = 0 or ry = 0 then this arc is treated as a straight line segment (a "lineto")
  // joining the endpoints.
  // http://www.w3.org/TR/SVG/implnote.html#ArcOutOfRangeParameters
  if (FloatNearlyZero(rx) && FloatNearlyZero(ry)) {
    return this->lineTo(endPoint);
  }
  // If the current point and target point for the arc are identical, it should be treated as a
  // zero length path. This ensures continuity in animations.
  srcPoints[1] = endPoint;
  if (srcPoints[0] == srcPoints[1]) {
    return this->lineTo(endPoint);
  }
  rx = std::abs(rx);
  ry = std::abs(ry);
  auto midPointDistance = (srcPoints[0] - srcPoints[1]) * 0.5f;

  auto pointTransform = Matrix::MakeRotate(-xAxisRotate);
  Point transformedMidPoint = {};
  pointTransform.mapPoints(&transformedMidPoint, &midPointDistance, 1);
  auto squareRx = rx * rx;
  auto squareRy = ry * ry;
  auto squareX = transformedMidPoint.x * transformedMidPoint.x;
  auto squareY = transformedMidPoint.y * transformedMidPoint.y;

  // Check if the radii are big enough to draw the arc, scale radii if not.
  // http://www.w3.org/TR/SVG/implnote.html#ArcCorrectionOutOfRangeRadii
  auto radiiScale = squareX / squareRx + squareY / squareRy;
  if (radiiScale > 1) {
    radiiScale = std::sqrt(radiiScale);
    rx *= radiiScale;
    ry *= radiiScale;
  }

  pointTransform.setScale(1.0f / rx, 1.0f / ry);
  pointTransform.preRotate(-xAxisRotate);

  std::array<Point, 2> unitPoints;
  pointTransform.mapPoints(unitPoints.data(), srcPoints.data(), unitPoints.size());
  auto delta = unitPoints[1] - unitPoints[0];

  auto d = delta.x * delta.x + delta.y * delta.y;
  auto scaleFactorSquared = std::max(1 / d - 0.25f, 0.f);

  auto scaleFactor = std::sqrt(scaleFactorSquared);
  if (reversed != static_cast<bool>(largeArc)) {  // flipped from the original implementation
    scaleFactor = -scaleFactor;
  }
  delta *= scaleFactor;
  auto centerPoint = unitPoints[0] + unitPoints[1];
  centerPoint *= 0.5f;
  centerPoint.offset(-delta.y, delta.x);
  unitPoints[0] -= centerPoint;
  unitPoints[1] -= centerPoint;
  auto theta1 = std::atan2(unitPoints[0].y, unitPoints[0].x);
  auto theta2 = std::atan2(unitPoints[1].y, unitPoints[1].x);
  auto thetaArc = theta2 - theta1;
  if (thetaArc < 0 && !reversed) {  // arcSweep flipped from the original implementation
    thetaArc += M_PI_F * 2.0f;
  } else if (thetaArc > 0 && reversed) {  // arcSweep flipped from the original implementation
    thetaArc -= M_PI_F * 2.0f;
  }

  // Very tiny angles cause our subsequent math to go wonky
  // but a larger value is probably ok too.
  if (std::abs(thetaArc) < (M_PI_F / (1000 * 1000))) {
    return this->lineTo(endPoint);
  }

  pointTransform.setRotate(xAxisRotate);
  pointTransform.preScale(rx, ry);

  // the arc may be slightly bigger than 1/4 circle, so allow up to 1/3rd
  auto segments = std::ceil(std::abs(thetaArc / (2 * M_PI_F / 3)));
  auto thetaWidth = thetaArc / segments;
  auto t = std::tan(0.5f * thetaWidth);
  if (!FloatsAreFinite(&t, 1)) {
    return;
  }
  auto startTheta = theta1;
  auto conicW = std::sqrt(0.5f + std::cos(thetaWidth) * 0.5f);
  auto float_is_integer = [](float scalar) -> bool { return scalar == std::floor(scalar); };
  auto expectIntegers = FloatNearlyZero(M_PI_F * 0.5f - std::abs(thetaWidth)) &&
                        float_is_integer(rx) && float_is_integer(ry) &&
                        float_is_integer(endPoint.x) && float_is_integer(endPoint.y);

  auto path = &(writableRef()->path);
  for (int i = 0; i < static_cast<int>(segments); ++i) {
    auto endTheta = startTheta + thetaWidth;
    auto sinEndTheta = SinSnapToZero(endTheta);
    auto cosEndTheta = CosSnapToZero(endTheta);

    unitPoints[1].set(cosEndTheta, sinEndTheta);
    unitPoints[1] += centerPoint;
    unitPoints[0] = unitPoints[1];
    unitPoints[0].offset(t * sinEndTheta, -t * cosEndTheta);
    std::array<Point, 2> mapped;
    pointTransform.mapPoints(mapped.data(), unitPoints.data(), unitPoints.size());

    // Computing the arc width introduces rounding errors that cause arcs to start outside their
    // marks.A round rect may lose convexity as a result.If the input values are on integers,
    // place the conic on integers as well.
    if (expectIntegers) {
      for (auto& point : mapped) {
        point.x = std::round(point.x);
        point.y = std::round(point.y);
      }
    }
    path->conicTo(mapped[0].x, mapped[0].y, mapped[1].x, mapped[1].y, conicW);
    startTheta = endTheta;
  }

  // The final point should match the input point (by definition); replace it to
  // ensure that rounding errors in the above math don't cause any problems.
  path->setLastPt(endPoint.x, endPoint.y);
}

void Path::close() {
  writableRef()->path.close();
}

void Path::addRect(const Rect& rect, bool reversed, unsigned startIndex) {
  addRect(rect.left, rect.top, rect.right, rect.bottom, reversed, startIndex);
}

void Path::addRect(float left, float top, float right, float bottom, bool reversed,
                   unsigned startIndex) {
  std::vector<Point> points = {};
  points.push_back({left, top});
  points.push_back({right, top});
  points.push_back({right, bottom});
  points.push_back({left, bottom});
  PointIterator iter(points, reversed, startIndex);
  auto path = &(writableRef()->path);
  path->moveTo(iter.current());
  path->lineTo(iter.next());
  path->lineTo(iter.next());
  path->lineTo(iter.next());
  path->close();
}

static std::vector<Point> GetArcPoints(float centerX, float centerY, float radiusX, float radiusY,
                                       float startRadius, float endRadius, int* numBeziers) {
  std::vector<Point> points = {};
  float start = startRadius;
  auto increasing = startRadius < endRadius;
  float end =
      increasing ? std::min(endRadius, start + M_PI_2_F) : std::max(endRadius, start - M_PI_2_F);
  float currentX, currentY;
  *numBeziers = 0;
  for (int i = 0; i < 4; i++) {
    auto radStep = end - start;
    auto distance = DistanceToControlPoint(radStep);
    auto u = cosf(start);
    auto v = sinf(start);
    currentX = centerX + u * radiusX;
    currentY = centerY + v * radiusY;
    points.push_back({currentX, currentY});
    auto x1 = currentX - v * distance * radiusX;
    auto y1 = currentY + u * distance * radiusY;
    points.push_back({x1, y1});
    u = cosf(end);
    v = sinf(end);
    currentX = centerX + u * radiusX;
    currentY = centerY + v * radiusY;
    auto x2 = currentX + v * distance * radiusX;
    auto y2 = currentY - u * distance * radiusY;
    points.push_back({x2, y2});
    (*numBeziers)++;
    if (end == endRadius) {
      points.push_back({currentX, currentY});
      break;
    }
    start = end;
    end =
        increasing ? std::min(endRadius, start + M_PI_2_F) : std::max(endRadius, start - M_PI_2_F);
  }
  return points;
}

void Path::addOval(const Rect& oval, bool reversed, unsigned startIndex) {
  writableRef()->path.addOval(ToSkRect(oval), ToSkDirection(reversed), startIndex);
}

void Path::addArc(const Rect& oval, float startAngle, float sweepAngle) {
  if (oval.isEmpty() || sweepAngle == 0.0f) {
    return;
  }
  constexpr auto kFullCircleAngle = static_cast<float>(360);
  if (sweepAngle >= kFullCircleAngle || sweepAngle <= -kFullCircleAngle) {
    // We can treat the arc as an oval if it begins at one of our legal starting positions.
    float startOver90 = startAngle / 90.f;
    float startOver90I = std::roundf(startOver90);
    float error = startOver90 - startOver90I;
    if (FloatNearlyEqual(error, 0)) {
      // Index 1 is at startAngle == 0.
      float startIndex = std::fmod(startOver90I + 1.f, 4.f);
      startIndex = startIndex < 0 ? startIndex + 4.f : startIndex;
      return addOval(oval, sweepAngle < 0, static_cast<unsigned>(startIndex));
    }
  }
  auto startRad = DegreesToRadians(startAngle);
  auto sweepRad = DegreesToRadians(sweepAngle);
  auto radiusX = oval.width() * 0.5f;
  auto radiusY = oval.height() * 0.5f;
  auto endRad = startRad + sweepRad;
  int numBeziers = 0;
  auto points =
      GetArcPoints(oval.centerX(), oval.centerY(), radiusX, radiusY, startRad, endRad, &numBeziers);
  PointIterator iter(points, false, 0);
  auto path = &(writableRef()->path);
  path->moveTo(iter.current());
  for (int i = 0; i < numBeziers; i++) {
    // Bind each iter.next() to a local first; argument evaluation order is unspecified, and MSVC
    // evaluates right-to-left while Clang/GCC go left-to-right, which would otherwise reverse the
    // (c1, c2, end) order on Windows and produce a corrupted path.
    auto c1 = iter.next();
    auto c2 = iter.next();
    auto end = iter.next();
    path->cubicTo(c1, c2, end);
  }
}

void Path::addRoundRect(const Rect& rect, float radiusX, float radiusY, bool reversed,
                        unsigned startIndex) {
  writableRef()->path.addRRect(SkRRect::MakeRectXY(ToSkRect(rect), radiusX, radiusY),
                               ToSkDirection(reversed), startIndex);
}

void Path::addRoundRect(const Rect& rect, const std::array<Point, 4>& radii, bool reversed,
                        unsigned startIndex) {
  SkRRect skRRect;
  skRRect.setRectRadii(ToSkRect(rect), reinterpret_cast<const SkPoint*>(radii.data()));
  writableRef()->path.addRRect(skRRect, ToSkDirection(reversed), startIndex);
}

/**
 * Bezier points of one smoothed corner of a rounded rectangle, in clockwise order.
 */
struct SmoothCornerPoints {
  // Incoming transition curve.
  Point entry = {};
  Point entryCtrl1 = {};
  Point entryCtrl2 = {};
  // Retained arc.
  Point arcStart = {};
  Point arcCtrl1 = {};
  Point arcCtrl2 = {};
  // Outgoing transition curve.
  Point arcEnd = {};
  Point exitCtrl1 = {};
  Point exitCtrl2 = {};
  Point exit = {};
};

static void ScalePointY(Point& point, float originY, float scale) {
  point.y = originY + (point.y - originY) * scale;
}

/**
 * Scales the corner points vertically about the line y = originY.
 * @param corner the corner points, scaled in place
 * @param originY the y coordinate that stays fixed under the scale
 * @param scale the vertical scale factor
 */
static void ScaleSmoothCornerY(SmoothCornerPoints& corner, float originY, float scale) {
  ScalePointY(corner.entry, originY, scale);
  ScalePointY(corner.entryCtrl1, originY, scale);
  ScalePointY(corner.entryCtrl2, originY, scale);
  ScalePointY(corner.arcStart, originY, scale);
  ScalePointY(corner.arcCtrl1, originY, scale);
  ScalePointY(corner.arcCtrl2, originY, scale);
  ScalePointY(corner.arcEnd, originY, scale);
  ScalePointY(corner.exitCtrl1, originY, scale);
  ScalePointY(corner.exitCtrl2, originY, scale);
  ScalePointY(corner.exit, originY, scale);
}

/**
 * Builds the points of one smoothed corner.
 * @param vertex the corner vertex
 * @param entryDir the unit tangent at the entry point, along the incoming edge toward the vertex
 * @param entryNormal the unit normal at the entry point, pointing into the shape
 * @param exitDir the unit tangent at the exit point, along the outgoing edge away from the vertex
 * @param exitNormal the unit normal at the exit point, pointing into the shape
 * @param radii the corner radii on the x and y axes
 * @param coeffs the coefficients of the smoothed corner with unit radius
 * @return the points of the corner contour
 */
static SmoothCornerPoints BuildSmoothCorner(const Point& vertex, const Point& entryDir,
                                            const Point& entryNormal, const Point& exitDir,
                                            const Point& exitNormal, const Point& radii,
                                            const UnitSmoothCornerCoeffs& coeffs) {
  const auto radius = radii.x;
  const auto start = coeffs.start * radius;
  const auto step = coeffs.step * radius;
  const auto arcOffsetAlong = coeffs.arcOffsetAlong * radius;
  const auto arcOffsetCross = coeffs.arcOffsetCross * radius;
  SmoothCornerPoints corner = {};
  corner.entry = vertex - entryDir * start;
  corner.entryCtrl1 = corner.entry + entryDir * (2.0f * step);
  corner.entryCtrl2 = corner.entry + entryDir * (3.0f * step);
  corner.arcStart = corner.entryCtrl2 + entryDir * arcOffsetAlong + entryNormal * arcOffsetCross;
  corner.exit = vertex + exitDir * start;
  corner.exitCtrl2 = corner.exit - exitDir * (2.0f * step);
  corner.exitCtrl1 = corner.exit - exitDir * (3.0f * step);
  corner.arcEnd = corner.exitCtrl1 - exitDir * arcOffsetAlong + exitNormal * arcOffsetCross;
  const auto center = vertex + (entryNormal + exitNormal) * radius;
  // Middle point of the retained arc.
  const auto mid = center - (entryNormal + exitNormal) * (radius / FLOAT_SQRT2);
  // The tangent vectors below keep the length of the radius so that multiplying by arcHandle (a
  // ratio of the radius) yields the actual handle length.
  auto tangentStart = Point{center.y - corner.arcStart.y, corner.arcStart.x - center.x};
  // Orient the tangent at arcStart along the direction of travel, toward mid.
  if (tangentStart.x * (mid.x - corner.arcStart.x) + tangentStart.y * (mid.y - corner.arcStart.y) <
      0.0f) {
    tangentStart = {-tangentStart.x, -tangentStart.y};
  }
  corner.arcCtrl1 = corner.arcStart + tangentStart * coeffs.arcHandle;
  auto tangentEnd = Point{center.y - corner.arcEnd.y, corner.arcEnd.x - center.x};
  // Orient the tangent at arcEnd along the direction of travel, away from mid.
  if (tangentEnd.x * (corner.arcEnd.x - mid.x) + tangentEnd.y * (corner.arcEnd.y - mid.y) < 0.0f) {
    tangentEnd = {-tangentEnd.x, -tangentEnd.y};
  }
  corner.arcCtrl2 = corner.arcEnd - tangentEnd * coeffs.arcHandle;
  if (radii.x > 0.0f && radii.y != radii.x) {
    ScaleSmoothCornerY(corner, vertex.y, radii.y / radii.x);
  }
  return corner;
}

static void AppendSmoothCorner(SkPath& path, const SmoothCornerPoints& corner, bool hasArc,
                               bool reversed) {
  Point points[10] = {corner.entry,     corner.entryCtrl1, corner.entryCtrl2, corner.arcStart,
                      corner.arcCtrl1,  corner.arcCtrl2,   corner.arcEnd,     corner.exitCtrl1,
                      corner.exitCtrl2, corner.exit};
  if (reversed) {
    std::reverse(points, points + 10);
  }
  path.cubicTo(ToSkPoint(points[1]), ToSkPoint(points[2]), ToSkPoint(points[3]));
  if (hasArc) {
    path.cubicTo(ToSkPoint(points[4]), ToSkPoint(points[5]), ToSkPoint(points[6]));
  }
  path.cubicTo(ToSkPoint(points[7]), ToSkPoint(points[8]), ToSkPoint(points[9]));
}

/**
 * Appends a smoothed rounded rectangle to the path as a new closed contour.
 * @param path the path that receives the contour
 * @param rRect the smoothed rounded rectangle
 * @param reversed whether the contour runs in the reverse direction
 * @param startIndex the boundary point where the contour starts, numbered as for a general rounded
 *        rectangle and taken modulo 8
 */
static void AddSmoothRRect(SkPath& path, const RRect& rRect, bool reversed, unsigned startIndex) {
  const auto coeffs = RRectUtils::ComputeUnitSmoothCornerCoeffs(rRect.smoothness());
  // Per-corner edge vectors in clockwise order (top-left, top-right, bottom-right, bottom-left):
  // entryDir, entryNormal, exitDir, exitNormal.
  static constexpr Point CORNER_EDGE_VECTORS[4][4] = {
      {{0, -1}, {1, 0}, {1, 0}, {0, 1}},
      {{1, 0}, {0, 1}, {0, 1}, {-1, 0}},
      {{0, 1}, {-1, 0}, {-1, 0}, {0, -1}},
      {{-1, 0}, {0, -1}, {0, -1}, {1, 0}},
  };
  const auto& rect = rRect.rect();
  const Point vertices[4] = {{rect.left, rect.top},
                             {rect.right, rect.top},
                             {rect.right, rect.bottom},
                             {rect.left, rect.bottom}};
  SmoothCornerPoints corners[4] = {};
  for (size_t i = 0; i < 4; ++i) {
    const auto& vectors = CORNER_EDGE_VECTORS[i];
    corners[i] = BuildSmoothCorner(vertices[i], vectors[0], vectors[1], vectors[2], vectors[3],
                                   rRect.radii()[i], coeffs);
  }
  // Boundary points between corners, indexed as startIndex: even indices end a corner, odd indices
  // start one.
  const Point boundary[8] = {
      corners[0].exit, corners[1].entry, corners[1].exit, corners[2].entry,
      corners[2].exit, corners[3].entry, corners[3].exit, corners[0].entry,
  };
  const auto hasArc = rRect.smoothness() < 1.0f;
  auto index = startIndex % 8;
  path.moveTo(ToSkPoint(boundary[index]));
  for (unsigned i = 0; i < 8; ++i) {
    if (reversed) {
      const auto next = (index + 7) % 8;
      if (index % 2 == 0) {
        AppendSmoothCorner(path, corners[index / 2], hasArc, true);
      } else {
        path.lineTo(ToSkPoint(boundary[next]));
      }
      index = next;
    } else {
      const auto next = (index + 1) % 8;
      if (index % 2 == 1) {
        AppendSmoothCorner(path, corners[(index + 1) / 2 % 4], hasArc, false);
      } else {
        path.lineTo(ToSkPoint(boundary[next]));
      }
      index = next;
    }
  }
  path.close();
}

void Path::addRRect(const RRect& rRect, bool reversed, unsigned int startIndex) {
  if (rRect.smoothness() > 0.0f) {
    AddSmoothRRect(writableRef()->path, rRect, reversed, startIndex);
    return;
  }
  SkRRect skRRect;
  skRRect.setRectRadii(ToSkRect(rRect.rect()),
                       reinterpret_cast<const SkPoint*>(rRect.radii().data()));
  writableRef()->path.addRRect(skRRect, ToSkDirection(reversed), startIndex);
}

bool Path::addPath(const Path& src, PathOp op) {
  auto& path = writableRef()->path;
  const auto& newPath = src.pathRef->path;
  if (op == PathOp::Append || op == PathOp::Extend) {
    if (path.isEmpty()) {
      path = newPath;
    } else {
      auto mode = op == PathOp::Extend ? SkPath::kExtend_AddPathMode : SkPath::kAppend_AddPathMode;
      path.addPath(newPath, mode);
    }
    return true;
  }
  SkPathOp pathOp;
  switch (op) {
    case PathOp::Intersect:
      pathOp = SkPathOp::kIntersect_SkPathOp;
      break;
    case PathOp::Difference:
      pathOp = SkPathOp::kDifference_SkPathOp;
      break;
    case PathOp::XOR:
      pathOp = SkPathOp::kXOR_SkPathOp;
      break;
    default:
      pathOp = SkPathOp::kUnion_SkPathOp;
      break;
  }
  return Op(path, newPath, pathOp, &path);
}

struct PathAreaBounds {
  size_t index = 0;
  Rect bounds = {};
  float start = 0.0f;
  float end = 0.0f;
};

static bool ComparePathAreaBounds(const PathAreaBounds& first, const PathAreaBounds& second) {
  return first.start < second.start || (first.start == second.start && first.index < second.index);
}

static std::vector<PathAreaBounds> SortPathAreaBounds(const std::vector<Path>& paths) {
  Rect totalBounds = {};
  std::vector<PathAreaBounds> bounds = {};
  bounds.reserve(paths.size());
  for (size_t index = 0; index < paths.size(); ++index) {
    auto rect = paths[index].getBounds();
    totalBounds.join(rect);
    bounds.push_back({index, rect, 0.0f, 0.0f});
  }
  auto horizontal = static_cast<double>(totalBounds.right) - totalBounds.left >=
                    static_cast<double>(totalBounds.bottom) - totalBounds.top;
  for (auto& entry : bounds) {
    entry.start = horizontal ? entry.bounds.left : entry.bounds.top;
    entry.end = horizontal ? entry.bounds.right : entry.bounds.bottom;
  }
  std::sort(bounds.begin(), bounds.end(), ComparePathAreaBounds);
  return bounds;
}

static bool CanConcatenateAreas(const std::vector<Path>& paths) {
  if (paths.size() <= 1) {
    return true;
  }
  auto fillType = paths.front().getFillType();
  for (const auto& path : paths) {
    if (path.isInverseFillType() || path.getFillType() != fillType) {
      return false;
    }
  }
  auto sortedBounds = SortPathAreaBounds(paths);
  std::vector<const PathAreaBounds*> active = {};
  for (const auto& entry : sortedBounds) {
    size_t retained = 0;
    for (const auto* previous : active) {
      if (previous->end <= entry.start) {
        continue;
      }
      active[retained++] = previous;
      if (Rect::Intersects(entry.bounds, previous->bounds)) {
        pk::SkPath intersection;
        if (!pk::Op(PathRef::ReadAccess(paths[entry.index]),
                    PathRef::ReadAccess(paths[previous->index]), pk::SkPathOp::kIntersect_SkPathOp,
                    &intersection) ||
            !intersection.isEmpty()) {
          return false;
        }
      }
    }
    active.resize(retained);
    active.push_back(&entry);
  }
  return true;
}

std::optional<Path> Path::Merge(const std::vector<Path>& sources) {
  std::vector<Path> nonempty = {};
  for (const auto& source : sources) {
    if (!source.pathRef->path.isFinite()) {
      return std::nullopt;
    }
    if (!source.isEmpty() || source.isInverseFillType()) {
      nonempty.push_back(source);
    }
  }
  Path merged = {};
  if (nonempty.empty()) {
    return merged;
  }
  // Whether the sources can simply be concatenated decides what has to be computed. Measuring it
  // first is what keeps the common case cheap: text that does not overlap never reaches the boolean
  // pass, and overlapping text never pays for the check that the concatenation reproduces the union
  // -- both are expensive, and only one of them is ever needed.
  if (CanConcatenateAreas(nonempty)) {
    merged = nonempty.front();
    for (size_t i = 1; i < nonempty.size(); ++i) {
      merged.addPath(nonempty[i]);
    }
    return merged;
  }
  pk::SkOpBuilder builder;
  // resolve() may reverse operands while testing its fast path before falling back to Op(). The
  // neutral first operand ensures each source is resolved under its own fill rule first.
  builder.add(pk::SkPath(), pk::SkPathOp::kUnion_SkPathOp);
  for (const auto& source : nonempty) {
    builder.add(source.pathRef->path, pk::SkPathOp::kUnion_SkPathOp);
  }
  pk::SkPath result;
  if (!builder.resolve(&result)) {
    return std::nullopt;
  }
  bool allWinding = true;
  for (const auto& source : nonempty) {
    allWinding = allWinding && source.getFillType() == PathFillType::Winding;
  }
  if (allWinding) {
    // Retain contour order only after checking the candidate against the actual union. This is not
    // a direction heuristic; rejection or a nonempty difference keeps the boolean result instead.
    // Overlapping sources can still agree on the area -- two rectangles that share a strip are one
    // outline either way -- and keeping the contours the sources came in with is what keeps such a
    // run from being redrawn by the union.
    auto candidate = nonempty.front();
    for (size_t i = 1; i < nonempty.size(); ++i) {
      candidate.addPath(nonempty[i]);
    }
    // Only the candidate is read as winding, and the result is left as the union wrote it. Both
    // sides have to be read the same way for the difference to mean anything, but making them agree
    // by rewriting the result instead would also make it agree in the cases where it must not:
    // contours that cancel under winding are exactly why the union came back even-odd, and those
    // runs have to keep the boolean result. Reading the candidate alone is what makes the check
    // fail there, which is the answer those runs need.
    PathRef::WriteAccess(candidate).setFillType(pk::SkPathFillType::kWinding);
    pk::SkPath difference;
    if (pk::Op(PathRef::ReadAccess(candidate), result, pk::SkPathOp::kXOR_SkPathOp, &difference) &&
        difference.isEmpty()) {
      return candidate;
    }
  }
  merged.writableRef()->path = result;
  return merged;
}

void Path::reset() {
  pathRef = std::make_shared<PathRef>();
}

void Path::transform(const Matrix& matrix) {
  if (matrix.isIdentity()) {
    return;
  }
  float values[9] = {};
  matrix.get9(values);
  SkMatrix skMatrix = {};
  skMatrix.set9(values);
  writableRef()->path.transform(skMatrix);
}

void Path::transform3D(const Matrix3D& matrix) {
  if (matrix.isIdentity()) {
    return;
  }
  float values[16] = {};
  matrix.getColumnMajor(values);
  SkMatrix skMatrix = {};
  // All vertices inside the Path have an initial z-coordinate of 0, so the third column of the 4x4
  // matrix does not affect the final transformation result and can be ignored. Additionally, since
  // we do not care about the final projected z-axis coordinate, the third row can also be ignored.
  // Therefore, the 4x4 matrix can be simplified to a 3x3 matrix.
  skMatrix.setAll(values[0], values[4], values[12], values[1], values[5], values[13], values[3],
                  values[7], values[15]);
  writableRef()->path.transform(skMatrix);
}

void Path::reverse() {
  auto& path = writableRef()->path;
  auto fillType = path.getFillType();
  SkPath tempPath;
  tempPath.reverseAddPath(path);
  tempPath.setFillType(fillType);
  path = tempPath;
}

PathRef* Path::writableRef() {
  if (pathRef.use_count() != 1) {
    pathRef = std::make_shared<PathRef>(pathRef->path);
  } else {
    // There only one reference to this PathRef, so we can safely reset the uniqueKey and bounds.
    pathRef->uniqueKey.reset();
    AtomicCacheReset(pathRef->bounds);
  }
  return pathRef.get();
}

int Path::countPoints() const {
  return pathRef->path.countPoints();
}

int Path::countVerbs() const {
  return pathRef->path.countVerbs();
}

bool Path::getLastPoint(Point* lastPoint) const {
  if (!lastPoint) {
    return false;
  }
  auto skPoint = SkPoint::Make(0, 0);
  if (pathRef->path.getLastPt(&skPoint)) {
    lastPoint->set(skPoint.fX, skPoint.fY);
    return true;
  }
  return false;
};

/////////////////////////////////////////////////////////////////////////////////////////////////
// Path::Iterator implementation
/////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(sizeof(pk::SkPath::Iter) <= 64,
              "Path::Iterator storage size is too small for SkPath::Iter");
static_assert(alignof(pk::SkPath::Iter) <= 8,
              "Path::Iterator storage alignment is insufficient for SkPath::Iter");

Path::Iterator::Iterator(const Path* path) : isDone(false) {
  new (storage) pk::SkPath::Iter(PathRef::ReadAccess(*path), false);
  // Advance to the first segment.
  ++(*this);
}

Path::Iterator::Iterator() = default;

Path::Iterator::~Iterator() {
  if (!isDone) {
    reinterpret_cast<pk::SkPath::Iter*>(storage)->~Iter();
  }
}

Path::Iterator::Iterator(const Iterator& other) : current(other.current), isDone(other.isDone) {
  if (!isDone) {
    new (storage) pk::SkPath::Iter(*reinterpret_cast<const pk::SkPath::Iter*>(other.storage));
  }
}

Path::Iterator& Path::Iterator::operator=(const Iterator& other) {
  if (this == &other) {
    return *this;
  }
  if (!isDone) {
    reinterpret_cast<pk::SkPath::Iter*>(storage)->~Iter();
  }
  current = other.current;
  isDone = other.isDone;
  if (!isDone) {
    new (storage) pk::SkPath::Iter(*reinterpret_cast<const pk::SkPath::Iter*>(other.storage));
  }
  return *this;
}

Path::Iterator& Path::Iterator::operator++() {
  advance();
  return *this;
}

void Path::Iterator::advance() {
  auto* iter = reinterpret_cast<pk::SkPath::Iter*>(storage);
  // Point and SkPoint have identical memory layout (two consecutive floats),
  // so we can directly pass current.points as SkPoint* to avoid memcpy.
  auto* pts = reinterpret_cast<pk::SkPoint*>(current.points);
  auto verb = iter->next(pts);
  if (verb == pk::SkPath::kDone_Verb) {
    isDone = true;
    iter->~Iter();
    current = {};
  } else {
    current.verb = static_cast<PathVerb>(verb);
    if (verb == pk::SkPath::kConic_Verb) {
      current.conicWeight = iter->conicWeight();
    } else {
      current.conicWeight = 0.0f;
    }
  }
}

Path::Iterator Path::begin() const {
  if (isEmpty()) {
    return Iterator();
  }
  return Iterator(this);
}

std::vector<Point> Path::ConvertConicToQuads(const Point& p0, const Point& p1, const Point& p2,
                                             float weight, int pow2) {
  size_t maxQuads = static_cast<size_t>(1) << pow2;
  std::vector<Point> quads(1 + (2 * maxQuads));
  int numQuads = SkPath::ConvertConicToQuads(*reinterpret_cast<const SkPoint*>(&p0),
                                             *reinterpret_cast<const SkPoint*>(&p1),
                                             *reinterpret_cast<const SkPoint*>(&p2), weight,
                                             reinterpret_cast<SkPoint*>(quads.data()), pow2);
  quads.resize(1 + (2 * static_cast<size_t>(numQuads)));
  return quads;
}

bool Path::toWinding() {
  auto fillType = getFillType();
  if (fillType == PathFillType::Winding || fillType == PathFillType::InverseWinding) {
    return true;
  }
  auto& path = PathRef::WriteAccess(*this);
  // AsWinding() decides each contour's direction from the nesting depth it sits at, reversing the
  // ones that disagree with the contour containing them. An outermost contour keeps the direction
  // it already has, so two of them that overlap while running opposite ways still cancel and leave
  // the overlap empty -- forcing every outermost contour to one shared direction, which no fill
  // rule asks for, would fill it in. It copies the curves it was given rather than rebuilding them,
  // so the geometry survives unchanged. It does not detect self-crossings or contours that cross
  // each other, so those still come back with the filled area changed; see Path::toWinding().
  //
  // The other pathkit entry for this conversion is deliberately not used. SkOpBuilder - add() once
  // and resolve(), which runs Simplify followed by FixWinding - decides the directions the same
  // way, but Simplify rebuilds the contours and quantises their coordinates, so a counter a
  // fraction of a unit wide closes up: PathTest.ToWindingKeepsThinCurveCounter fails under it while
  // passing here. Only reversing contours is what keeps that geometry.
  SkPath result;
  if (!AsWinding(path, &result)) {
    return false;
  }
  result.setFillType(fillType == PathFillType::InverseEvenOdd ? SkPathFillType::kInverseWinding
                                                              : SkPathFillType::kWinding);
  path = result;
  return true;
}

}  // namespace tgfx
