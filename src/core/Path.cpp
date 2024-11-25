/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/PathRef.h"
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
  std::vector<Point> points = {};

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

bool Path::isRect(Rect* rect) const {
  if (!rect) {
    return pathRef->path.isRect(nullptr);
  }
  SkRect skRect = {};
  if (!pathRef->path.isRect(&skRect)) {
    return false;
  }
  rect->setLTRB(skRect.fLeft, skRect.fTop, skRect.fRight, skRect.fBottom);
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
  if (!pathRef->path.isRRect(&skRRect) || !skRRect.isSimple()) {
    return false;
  }
  if (rRect) {
    const auto& rect = skRRect.rect();
    rRect->rect.setLTRB(rect.fLeft, rect.fTop, rect.fRight, rect.fBottom);
    auto radii = skRRect.getSimpleRadii();
    rRect->radii.set(radii.fX, radii.fY);
  }
  return true;
}

Rect Path::getBounds() const {
  return pathRef->getBounds();
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
  auto transformedMidPoint = Point::Zero();
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

  auto* path = &(writableRef()->path);
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
    path->cubicTo(iter.next(), iter.next(), iter.next());
  }
}

void Path::addRoundRect(const Rect& rect, float radiusX, float radiusY, bool reversed,
                        unsigned startIndex) {
  writableRef()->path.addRRect(SkRRect::MakeRectXY(ToSkRect(rect), radiusX, radiusY),
                               ToSkDirection(reversed), startIndex);
}

void Path::addRRect(const RRect& rRect, bool reversed, unsigned int startIndex) {
  auto skRRect = SkRRect::MakeRectXY(ToSkRect(rRect.rect), rRect.radii.x, rRect.radii.y);
  writableRef()->path.addRRect(skRRect, ToSkDirection(reversed), startIndex);
}

void Path::addPath(const Path& src, PathOp op) {
  auto& path = writableRef()->path;
  const auto& newPath = src.pathRef->path;
  if (op == PathOp::Append) {
    if (path.isEmpty()) {
      path = newPath;
    } else {
      path.addPath(newPath);
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
  Op(path, newPath, pathOp, &path);
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

void Path::reverse() {
  auto& path = writableRef()->path;
  SkPath tempPath;
  tempPath.reverseAddPath(path);
  path = tempPath;
}

void Path::decompose(const PathIterator& iterator, void* info) const {
  if (iterator == nullptr) {
    return;
  }
  const auto& skPath = pathRef->path;
  SkPath::Iter iter(skPath, false);
  SkPoint points[4];
  SkPoint quads[5];
  SkPath::Verb verb;
  while ((verb = iter.next(points)) != SkPath::kDone_Verb) {
    switch (verb) {
      case SkPath::kMove_Verb:
        iterator(PathVerb::Move, reinterpret_cast<Point*>(points), info);
        break;
      case SkPath::kLine_Verb:
        iterator(PathVerb::Line, reinterpret_cast<Point*>(points), info);
        break;
      case SkPath::kQuad_Verb:
        iterator(PathVerb::Quad, reinterpret_cast<Point*>(points), info);
        break;
      case SkPath::kConic_Verb:
        // approximate with 2^1=2 quads.
        SkPath::ConvertConicToQuads(points[0], points[1], points[2], iter.conicWeight(), quads, 1);
        iterator(PathVerb::Quad, reinterpret_cast<Point*>(quads), info);
        iterator(PathVerb::Quad, reinterpret_cast<Point*>(quads) + 2, info);
        break;
      case SkPath::kCubic_Verb:
        iterator(PathVerb::Cubic, reinterpret_cast<Point*>(points), info);
        break;
      case SkPath::kClose_Verb:
        iterator(PathVerb::Close, reinterpret_cast<Point*>(points), info);
        break;
      default:
        break;
    }
  }
}

PathRef* Path::writableRef() {
  if (pathRef.use_count() != 1) {
    pathRef = std::make_shared<PathRef>(pathRef->path);
  } else {
    pathRef->uniqueKey.reset();
    pathRef->resetBounds();
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

}  // namespace tgfx
