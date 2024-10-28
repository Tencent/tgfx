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

#define DistanceToControlPoint(angleStep) (4 * tanf((angleStep)*0.25f) / 3)

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
                                       float startAngle, float endAngle, int* numBeziers) {
  std::vector<Point> points = {};
  float start = startAngle;
  float end = std::min(endAngle, start + M_PI_2_F);
  float currentX, currentY;
  *numBeziers = 0;
  for (int i = 0; i < 4; i++) {
    auto angleStep = end - start;
    auto distance = DistanceToControlPoint(angleStep);
    currentX = centerX + cosf(start) * radiusX;
    currentY = centerY + sinf(start) * radiusY;
    points.push_back({currentX, currentY});
    auto u = cosf(start);
    auto v = sinf(start);
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
    if (end == endAngle) {
      break;
    }
    start = end;
    end = std::min(endAngle, start + M_PI_2_F);
  }
  return points;
}

void Path::addOval(const Rect& oval, bool reversed, unsigned startIndex) {
  writableRef()->path.addOval(ToSkRect(oval), ToSkDirection(reversed), startIndex);
}

void Path::addArc(const Rect& oval, float startAngle, float sweepAngle) {
  auto radiusX = oval.width() * 0.5f;
  auto radiusY = oval.height() * 0.5f;
  auto endAngle = startAngle + sweepAngle;
  auto reversed = sweepAngle < 0;
  if (reversed) {
    std::swap(startAngle, endAngle);
  }
  int numBeziers = 0;
  auto points = GetArcPoints(oval.centerX(), oval.centerY(), radiusX, radiusY, startAngle, endAngle,
                             &numBeziers);
  PointIterator iter(points, reversed, 0);
  auto path = &(writableRef()->path);
  path->moveTo(iter.current());
  for (int i = 0; i < numBeziers; i++) {
    path->cubicTo(iter.next(), iter.next(), iter.next());
  }
  path->close();
}

