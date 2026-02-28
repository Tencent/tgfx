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

#pragma once

#include <memory>
#include <vector>
#include "gpu/Quad.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix3D.h"

namespace tgfx {

/**
 * DrawPolygon3D represents a splittable 3D polygon for BSP tree processing.
 * It stores transformed 3D vertices in screen space and supports splitting by other polygons.
 */
class DrawPolygon3D {
 public:
  /**
   * Constructs a polygon from an image's 2D bounds and a 3D transformation matrix.
   * The transform is applied immediately to convert vertices to screen space.
   * @param depth The depth level in the layer tree (used for sorting coplanar polygons).
   * @param sequenceIndex The sequence index within the same depth level.
   */
  DrawPolygon3D(std::shared_ptr<Image> image, const Matrix3D& matrix, int depth, int sequenceIndex,
                float alpha, bool antiAlias);

  /**
   * Splits the given polygon by this polygon's plane.
   * For coplanar polygons, larger sequenceIndex goes to front (drawn later, appears on top).
   * @param polygon The polygon to split (ownership will be transferred).
   * @param front Output: portion in front of this plane, entire polygon if not split, or nullptr.
   * @param back Output: portion behind this plane, entire polygon if not split, or nullptr.
   * @param isCoplanar Output: true if polygon is coplanar with this plane.
   */
  void splitAnother(std::unique_ptr<DrawPolygon3D> polygon, std::unique_ptr<DrawPolygon3D>* front,
                    std::unique_ptr<DrawPolygon3D>* back, bool* isCoplanar) const;

  /**
   * Returns the signed distance from a point to this polygon's plane.
   * Positive means in front (same side as normal), negative means behind.
   */
  float signedDistanceTo(const Vec3& point) const;

  const std::vector<Vec3>& points() const {
    return _points;
  }

  bool isSplit() const {
    return _isSplit;
  }

  float alpha() const {
    return _alpha;
  }

  const std::shared_ptr<Image>& image() const {
    return _image;
  }

  const Matrix3D& matrix() const {
    return _matrix;
  }

  bool antiAlias() const {
    return _antiAlias;
  }

  bool isFacingPositiveZ() const;

  int sequenceIndex() const {
    return _sequenceIndex;
  }

  int depth() const {
    return _depth;
  }

  /**
   * Converts this polygon to a list of quads for rendering.
   * Each quad contains 4 vertices in local space (Z-order: LT, LB, RT, RB).
   * For triangles, the last two vertices are the same (RT == RB).
   * @return A list of quads in Z-order vertex layout.
   */
  std::vector<Quad> toQuads() const;

 private:
  // Constructs a polygon from already-transformed 3D points (used for split polygons).
  DrawPolygon3D(std::shared_ptr<Image> image, const Matrix3D& matrix, std::vector<Vec3> points,
                const Vec3& normal, int depth, int sequenceIndex, float alpha, bool antiAlias);

  void constructNormal();

  std::vector<Vec3> _points = {};
  Vec3 _normal = {0.0f, 0.0f, 1.0f};
  int _depth = 0;
  int _sequenceIndex = 0;
  // Whether this polygon was split from another polygon.
  bool _isSplit = false;
  float _alpha = 1.0f;
  bool _antiAlias = true;
  std::shared_ptr<Image> _image = nullptr;
  Matrix3D _matrix = {};
};

/**
 * Comparator for sorting DrawPolygon3D by paint order.
 * Smaller depth (parent layers) should be drawn first (at bottom).
 * Within the same depth, smaller sequenceIndex should be drawn first.
 */
static inline bool DrawPolygon3DOrder(const std::unique_ptr<DrawPolygon3D>& x,
                                      const std::unique_ptr<DrawPolygon3D>& y) {
  if (x->depth() != y->depth()) {
    return x->depth() < y->depth();
  }
  return x->sequenceIndex() < y->sequenceIndex();
}

}  // namespace tgfx
