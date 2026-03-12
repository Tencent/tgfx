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

#include "DrawPolygon3D.h"
#include <algorithm>
#include <cmath>
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"

namespace tgfx {

// Distance tolerance for determining which side of a plane a point lies on.
static constexpr float SplitThreshold = 0.05f;

static Vec3 InterpolatePoint(const Vec3& from, const Vec3& to, float delta) {
  return Vec3(from.x + (to.x - from.x) * delta, from.y + (to.y - from.y) * delta,
              from.z + (to.z - from.z) * delta);
}

static size_t NextIndex(size_t i, size_t count) {
  return (i + 1) % count;
}

static size_t PrevIndex(size_t i, size_t count) {
  return (i + count - 1) % count;
}

static Point ProjectPoint(const Matrix3D& matrix, const Vec3& point) {
  auto result = matrix.mapPoint(point);
  return Point::Make(result.x, result.y);
}

static void CollectSplitPoints(const std::vector<Vec3>& points, const Vec3& startIntersection,
                               const Vec3& endIntersection, size_t beginIndex, size_t endIndex,
                               std::vector<Vec3>* result) {
  result->push_back(startIntersection);
  size_t numPoints = points.size();
  for (size_t index = beginIndex; index != endIndex; index = NextIndex(index, numPoints)) {
    result->push_back(points[index]);
  }
  if (result->back() != endIntersection) {
    result->push_back(endIntersection);
  }
}

DrawPolygon3D::DrawPolygon3D(std::shared_ptr<Image> image, const Matrix3D& matrix, int depth,
                             int sequenceIndex, float alpha, bool antiAlias)
    : _depth(depth), _sequenceIndex(sequenceIndex), _alpha(alpha), _antiAlias(antiAlias),
      _image(std::move(image)), _matrix(matrix) {
  auto srcW = static_cast<float>(_image->width());
  auto srcH = static_cast<float>(_image->height());

  Vec3 corners[4] = {
      Vec3(0.0f, 0.0f, 0.0f),
      Vec3(srcW, 0.0f, 0.0f),
      Vec3(srcW, srcH, 0.0f),
      Vec3(0.0f, srcH, 0.0f),
  };

  // Caller guarantees that vertices after transformation do not intersect the observer's z-plane.
  _points.reserve(4);
  for (const auto& corner : corners) {
    _points.push_back(matrix.mapPoint(corner));
  }

  constructNormal();
}

DrawPolygon3D::DrawPolygon3D(std::shared_ptr<Image> image, const Matrix3D& matrix,
                             std::vector<Vec3> points, const Vec3& normal, int depth,
                             int sequenceIndex, float alpha, bool antiAlias)
    : _points(std::move(points)), _normal(normal), _depth(depth), _sequenceIndex(sequenceIndex),
      _isSplit(true), _alpha(alpha), _antiAlias(antiAlias), _image(std::move(image)),
      _matrix(matrix) {
}

// Computes the normal by averaging cross products of opposite vertex pairs from the first vertex.
// Works correctly for convex polygons with 3+ vertices.
void DrawPolygon3D::constructNormal() {
  Vec3 newNormal(0.0f, 0.0f, 0.0f);
  auto delta = _points.size() / 2;
  for (size_t i = 1; i + delta < _points.size(); i++) {
    auto v1 = _points[i] - _points[0];
    auto v2 = _points[i + delta] - _points[0];
    newNormal += Vec3::Cross(v1, v2);
  }

  float length = newNormal.length();
  if (!FloatNearlyZero(length) && !FloatNearlyEqual(length, 1.0f)) {
    newNormal = newNormal * (1.0f / length);
  }
  _normal = newNormal;
}

float DrawPolygon3D::signedDistanceTo(const Vec3& point) const {
  return Vec3::Dot(point - _points[0], _normal);
}

void DrawPolygon3D::splitAnother(std::unique_ptr<DrawPolygon3D> polygon,
                                 std::unique_ptr<DrawPolygon3D>* front,
                                 std::unique_ptr<DrawPolygon3D>* back, bool* isCoplanar) const {
  // Tolerance for checking if the normal vector has unit length.
  DEBUG_ASSERT(std::abs(_normal.lengthSquared() - 1.0f) <= 0.001f);

  const size_t numPoints = polygon->_points.size();
  std::vector<float> vertexDistance(numPoints);
  size_t posCount = 0;
  size_t negCount = 0;

  for (size_t i = 0; i < numPoints; i++) {
    vertexDistance[i] = signedDistanceTo(polygon->_points[i]);
    if (vertexDistance[i] < -SplitThreshold) {
      ++negCount;
    } else if (vertexDistance[i] > SplitThreshold) {
      ++posCount;
    } else {
      vertexDistance[i] = 0.0f;
    }
  }

  // The polygon is coplanar with this polygon.
  if (posCount == 0 && negCount == 0) {
    *isCoplanar = true;
    // Compare by (depth, sequenceIndex) to determine paint order.
    // Larger values should be drawn later (on top), so they go to front.
    bool polygonIsLater = (polygon->_depth > _depth) ||
                          (polygon->_depth == _depth && polygon->_sequenceIndex >= _sequenceIndex);
    if (polygonIsLater) {
      *front = std::move(polygon);
    } else {
      *back = std::move(polygon);
    }
    return;
  }

  // The polygon is entirely on one side of this polygon.
  *isCoplanar = false;
  if (negCount == 0) {
    *front = std::move(polygon);
    return;
  }
  if (posCount == 0) {
    *back = std::move(polygon);
    return;
  }

  // The polygon intersects with this polygon.
  size_t frontBegin = 0;
  size_t backBegin = 0;
  size_t preFrontBegin = 0;
  size_t preBackBegin = 0;
  for (size_t i = 0; i < numPoints; i++) {
    if (vertexDistance[i] > 0.0f) {
      frontBegin = i;
      break;
    }
  }
  while (vertexDistance[preFrontBegin = PrevIndex(frontBegin, numPoints)] > 0.0f) {
    frontBegin = preFrontBegin;
  }
  for (size_t i = 0; i < numPoints; i++) {
    if (vertexDistance[i] < 0.0f) {
      backBegin = i;
      break;
    }
  }
  while (vertexDistance[preBackBegin = PrevIndex(backBegin, numPoints)] < 0.0f) {
    backBegin = preBackBegin;
  }

  // First vertex of the front fragment (same side as normal), on the intersection line.
  Vec3 prePosIntersection = InterpolatePoint(
      polygon->_points[preFrontBegin], polygon->_points[frontBegin],
      vertexDistance[preFrontBegin] / (vertexDistance[preFrontBegin] - vertexDistance[frontBegin]));
  // First vertex of the back fragment (opposite side of normal), on the intersection line.
  Vec3 preNegIntersection = InterpolatePoint(
      polygon->_points[preBackBegin], polygon->_points[backBegin],
      vertexDistance[preBackBegin] / (vertexDistance[preBackBegin] - vertexDistance[backBegin]));

  std::vector<Vec3> frontPoints;
  CollectSplitPoints(polygon->_points, prePosIntersection, preNegIntersection, frontBegin,
                     backBegin, &frontPoints);
  std::vector<Vec3> backPoints;
  CollectSplitPoints(polygon->_points, preNegIntersection, prePosIntersection, backBegin,
                     frontBegin, &backPoints);

  *front = std::unique_ptr<DrawPolygon3D>(new DrawPolygon3D(
      polygon->_image, polygon->_matrix, std::move(frontPoints), polygon->_normal, polygon->_depth,
      polygon->_sequenceIndex, polygon->_alpha, polygon->_antiAlias));
  *back = std::unique_ptr<DrawPolygon3D>(new DrawPolygon3D(
      polygon->_image, polygon->_matrix, std::move(backPoints), polygon->_normal, polygon->_depth,
      polygon->_sequenceIndex, polygon->_alpha, polygon->_antiAlias));

  DEBUG_ASSERT((*front)->_points.size() >= 3);
  DEBUG_ASSERT((*back)->_points.size() >= 3);
}

bool DrawPolygon3D::isFacingPositiveZ() const {
  return _normal.z > 0.0f;
}

std::vector<Quad> DrawPolygon3D::toQuads() const {
  std::vector<Quad> quads;
  size_t n = _points.size();
  if (n < 3) {
    DEBUG_ASSERT(false);
    return quads;
  }
  Matrix3D inverseMatrix;
  if (!_matrix.invert(&inverseMatrix)) {
    DEBUG_ASSERT(false);
    return quads;
  }

  // Project all 3D points to 2D local space
  std::vector<Point> localPoints;
  localPoints.reserve(n);
  for (const auto& point : _points) {
    localPoints.push_back(ProjectPoint(inverseMatrix, point));
  }

  if (n == 3) {
    // Triangle: degenerate to quad (duplicate last vertex)
    quads.push_back(
        Quad::MakeFromCW(localPoints[0], localPoints[1], localPoints[2], localPoints[2]));
    return quads;
  }
  if (n == 4) {
    quads.push_back(
        Quad::MakeFromCW(localPoints[0], localPoints[1], localPoints[2], localPoints[3]));
    return quads;
  }
  // n > 4: Fan decomposition into quads, each quad covers two triangles when possible.
  for (size_t i = 1; i + 2 < n; i += 2) {
    quads.push_back(
        Quad::MakeFromCW(localPoints[0], localPoints[i], localPoints[i + 1], localPoints[i + 2]));
  }
  // Handle remaining triangle if odd number of triangles.
  if ((n - 2) % 2 == 1) {
    quads.push_back(Quad::MakeFromCW(localPoints[0], localPoints[n - 2], localPoints[n - 1],
                                     localPoints[n - 1]));
  }
  return quads;
}

}  // namespace tgfx
