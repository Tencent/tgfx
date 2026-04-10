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
 * RRect describes a rounded rectangle with a bounding rect and per-corner radii. Each corner has
 * an independent (xRadius, yRadius) pair stored in the radii array in the order [TopLeft, TopRight,
 * BottomRight, BottomLeft].
 */
struct RRect {
  /**
   * Type describes the specialization of the RRect.
   */
  enum class Type {
    Rect,    // all corner radii are zero (or rect is empty)
    Oval,    // radii fill the rect
    Simple,  // all four corners have the same radii
    Complex  // arbitrary per-corner radii
  };

  Rect rect = {};
  std::array<Point, 4> radii = {};  // [TL, TR, BR, BL]
  Type type = Type::Rect;

  /**
   * Returns true if the bounds are a simple rectangle.
   */
  bool isRect() const;

  /**
   * Returns true if the bounds are an oval.
   */
  bool isOval() const;

  /**
   * Returns true if all four corners share the same radii.
   */
  bool isSimple() const;

  /**
   * Returns true if the four corners have arbitrary independent radii.
   */
  bool isComplex() const;

  /**
   * Sets to rounded rectangle with the same radii for all four corners.
   * @param rect  bounds of rounded rectangle
   * @param radiusX  x-axis radius of corners
   * @param radiusY  y-axis radius of corners
   */
  void setRectXY(const Rect& rect, float radiusX, float radiusY);

  /**
   * Sets to rounded rectangle with per-corner radii. The radii are scaled down proportionally if
   * adjacent corners overlap along any edge, following the W3C CSS specification.
   * @param rect  bounds of rounded rectangle
   * @param radii  per-corner radii in the order [TL, TR, BR, BL]
   */
  void setRectRadii(const Rect& rect, const std::array<Point, 4>& radii);

  /**
   * Sets bounds to oval, x-axis radii to half oval.width(), and all y-axis radii to half
   * oval.height().
   */
  void setOval(const Rect& oval);

  /**
   * Scale the round rectangle by scaleX and scaleY.
   */
  void scale(float scaleX, float scaleY);
};
}  // namespace tgfx