// This converts the SVG arc to conics based on the SVG standard.
// Code source:
// 1. kdelibs/kdecore/svgicons Niko's code
// 2. webkit/chrome SVGPathNormalizer::decomposeArcToCubic()
// See also SVG implementation notes:
// http://www.w3.org/TR/SVG/implnote.html#ArcConversionEndpointToCenter
// Note that arcSweep bool value is flipped from the original implementation.
void Path::addArc(float rx, float ry, float xAxisRotate, ArcSize largeArc, PathDirection sweep,
                  Point endPt) {
  // path->injectMoveToIfNeeded();
  std::array<Point, 2> srcPts;
  this->getLastPt(&srcPts[0]);
  // If rx = 0 or ry = 0 then this arc is treated as a straight line segment (a "lineto")
  // joining the endpoints.
  // http://www.w3.org/TR/SVG/implnote.html#ArcOutOfRangeParameters
  if (FloatNearlyZero(rx) && FloatNearlyZero(ry)) {
    return this->lineTo(endPt);
  }
  // If the current point and target point for the arc are identical, it should be treated as a
  // zero length path. This ensures continuity in animations.
  srcPts[1] = endPt;
  if (srcPts[0] == srcPts[1]) {
    return this->lineTo(endPt);
  }
  rx = std::abs(rx);
  ry = std::abs(ry);
  Point midPointDistance = (srcPts[0] - srcPts[1]) * 0.5f;

  Matrix pointTransform;
  pointTransform.setRotate(-xAxisRotate);

  Point transformedMidPoint;
  pointTransform.mapPoints(&transformedMidPoint, &midPointDistance, 1);
  float squareRx = rx * rx;
  float squareRy = ry * ry;
  float squareX = transformedMidPoint.x * transformedMidPoint.x;
  float squareY = transformedMidPoint.y * transformedMidPoint.y;

  // Check if the radii are big enough to draw the arc, scale radii if not.
  // http://www.w3.org/TR/SVG/implnote.html#ArcCorrectionOutOfRangeRadii
  float radiiScale = squareX / squareRx + squareY / squareRy;
  if (radiiScale > 1) {
    radiiScale = std::sqrt(radiiScale);
    rx *= radiiScale;
    ry *= radiiScale;
  }

  pointTransform.setScale(1.0f / rx, 1.0f / ry);
  pointTransform.preRotate(-xAxisRotate);

  std::array<Point, 2> unitPts;
  pointTransform.mapPoints(unitPts.data(), srcPts.data(), unitPts.size());
  Point delta = unitPts[1] - unitPts[0];

  float d = delta.x * delta.x + delta.y * delta.y;
  float scaleFactorSquared = std::max(1 / d - 0.25f, 0.f);

  float scaleFactor = std::sqrt(scaleFactorSquared);
  if ((sweep == PathDirection::CCW) !=
      static_cast<bool>(largeArc)) {  // flipped from the original implementation
    scaleFactor = -scaleFactor;
  }
  delta *= scaleFactor;
  Point centerPoint = unitPts[0] + unitPts[1];
  centerPoint *= 0.5f;
  centerPoint.offset(-delta.y, delta.x);
  unitPts[0] -= centerPoint;
  unitPts[1] -= centerPoint;
  float theta1 = std::atan2(unitPts[0].y, unitPts[0].x);
  float theta2 = std::atan2(unitPts[1].y, unitPts[1].x);
  float thetaArc = theta2 - theta1;
  if (thetaArc < 0 &&
      (sweep == PathDirection::CW)) {  // arcSweep flipped from the original implementation
    thetaArc += M_PI_F * 2.0f;
  } else if (thetaArc > 0 &&
             (sweep != PathDirection::CW)) {  // arcSweep flipped from the original implementation
    thetaArc -= M_PI_F * 2.0f;
  }

  // Very tiny angles cause our subsequent math to go wonky
  // but a larger value is probably ok too.
  if (std::abs(thetaArc) < (M_PI_F / (1000 * 1000))) {
    return this->lineTo(endPt);
  }

  pointTransform.setRotate(xAxisRotate);
  pointTransform.preScale(rx, ry);

  // the arc may be slightly bigger than 1/4 circle, so allow up to 1/3rd
  float segments = std::ceil(std::abs(thetaArc / (2 * M_PI_F / 3)));
  float thetaWidth = thetaArc / segments;
  float t = std::tan(0.5f * thetaWidth);
  if (!FloatsAreFinite(&t, 1)) {
    return;
  }
  float startTheta = theta1;
  float w = std::sqrt(0.5f + std::cos(thetaWidth) * 0.5f);
  auto float_is_integer = [](float scalar) -> bool { return scalar == std::floor(scalar); };
  bool expectIntegers = FloatNearlyZero(M_PI_F * 0.5f - std::abs(thetaWidth)) &&
                        float_is_integer(rx) && float_is_integer(ry) && float_is_integer(endPt.x) &&
                        float_is_integer(endPt.y);

  auto path = &(writableRef()->path);
  for (int i = 0; i < static_cast<int>(segments); ++i) {
    float endTheta = startTheta + thetaWidth;
    float sinEndTheta = SkScalarSinSnapToZero(endTheta);
    float cosEndTheta = SkScalarCosSnapToZero(endTheta);

    unitPts[1].set(cosEndTheta, sinEndTheta);
    unitPts[1] += centerPoint;
    unitPts[0] = unitPts[1];
    unitPts[0].offset(t * sinEndTheta, -t * cosEndTheta);
    std::array<Point, 2> mapped;
    pointTransform.mapPoints(mapped.data(), unitPts.data(), unitPts.size());
    /*
        Computing the arc width introduces rounding errors that cause arcs to start
        outside their marks. A round rect may lose convexity as a result. If the input
        values are on integers, place the conic on integers as well.
         */
    if (expectIntegers) {
      for (Point& point : mapped) {
        point.x = std::round(point.x);
        point.y = std::round(point.y);
      }
    }
    path->conicTo(mapped[0].x, mapped[0].y, mapped[1].x, mapped[1].y, w);
    startTheta = endTheta;
  }

  // The final point should match the input point (by definition); replace it to
  // ensure that rounding errors in the above math don't cause any problems.
  path->setLastPt(endPt.x, endPt.y);
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

bool Path::getLastPt(Point* lastPt) const {
  SkPoint skPoint;
  if (pathRef->path.getLastPt(&skPoint)) {
    lastPt->set(skPoint.fX, skPoint.fY);
    return true;
  }
  return false;
};

}  // namespace tgfx
