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
#include <functional>
#include <limits>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#include <include/core/SkPath.h>
#pragma clang diagnostic pop
#include <include/core/SkPathTypes.h>
#include <include/core/SkRect.h>
#include <include/pathops/SkPathOps.h>
#include "core/PathRef.h"
#include "core/utils/AtomicCache.h"
#include "core/utils/MathExtra.h"

namespace tgfx {
using namespace pk;

static SkPathDirection ToSkDirection(bool reversed) {
  return reversed ? SkPathDirection::kCCW : SkPathDirection::kCW;
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
  std::function<float()> increaseRadius;
  if (startRadius < endRadius) {
    increaseRadius = [&] { return std::min(endRadius, start + M_PI_2_F); };
  } else {
    increaseRadius = [&] { return std::max(endRadius, start - M_PI_2_F); };
  }
  float end = increaseRadius();
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
    end = increaseRadius();
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

void Path::addRRect(const RRect& rRect, bool reversed, unsigned int startIndex) {
  SkRRect skRRect;
  skRRect.setRectRadii(ToSkRect(rRect.rect()),
                       reinterpret_cast<const SkPoint*>(rRect.radii().data()));
  writableRef()->path.addRRect(skRRect, ToSkDirection(reversed), startIndex);
}

void Path::addPath(const Path& src, PathOp op) {
  addPath(src, op, nullptr);
}

void Path::addPath(const Path& src, PathOp op, bool* success) {
  if (success != nullptr) {
    *success = true;
  }
  auto& path = writableRef()->path;
  const auto& newPath = src.pathRef->path;
  if (op == PathOp::Append || op == PathOp::Extend) {
    if (path.isEmpty()) {
      path = newPath;
    } else {
      auto mode = op == PathOp::Extend ? SkPath::kExtend_AddPathMode : SkPath::kAppend_AddPathMode;
      path.addPath(newPath, mode);
    }
    return;
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
  auto ok = Op(path, newPath, pathOp, &path);
  if (success != nullptr) {
    *success = ok;
  }
}

namespace {

// The number of samples used to flatten a curve while measuring a contour. Only the sign of the
// area and an approximate bounding box are needed, so this never has to be exact.
constexpr int CURVE_SAMPLES = 32;

// How many containment candidates a contour is thinned down to. Every one of them has to agree,
// so this bounds the cost of a containment test without letting a long contour go unrepresented.
constexpr size_t MAX_PROBE_COUNT = 8;

// One contour split out of a path. The original verb sequence is kept so the contour can be
// written back without changing its geometry in any way.
struct ContourInfo {
  std::vector<Path::Segment> segments = {};
  bool closed = false;
  Path path = {};
  // Bounds of the flattened outline. Used to pick the innermost container.
  float tightLeft = 0.0f;
  float tightTop = 0.0f;
  float tightRight = 0.0f;
  float tightBottom = 0.0f;
  // Bounds including control points: a superset of the true area, used as a cheap prefilter.
  float fatLeft = 0.0f;
  float fatTop = 0.0f;
  float fatRight = 0.0f;
  float fatBottom = 0.0f;
  // Candidate interior points spread over the contour. A contour counts as nested inside another
  // only when every one of them lies inside it, so a single point that happens to land on a
  // boundary or on a crossing cannot decide the relation on its own.
  std::vector<Point> probes = {};
  double signedArea = 0.0;
  // Exact area accumulated per segment, measured in the frame that puts this contour's first point
  // at (0, 0). Translation leaves the signed area alone but shrinks the cross products, and a thin
  // outline whose two sides almost cancel would otherwise be decided by a difference far smaller
  // than the sampling error - flipping the contour the wrong way.
  double exactArea = 0.0;
  // Conics have no polynomial area formula, so their contribution is carried as a chord sum plus a
  // bound on how far the arc can stray from those chords. The sign is trusted only while it is
  // larger than the bound; otherwise the direction is not decidable and the conversion fails.
  std::vector<Path::Segment> conics = {};
  Point origin = {};
  int depth = 0;
  bool reverse = false;
};

Point EvalQuad(const Point& p0, const Point& p1, const Point& p2, float t) {
  auto u = 1.0f - t;
  return {u * u * p0.x + 2.0f * u * t * p1.x + t * t * p2.x,
          u * u * p0.y + 2.0f * u * t * p1.y + t * t * p2.y};
}

Point EvalCubic(const Point& p0, const Point& p1, const Point& p2, const Point& p3, float t) {
  auto u = 1.0f - t;
  auto uu = u * u;
  auto tt = t * t;
  return {uu * u * p0.x + 3.0f * uu * t * p1.x + 3.0f * u * tt * p2.x + tt * t * p3.x,
          uu * u * p0.y + 3.0f * uu * t * p1.y + 3.0f * u * tt * p2.y + tt * t * p3.y};
}

Point EvalConic(const Point& p0, const Point& p1, const Point& p2, float weight, float t) {
  auto u = 1.0f - t;
  auto divisor = u * u + 2.0f * weight * u * t + t * t;
  if (divisor == 0.0f) {
    return p0;
  }
  return {(u * u * p0.x + 2.0f * weight * u * t * p1.x + t * t * p2.x) / divisor,
          (u * u * p0.y + 2.0f * weight * u * t * p1.y + t * t * p2.y) / divisor};
}

void GrowBounds(const Point& point, float& left, float& top, float& right, float& bottom) {
  left = std::min(left, point.x);
  top = std::min(top, point.y);
  right = std::max(right, point.x);
  bottom = std::max(bottom, point.y);
}

// Kept in double: two finite coordinates can span a range whose product leaves the float range
// entirely (a 1e19-wide outline), and an overflowing area would stop that outline from ever being
// picked as a container, so its counters would be read at the wrong depth.
double BoundsArea(float left, float top, float right, float bottom) {
  auto width = static_cast<double>(right) - static_cast<double>(left);
  auto height = static_cast<double>(bottom) - static_cast<double>(top);
  if (width <= 0.0 || height <= 0.0) {
    return 0.0;
  }
  return width * height;
}

// Signed area of one segment under Green's theorem, integrated exactly instead of by flattening.
// The integrand of a line, quadratic or cubic is a polynomial, so a three-point Gauss-Legendre
// rule - exact for polynomials up to degree five - gives the true value. That matters for thin
// outlines: their area is a small difference between large terms, which sampling gets wrong.
double ExactSegmentArea(const Path::Segment& segment, const Point& origin) {
  static constexpr double kNodes[3] = {0.5 - 0.5 * 0.7745966692414834, 0.5,
                                       0.5 + 0.5 * 0.7745966692414834};
  static constexpr double kWeights[3] = {5.0 / 18.0, 8.0 / 18.0, 5.0 / 18.0};
  // The coordinates are moved into the contour's own frame before anything is combined: the sum
  // below is translation invariant, but its terms are not, and large coordinates leave a thin
  // contour's sign buried in cancellation.
  //
  // The shift has to happen in double. Subtracting in the float fields first rounds the difference
  // away, so two distinct vertices can collapse into one and the area comes out as exactly zero -
  // which then reads as "no direction", silently skipping the reversal a counter needed.
  auto originX = static_cast<double>(origin.x);
  auto originY = static_cast<double>(origin.y);
  double area = 0.0;
  for (int i = 0; i < 3; i++) {
    auto t = kNodes[i];
    auto u = 1.0 - t;
    double x, y, dx, dy;
    switch (segment.verb) {
      case PathVerb::Line: {
        auto x0 = static_cast<double>(segment.points[0].x) - originX;
        auto y0 = static_cast<double>(segment.points[0].y) - originY;
        auto x1 = static_cast<double>(segment.points[1].x) - originX;
        auto y1 = static_cast<double>(segment.points[1].y) - originY;
        x = x0 + (x1 - x0) * t;
        y = y0 + (y1 - y0) * t;
        dx = x1 - x0;
        dy = y1 - y0;
        break;
      }
      case PathVerb::Quad: {
        auto x0 = static_cast<double>(segment.points[0].x) - originX;
        auto y0 = static_cast<double>(segment.points[0].y) - originY;
        auto x1 = static_cast<double>(segment.points[1].x) - originX;
        auto y1 = static_cast<double>(segment.points[1].y) - originY;
        auto x2 = static_cast<double>(segment.points[2].x) - originX;
        auto y2 = static_cast<double>(segment.points[2].y) - originY;
        x = u * u * x0 + 2.0 * u * t * x1 + t * t * x2;
        y = u * u * y0 + 2.0 * u * t * y1 + t * t * y2;
        dx = 2.0 * u * (x1 - x0) + 2.0 * t * (x2 - x1);
        dy = 2.0 * u * (y1 - y0) + 2.0 * t * (y2 - y1);
        break;
      }
      case PathVerb::Cubic: {
        auto x0 = static_cast<double>(segment.points[0].x) - originX;
        auto y0 = static_cast<double>(segment.points[0].y) - originY;
        auto x1 = static_cast<double>(segment.points[1].x) - originX;
        auto y1 = static_cast<double>(segment.points[1].y) - originY;
        auto x2 = static_cast<double>(segment.points[2].x) - originX;
        auto y2 = static_cast<double>(segment.points[2].y) - originY;
        auto x3 = static_cast<double>(segment.points[3].x) - originX;
        auto y3 = static_cast<double>(segment.points[3].y) - originY;
        x = u * u * u * x0 + 3.0 * u * u * t * x1 + 3.0 * u * t * t * x2 + t * t * t * x3;
        y = u * u * u * y0 + 3.0 * u * u * t * y1 + 3.0 * u * t * t * y2 + t * t * t * y3;
        dx = 3.0 * u * u * (x1 - x0) + 6.0 * u * t * (x2 - x1) + 3.0 * t * t * (x3 - x2);
        dy = 3.0 * u * u * (y1 - y0) + 6.0 * u * t * (y2 - y1) + 3.0 * t * t * (y3 - y2);
        break;
      }
      default:
        return 0.0;
    }
    area += kWeights[i] * 0.5 * (x * dy - y * dx);
  }
  return area;
}

int SegmentPointCount(PathVerb verb) {
  switch (verb) {
    case PathVerb::Line:
      return 2;
    case PathVerb::Quad:
    case PathVerb::Conic:
      return 3;
    default:
      return 4;
  }
}

Point SegmentEndPoint(const Path::Segment& segment) {
  switch (segment.verb) {
    case PathVerb::Line:
      return segment.points[1];
    case PathVerb::Quad:
    case PathVerb::Conic:
      return segment.points[2];
    default:
      return segment.points[3];
  }
}

const Path::Segment* LastCurveSegment(const std::vector<Path::Segment>& segments);

// An outline that was never closed still fills as if it were: both rasterizing and contains()
// treat the gap between the last point and the start as an implicit edge. That edge carries no
// separate term here because every area is measured with the contour's start point at the origin,
// where the edge back to it has a zero cross product.

// One piece of a conic, written in the frame that puts the contour's first point at the origin.
struct ConicPiece {
  double x0 = 0.0;
  double y0 = 0.0;
  double x1 = 0.0;
  double y1 = 0.0;
  double x2 = 0.0;
  double y2 = 0.0;
  double weight = 1.0;
};

// Splits a conic at t == 0.5 with rational de Casteljau. Both halves trace the same points as the
// original, so their signed areas add back up to it. The weights are rescaled to 1 at both ends,
// which reparametrises each half but leaves its trace - and therefore its area - untouched.
void ChopConic(const ConicPiece& piece, ConicPiece* left, ConicPiece* right) {
  auto divisor = 1.0 + piece.weight;
  auto ax = (piece.x0 + piece.weight * piece.x1) / divisor;
  auto ay = (piece.y0 + piece.weight * piece.y1) / divisor;
  auto bx = (piece.weight * piece.x1 + piece.x2) / divisor;
  auto by = (piece.weight * piece.y1 + piece.y2) / divisor;
  auto weight = std::sqrt(0.5 * (1.0 + piece.weight));
  *left = {piece.x0, piece.y0, ax, ay, 0.5 * (ax + bx), 0.5 * (ay + by), weight};
  *right = {0.5 * (ax + bx), 0.5 * (ay + by), bx, by, piece.x2, piece.y2, weight};
}

// Signed area of one conic, as the sum of the chords of its `level` subdivisions plus a bound on
// how far the true arc can stray from those chords. A conic with a positive weight stays inside its
// control triangle, so the region between a chord and its arc is contained in that triangle and can
// never be larger than it. Each extra level quarters the bound.
//
// That bound is what makes the sign decidable: when the chord sum is larger than it, no error
// within the bound can reach past zero. Sampling more finely instead would only shrink the same
// one-sided error without ever measuring it.
void ConicAreaWithBound(const ConicPiece& piece, int level, double* area, double* bound) {
  auto triangle =
      std::fabs(0.5 * (piece.x0 * (piece.y1 - piece.y2) + piece.x1 * (piece.y2 - piece.y0) +
                       piece.x2 * (piece.y0 - piece.y1)));
  if (level <= 0) {
    *area += 0.5 * (piece.x0 * piece.y2 - piece.x2 * piece.y0);
    *bound += triangle;
    return;
  }
  ConicPiece left = {};
  ConicPiece right = {};
  ChopConic(piece, &left, &right);
  ConicAreaWithBound(left, level - 1, area, bound);
  ConicAreaWithBound(right, level - 1, area, bound);
}

// Highest subdivision level tried before the direction is given up as undecidable. Every level
// doubles the piece count and quarters the bound, so the last one already costs 4096 pieces per
// conic; a contour still undecided there is degenerate rather than merely thin.
constexpr int MaxConicLevel = 12;

// Sums every conic of one contour at the given subdivision level.
void ConicChainArea(const std::vector<Path::Segment>& conics, const Point& origin, int level,
                    double* area, double* bound) {
  auto originX = static_cast<double>(origin.x);
  auto originY = static_cast<double>(origin.y);
  for (const auto& segment : conics) {
    ConicPiece piece = {};
    piece.x0 = static_cast<double>(segment.points[0].x) - originX;
    piece.y0 = static_cast<double>(segment.points[0].y) - originY;
    piece.x1 = static_cast<double>(segment.points[1].x) - originX;
    piece.y1 = static_cast<double>(segment.points[1].y) - originY;
    piece.x2 = static_cast<double>(segment.points[2].x) - originX;
    piece.y2 = static_cast<double>(segment.points[2].y) - originY;
    piece.weight = static_cast<double>(segment.conicWeight);
    ConicAreaWithBound(piece, level, area, bound);
  }
}

bool DecomposeContours(const Path& path, std::vector<ContourInfo>& contours) {
  ContourInfo* current = nullptr;
  auto finish = [](ContourInfo& contour) -> bool {
    if (contour.conics.empty()) {
      contour.signedArea = contour.exactArea;
    } else {
      // Refine the chord sum until the bound on its error drops below the area itself. Two
      // samplings agreeing on a sign proved nothing: both are chord sums of the same arc and both
      // miss it on the same side, so a thin outline can push them past zero together. A bound does
      // decide it - no error inside the bound can reach zero - and when refinement runs out, the
      // direction is reported as undecidable rather than guessed.
      bool decidable = false;
      for (int level = 0; level <= MaxConicLevel; level++) {
        double conicArea = 0.0;
        double conicBound = 0.0;
        ConicChainArea(contour.conics, contour.origin, level, &conicArea, &conicBound);
        auto total = contour.exactArea + conicArea;
        if (std::fabs(total) > conicBound) {
          contour.signedArea = total;
          decidable = true;
          break;
        }
      }
      if (!decidable) {
        return false;
      }
    }
    if (!contour.closed) {
      contour.path.close();
    }
    if (contour.probes.size() > MAX_PROBE_COUNT) {
      // Keep an even spread instead of the first few, so the candidates still speak for the whole
      // contour rather than for one corner of it.
      std::vector<Point> thinned = {};
      thinned.reserve(MAX_PROBE_COUNT);
      auto stride =
          static_cast<double>(contour.probes.size()) / static_cast<double>(MAX_PROBE_COUNT);
      for (size_t i = 0; i < MAX_PROBE_COUNT; ++i) {
        thinned.push_back(contour.probes[static_cast<size_t>(i * stride)]);
      }
      contour.probes = std::move(thinned);
    }
    return true;
  };
  for (const auto& segment : path) {
    if (segment.verb == PathVerb::Move) {
      if (current != nullptr && !finish(*current)) {
        return false;
      }
      contours.emplace_back();
      current = &contours.back();
      current->segments.push_back(segment);
      current->path.moveTo(segment.points[0]);
      current->path.setFillType(PathFillType::Winding);
      current->origin = segment.points[0];
      current->tightLeft = current->fatLeft = current->tightRight = current->fatRight =
          segment.points[0].x;
      current->tightTop = current->fatTop = current->tightBottom = current->fatBottom =
          segment.points[0].y;
      continue;
    }
    if (current == nullptr) {
      return false;
    }
    if (segment.verb == PathVerb::Close) {
      current->closed = true;
      current->segments.push_back(segment);
      current->path.close();
      continue;
    }
    if (segment.verb != PathVerb::Line && segment.verb != PathVerb::Quad &&
        segment.verb != PathVerb::Conic && segment.verb != PathVerb::Cubic) {
      return false;
    }
    current->segments.push_back(segment);
    if (segment.verb == PathVerb::Conic) {
      if (!std::isfinite(segment.conicWeight) || segment.conicWeight <= 0.0f) {
        // The error bound rests on the arc staying inside its control triangle, which only holds
        // for a positive weight. Without it there is no way to tell which way the contour turns.
        return false;
      }
      current->conics.push_back(segment);
    } else {
      current->exactArea += ExactSegmentArea(segment, current->origin);
    }
    switch (segment.verb) {
      case PathVerb::Line:
        current->path.lineTo(segment.points[1]);
        break;
      case PathVerb::Quad:
        current->path.quadTo(segment.points[1], segment.points[2]);
        break;
      case PathVerb::Conic:
        current->path.conicTo(segment.points[1], segment.points[2], segment.conicWeight);
        break;
      default:
        current->path.cubicTo(segment.points[1], segment.points[2], segment.points[3]);
        break;
    }
    auto pointCount = SegmentPointCount(segment.verb);
    for (int i = 0; i < pointCount; ++i) {
      const auto& point = segment.points[i];
      if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
        // Neither an area nor a containment test means anything here, and guessing a direction
        // would silently hand back a path that claims a rule it does not follow.
        return false;
      }
      GrowBounds(point, current->fatLeft, current->fatTop, current->fatRight, current->fatBottom);
    }
    if (segment.verb != PathVerb::Line) {
      // Interior samples flatten the curve for the area and the tight bounds. They stop short of
      // t == 1, so the end point is added separately below.
      for (int i = 1; i < CURVE_SAMPLES; ++i) {
        auto t = static_cast<float>(i) / static_cast<float>(CURVE_SAMPLES);
        Point point = {};
        switch (segment.verb) {
          case PathVerb::Quad:
            point = EvalQuad(segment.points[0], segment.points[1], segment.points[2], t);
            break;
          case PathVerb::Conic:
            point = EvalConic(segment.points[0], segment.points[1], segment.points[2],
                              segment.conicWeight, t);
            break;
          default:
            point = EvalCubic(segment.points[0], segment.points[1], segment.points[2],
                              segment.points[3], t);
            break;
        }
        GrowBounds(point, current->tightLeft, current->tightTop, current->tightRight,
                   current->tightBottom);
      }
    }
    // A line is already flat, so sampling it would only repeat its end point over and over. Every
    // segment contributes its end point exactly once to the tight bounds.
    auto endPoint = SegmentEndPoint(segment);
    GrowBounds(endPoint, current->tightLeft, current->tightTop, current->tightRight,
               current->tightBottom);
    // One containment candidate per segment. They are thinned to an even spread once the contour
    // is complete; a single point cannot be trusted to speak for the whole contour.
    switch (segment.verb) {
      case PathVerb::Line:
        current->probes.push_back({(segment.points[0].x + segment.points[1].x) * 0.5f,
                                   (segment.points[0].y + segment.points[1].y) * 0.5f});
        break;
      case PathVerb::Quad:
        current->probes.push_back(
            EvalQuad(segment.points[0], segment.points[1], segment.points[2], 0.5f));
        break;
      case PathVerb::Conic:
        current->probes.push_back(EvalConic(segment.points[0], segment.points[1], segment.points[2],
                                            segment.conicWeight, 0.5f));
        break;
      default:
        current->probes.push_back(EvalCubic(segment.points[0], segment.points[1], segment.points[2],
                                            segment.points[3], 0.5f));
        break;
    }
  }
  if (current != nullptr && !finish(*current)) {
    return false;
  }
  return true;
}

// True when the outer contour is even potentially a container of the inner one. The fat bounds of
// the outer one enclose its whole area and the tight bounds of the inner one stay inside its area,
// so failing this test rules the pair out without running the containment test.
bool MightContain(const ContourInfo& outer, const ContourInfo& inner) {
  return outer.fatLeft <= inner.tightLeft && outer.fatTop <= inner.tightTop &&
         outer.fatRight >= inner.tightRight && outer.fatBottom >= inner.tightBottom;
}

// A closed contour ends with a Close verb, which carries no point, so the last curve decides where
// the reversed contour has to start.
const Path::Segment* LastCurveSegment(const std::vector<Path::Segment>& segments) {
  for (size_t i = segments.size(); i-- > 1;) {
    if (segments[i].verb != PathVerb::Close) {
      return &segments[i];
    }
  }
  return nullptr;
}

void WriteContour(Path& output, const ContourInfo& contour) {
  const auto& segments = contour.segments;
  if (segments.size() < 2) {
    output.moveTo(segments[0].points[0]);
    output.close();
    return;
  }
  if (!contour.reverse) {
    output.moveTo(segments[0].points[0]);
    for (size_t i = 1; i < segments.size(); ++i) {
      const auto& segment = segments[i];
      switch (segment.verb) {
        case PathVerb::Line:
          output.lineTo(segment.points[1]);
          break;
        case PathVerb::Quad:
          output.quadTo(segment.points[1], segment.points[2]);
          break;
        case PathVerb::Conic:
          output.conicTo(segment.points[1], segment.points[2], segment.conicWeight);
          break;
        case PathVerb::Cubic:
          output.cubicTo(segment.points[1], segment.points[2], segment.points[3]);
          break;
        default:
          break;
      }
    }
  } else {
    // Walking the loop backwards means starting at the last point of the last segment and emitting
    // every segment in reverse. A quadratic keeps its control point; a cubic swaps the two.
    const auto* last = LastCurveSegment(segments);
    if (last == nullptr) {
      output.moveTo(segments[0].points[0]);
      if (contour.closed) {
        output.close();
      }
      return;
    }
    auto start = SegmentEndPoint(*last);
    output.moveTo(start);
    auto lastIndex = static_cast<size_t>(last - segments.data());
    for (size_t i = lastIndex + 1; i-- > 1;) {
      const auto& segment = segments[i];
      switch (segment.verb) {
        case PathVerb::Line:
          output.lineTo(segment.points[0]);
          break;
        case PathVerb::Quad:
          output.quadTo(segment.points[1], segment.points[0]);
          break;
        case PathVerb::Conic:
          output.conicTo(segment.points[1], segment.points[0], segment.conicWeight);
          break;
        case PathVerb::Cubic:
          output.cubicTo(segment.points[2], segment.points[1], segment.points[0]);
          break;
        default:
          break;
      }
    }
  }
  if (contour.closed) {
    output.close();
  }
}

}  // namespace

bool Path::toWinding() {
  auto fillType = getFillType();
  if (fillType == PathFillType::Winding || fillType == PathFillType::InverseWinding) {
    return true;
  }
  auto targetFillType = fillType == PathFillType::InverseEvenOdd ? PathFillType::InverseWinding
                                                                 : PathFillType::Winding;
  // A single contour is filled identically by both rules: one contour is an odd count and a
  // non-zero sum alike. Only the rule has to change.
  if (isEmpty()) {
    setFillType(targetFillType);
    return true;
  }
  std::vector<ContourInfo> contours;
  if (!DecomposeContours(*this, contours)) {
    return false;
  }
  if (contours.size() < 2) {
    setFillType(targetFillType);
    return true;
  }
  // A contour counts as nested inside another only when every one of its candidate points lies
  // inside it. One point agreeing is not enough to speak for the whole contour: candidates that
  // disagree mean the two contours share area instead of containing each other, and flipping
  // either of them would change what the path fills.
  auto containsAllProbes = [](const ContourInfo& outer, const ContourInfo& inner) {
    if (inner.probes.empty()) {
      return false;
    }
    for (const auto& probe : inner.probes) {
      if (!outer.path.contains(probe.x, probe.y)) {
        return false;
      }
    }
    return true;
  };
  // Resolve each contour's nesting depth. A contour's parent is its innermost container.
  std::vector<int> depths(contours.size(), -1);
  std::vector<bool> visiting(contours.size(), false);
  auto cyclic = false;
  std::function<int(size_t)> resolveDepth = [&](size_t index) -> int {
    if (depths[index] >= 0) {
      return depths[index];
    }
    if (visiting[index]) {
      // Two contours cannot contain each other. Reaching one that is already being resolved means
      // the containment test contradicted itself, and every depth below would be guesswork.
      cyclic = true;
      return 0;
    }
    visiting[index] = true;
    int parent = -1;
    // Doubles all the way: a container's area may exceed the float range, and starting from the
    // largest float would silently disqualify exactly those outlines.
    auto parentArea = std::numeric_limits<double>::max();
    for (size_t i = 0; i < contours.size(); ++i) {
      if (i == index) {
        continue;
      }
      if (!MightContain(contours[i], contours[index])) {
        continue;
      }
      if (!containsAllProbes(contours[i], contours[index])) {
        continue;
      }
      auto area = BoundsArea(contours[i].tightLeft, contours[i].tightTop, contours[i].tightRight,
                             contours[i].tightBottom);
      if (area < parentArea) {
        parentArea = area;
        parent = static_cast<int>(i);
      }
    }
    auto depth = parent < 0 ? 0 : resolveDepth(static_cast<size_t>(parent)) + 1;
    visiting[index] = false;
    depths[index] = depth;
    return depth;
  };
  for (size_t i = 0; i < contours.size(); ++i) {
    contours[i].depth = resolveDepth(i);
  }
  if (cyclic) {
    // Report the failure instead of returning a path that claims a rule its geometry does not
    // follow. The caller keeps its own outline, which is still the better of the two outcomes.
    return false;
  }
  // Contours that share area without either containing the other - overlapping strokes - are
  // outside the nesting this conversion reasons about. They are left exactly as they are: both
  // stay at the outermost depth and keep their own direction, which is how a set of overlapping
  // strokes is meant to read under winding. Flipping either one by a guessed depth would change
  // the filled area, so the conversion silently does less here rather than inventing an answer.
  // Callers that need such an outline made winding-correct have to resolve the overlaps first:
  // addPath() with a boolean operation does that and orients the result correctly.
  // Keep the direction the outermost contours already use, so an outline that is already
  // consistent is left alone.
  int base = 0;
  // Doubles, matching BoundsArea: the largest outline may have an area beyond the float range.
  double baseArea = -1.0;
  for (const auto& contour : contours) {
    if (contour.depth != 0 || contour.signedArea == 0.0f) {
      continue;
    }
    auto area =
        BoundsArea(contour.tightLeft, contour.tightTop, contour.tightRight, contour.tightBottom);
    if (area > baseArea) {
      baseArea = area;
      base = contour.signedArea > 0.0f ? 1 : -1;
    }
  }
  if (base == 0) {
    // Every contour has no area, so the path fills nothing and the rule cannot matter.
    setFillType(targetFillType);
    return true;
  }
  Path result = {};
  result.setFillType(targetFillType);
  for (auto& contour : contours) {
    if (contour.signedArea == 0.0f) {
      contour.reverse = false;
    } else {
      auto expected = contour.depth % 2 == 0 ? base : -base;
      contour.reverse = (contour.signedArea > 0.0f ? 1 : -1) != expected;
    }
    WriteContour(result, contour);
  }
  *this = result;
  return true;
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

}  // namespace tgfx
