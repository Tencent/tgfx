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

#include "core/Matrix3DUtils.h"
#include <algorithm>
#include <vector>
#include "utils/MathExtra.h"

namespace tgfx {

namespace {

// Homogeneous vertex (X, Y, W) used during Sutherland-Hodgman clipping. Values stay unhomogenized
// so that the clip against the near plane (W > 0) is well defined.
struct ClipVertex {
  float x;
  float y;
  float w;
};

// Signed distance to a clip plane is a*X + b*Y + c*W; positive means inside the kept half-space.
struct ClipPlane {
  float a;
  float b;
  float c;
};

}  // namespace

static float SignedDistance(const ClipPlane& plane, const ClipVertex& vertex) {
  return plane.a * vertex.x + plane.b * vertex.y + plane.c * vertex.w;
}

// Sutherland-Hodgman clip of `input` against `plane`, writing survivors to `output`. Boundary
// crossings interpolate linearly in homogeneous space so the near-plane clip stays well defined.
static void ClipPolygonAgainstPlane(const std::vector<ClipVertex>& input, const ClipPlane& plane,
                                    std::vector<ClipVertex>* output) {
  output->clear();
  const size_t count = input.size();
  if (count == 0) {
    return;
  }
  ClipVertex prev = input[count - 1];
  float distancePrev = SignedDistance(plane, prev);
  for (size_t i = 0; i < count; ++i) {
    const ClipVertex& current = input[i];
    const float distanceCurrent = SignedDistance(plane, current);
    const bool prevInside = distancePrev >= 0.0f;
    const bool currentInside = distanceCurrent >= 0.0f;
    if (prevInside != currentInside) {
      const float t = distancePrev / (distancePrev - distanceCurrent);
      output->push_back({prev.x + t * (current.x - prev.x), prev.y + t * (current.y - prev.y),
                         prev.w + t * (current.w - prev.w)});
    }
    if (currentInside) {
      output->push_back(current);
    }
    prev = current;
    distancePrev = distanceCurrent;
  }
}

bool Matrix3DUtils::IsRectBehindCamera(const Rect& rect, const Matrix3D& matrix) {
  return matrix.mapHomogeneous(rect.left, rect.top, 0, 1).w <= 0 ||
         matrix.mapHomogeneous(rect.left, rect.bottom, 0, 1).w <= 0 ||
         matrix.mapHomogeneous(rect.right, rect.top, 0, 1).w <= 0 ||
         matrix.mapHomogeneous(rect.right, rect.bottom, 0, 1).w <= 0;
}

Matrix3D Matrix3DUtils::OriginAdaptedMatrix3D(const Matrix3D& matrix3D, const Point& newOrigin) {
  auto offsetMatrix = Matrix3D::MakeTranslate(newOrigin.x, newOrigin.y, 0);
  auto invOffsetMatrix = Matrix3D::MakeTranslate(-newOrigin.x, -newOrigin.y, 0);
  return invOffsetMatrix * matrix3D * offsetMatrix;
}

bool Matrix3DUtils::IsMatrix3DAffine(const Matrix3D& matrix) {
  return FloatNearlyZero(matrix.getRowColumn(0, 2)) && FloatNearlyZero(matrix.getRowColumn(1, 2)) &&
         matrix.getRow(2) == Vec4(0, 0, 1, 0) && matrix.getRow(3) == Vec4(0, 0, 0, 1);
}

Matrix Matrix3DUtils::GetMayLossyAffineMatrix(const Matrix3D& matrix) {
  return Matrix::MakeAll(matrix.getRowColumn(0, 0), matrix.getRowColumn(0, 1),
                         matrix.getRowColumn(0, 3), matrix.getRowColumn(1, 0),
                         matrix.getRowColumn(1, 1), matrix.getRowColumn(1, 3));
}

Rect Matrix3DUtils::InverseMapRect(const Rect& rect, const Matrix3D& matrix) {
  auto matrix2D = matrix.asMatrix();
  Matrix inversedMatrix;
  if (!matrix2D.invert(&inversedMatrix)) {
    return Rect::MakeEmpty();
  }
  return inversedMatrix.mapRect(rect);
}

bool Matrix3DUtils::ComputeVisibleFootprints(const Rect& localBounds, const Rect& destRect,
                                             const Matrix3D& matrix, Rect* localFootprint,
                                             Rect* destFootprint) {
  // 3x3 homography that maps local (x, y) on the z=0 plane to destination (X, Y, W). Rows and
  // columns 0, 1, 3 of the 4x4 matrix carry the projective part; the z axis is dropped since the
  // leaf lies on z=0.
  const auto homography = Matrix::MakeAll(
      matrix.getRowColumn(0, 0), matrix.getRowColumn(0, 1), matrix.getRowColumn(0, 3),
      matrix.getRowColumn(1, 0), matrix.getRowColumn(1, 1), matrix.getRowColumn(1, 3),
      matrix.getRowColumn(3, 0), matrix.getRowColumn(3, 1), matrix.getRowColumn(3, 3));
  Matrix inverseHomography;
  if (!homography.invert(&inverseHomography)) {
    return false;
  }

  // Do not divide by W yet; that would collapse near-plane crossings and break the clip below.
  std::vector<ClipVertex> polygon;
  polygon.reserve(8);
  const float localCorners[4][2] = {{localBounds.left, localBounds.top},
                                    {localBounds.right, localBounds.top},
                                    {localBounds.right, localBounds.bottom},
                                    {localBounds.left, localBounds.bottom}};
  for (const auto& corner : localCorners) {
    const auto projected = homography.mapHomogeneous(corner[0], corner[1], 1.0f);
    polygon.push_back({projected.x, projected.y, projected.z});
  }

  const ClipPlane planes[5] = {
      {0.0f, 0.0f, 1.0f},              // W > 0 near plane
      {1.0f, 0.0f, -destRect.left},    // X - left·W >= 0
      {-1.0f, 0.0f, destRect.right},   // right·W - X >= 0
      {0.0f, 1.0f, -destRect.top},     // Y - top·W >= 0
      {0.0f, -1.0f, destRect.bottom},  // bottom·W - Y >= 0
  };
  std::vector<ClipVertex> scratch;
  scratch.reserve(8);
  for (const auto& plane : planes) {
    ClipPolygonAgainstPlane(polygon, plane, &scratch);
    polygon.swap(scratch);
    if (polygon.empty()) {
      return false;
    }
  }

  float localMinX = 0.0f;
  float localMinY = 0.0f;
  float localMaxX = 0.0f;
  float localMaxY = 0.0f;
  float destMinX = 0.0f;
  float destMinY = 0.0f;
  float destMaxX = 0.0f;
  float destMaxY = 0.0f;
  bool initialized = false;
  for (const auto& vertex : polygon) {
    if (!(vertex.w > 0.0f)) {
      continue;
    }
    const float destX = vertex.x / vertex.w;
    const float destY = vertex.y / vertex.w;
    const auto local = inverseHomography.mapHomogeneous(vertex.x, vertex.y, vertex.w);
    if (!(local.z > 0.0f)) {
      continue;
    }
    const float localX = local.x / local.z;
    const float localY = local.y / local.z;
    if (!initialized) {
      localMinX = localMaxX = localX;
      localMinY = localMaxY = localY;
      destMinX = destMaxX = destX;
      destMinY = destMaxY = destY;
      initialized = true;
    } else {
      localMinX = std::min(localMinX, localX);
      localMaxX = std::max(localMaxX, localX);
      localMinY = std::min(localMinY, localY);
      localMaxY = std::max(localMaxY, localY);
      destMinX = std::min(destMinX, destX);
      destMaxX = std::max(destMaxX, destX);
      destMinY = std::min(destMinY, destY);
      destMaxY = std::max(destMaxY, destY);
    }
  }
  if (!initialized) {
    return false;
  }
  *localFootprint = Rect::MakeLTRB(localMinX, localMinY, localMaxX, localMaxY);
  *destFootprint = Rect::MakeLTRB(destMinX, destMinY, destMaxX, destMaxY);
  return true;
}

Matrix3D Matrix3DUtils::ScaleAdaptedMatrix3D(const Matrix3D& matrix, float scale) {
  if (FloatNearlyEqual(scale, 1.0f)) {
    return matrix;
  }
  auto invScale = 1.0f / scale;
  auto invScaleMatrix = Matrix3D::MakeScale(invScale, invScale, 1.0f);
  auto scaleMatrix = Matrix3D::MakeScale(scale, scale, 1.0f);
  return scaleMatrix * matrix * invScaleMatrix;
}

}  // namespace tgfx
