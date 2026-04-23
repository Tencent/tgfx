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

#pragma once

#include <array>
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"

namespace tgfx {
/**
 * RRect describes a rounded rectangle defined by a bounding rect and per-corner radii.
 */
class RRect {
 public:
  /**
   * Returns an RRect with the same radii for all four corners. See setRectXY() for details.
   */
  static RRect MakeRectXY(const Rect& rect, float radiusX, float radiusY) {
    RRect rr = {};
    rr.setRectXY(rect, radiusX, radiusY);
    return rr;
  }

  /**
   * Returns an RRect with per-corner radii. See setRectRadii() for details.
   */
  static RRect MakeRectRadii(const Rect& rect, const std::array<Point, 4>& radii) {
    RRect rr = {};
    rr.setRectRadii(rect, radii);
    return rr;
  }

  /**
   * Returns an oval RRect inscribed in the given bounds. See setOval() for details.
   */
  static RRect MakeOval(const Rect& oval) {
    RRect rr = {};
    rr.setOval(oval);
    return rr;
  }

  enum class Type {
    // All four corner radii are zero (or rect is empty).
    Rect,
    // All four corners have the same radii, each equal to half the corresponding edge length.
    Oval,
    // All four corners have the same radii.
    Simple,
    // Each corner can have an arbitrary independent radius.
    Complex
  };

  /**
   * Returns the type of this rounded rectangle.
   */
  Type type() const {
    return _type;
  }

  /**
   * Sets to rounded rectangle with the same radii for all four corners.
   * @param rect  bounds of rounded rectangle
   * @param radiusX  x-axis radius of corners
   * @param radiusY  y-axis radius of corners
   */
  void setRectXY(const Rect& rect, float radiusX, float radiusY);

  /**
   * Sets to rounded rectangle with per-corner radii. Any radius component that is negative is
   * first clamped to zero; the remaining radii are then scaled down proportionally if adjacent
   * corners overlap along any edge.
   * @param rect  bounds of rounded rectangle
   * @param radii  per-corner radii in the order [top-left, top-right, bottom-right, bottom-left]
   */
  void setRectRadii(const Rect& rect, const std::array<Point, 4>& radii);

  /**
   * Sets bounds to oval, x-axis radii to half oval.width(), and all y-axis radii to half
   * oval.height().
   */
  void setOval(const Rect& oval);

  /**
   * Scales the round rectangle by scaleX and scaleY.
   */
  void scale(float scaleX, float scaleY);

  /**
   * Translates the round rectangle by (dx, dy) without changing the radii or type.
   */
  void offset(float dx, float dy);

  /**
   * Returns the bounding rectangle.
   */
  const Rect& rect() const {
    return _rect;
  }

  /**
   * Returns the four corner radii in the order [top-left, top-right, bottom-right, bottom-left].
   */
  const std::array<Point, 4>& radii() const {
    return _radii;
  }

 private:
  Rect _rect = {};
  std::array<Point, 4> _radii = {};
  Type _type = Type::Rect;
};
}  // namespace tgfx
