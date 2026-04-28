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
#include "DrawableRect.h"
#include "gpu/Quad.h"

namespace tgfx {

/**
 * DrawPolygon3D represents a splittable 3D polygon for BSP tree processing.
 * It stores transformed 3D vertices in screen space and supports splitting by other polygons.
 */
class DrawPolygon3D {
 public:
  /**
   * Constructs a polygon from a DrawableRect's image bounds and its 3D transformation matrix.
   * The transform is applied immediately to convert vertices to screen space.
   * @param drawRect The draw data for this polygon. Must not be nullptr. Ownership is NOT
   * taken.
   */
  explicit DrawPolygon3D(DrawableRect* drawRect);

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

  DrawableRect* drawRect() const {
    return _drawRect;
  }

  const std::vector<Vec3>& points() const {
    return _points;
  }

  bool isSplit() const {
    return _isSplit;
  }

  bool isFacingPositiveZ() const;

  /**
   * Converts this polygon to a list of quads for rendering.
   * Each quad contains 4 vertices in local space (Z-order: LT, LB, RT, RB).
   * For triangles, the last two vertices are the same (RT == RB).
   * @return A list of quads in Z-order vertex layout.
   */
  std::vector<Quad> toQuads() const;

  /**
   * Creates a variant of this polygon that shares the same geometry but uses a different
   * DrawableRect.
   * @param drawRect The draw data for the variant. Must not be nullptr.
   */
  DrawPolygon3D makeVariant(DrawableRect* drawRect) const;

 private:
  // Constructs a polygon from already-transformed 3D points (used for split polygons).
  DrawPolygon3D(DrawableRect* drawRect, std::vector<Vec3> points, const Vec3& normal);

  void constructNormal();

  DrawableRect* _drawRect = nullptr;
  std::vector<Vec3> _points = {};
  Vec3 _normal = {0.0f, 0.0f, 1.0f};
  // Whether this polygon was split from another polygon.
  bool _isSplit = false;
};

/**
 * Comparator for sorting DrawPolygon3D by paint order.
 * Smaller order (parent layers) should be drawn first (at bottom).
 */
static inline bool DrawPolygon3DOrder(const std::unique_ptr<DrawPolygon3D>& x,
                                      const std::unique_ptr<DrawPolygon3D>& y) {
  return x->drawRect()->paintOrder < y->drawRect()->paintOrder;
}

}  // namespace tgfx
