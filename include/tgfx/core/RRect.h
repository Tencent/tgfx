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
#include "tgfx/core/Rect.h"

namespace tgfx {
/**
 * RRect describes a rounded rectangle defined by a bounding rect, per-corner radii, and a corner
 * smoothness shared by all four corners.
 */
class RRect {
 public:
  /**
   * Returns an RRect representing a plain rectangle with zero corner radii.
   */
  static RRect MakeRect(const Rect& rect) {
    RRect rr = {};
    rr.setRect(rect);
    return rr;
  }

  /**
   * Returns an RRect with the same radii for all four corners. See setRectXY() for details.
   */
  static RRect MakeRectXY(const Rect& rect, float radiusX, float radiusY, float smoothness = 0.0f) {
    RRect rr = {};
    rr.setRectXY(rect, radiusX, radiusY, smoothness);
    return rr;
  }

  /**
   * Returns an RRect with per-corner radii. See setRectRadii() for details.
   */
  static RRect MakeRectRadii(const Rect& rect, const std::array<Point, 4>& radii,
                             float smoothness = 0.0f) {
    RRect rr = {};
    rr.setRectRadii(rect, radii, smoothness);
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
   * Returns true if the bounds are a simple rectangle.
   */
  bool isRect() const {
    return _type == Type::Rect;
  }

  /**
   * Returns true if the bounds are an oval.
   */
  bool isOval() const {
    return _type == Type::Oval;
  }

  /**
   * Returns true if each corner may have an independent radius.
   */
  bool isComplex() const {
    return _type == Type::Complex;
  }

  /**
   * Sets to a plain rectangle with zero corner radii.
   * @param rect  bounds of the rectangle
   */
  void setRect(const Rect& rect);

  /**
   * Sets to rounded rectangle with the same radii for all four corners.
   * @param rect  bounds of rounded rectangle
   * @param radiusX  x-axis radius of corners
   * @param radiusY  y-axis radius of corners
   * @param smoothness  corner smoothness applied uniformly to all corners
   */
  void setRectXY(const Rect& rect, float radiusX, float radiusY, float smoothness = 0.0f);

  /**
   * Sets to rounded rectangle with per-corner radii. If either component of a corner radius is
   * negative, that entire corner is forced to (0, 0) (producing a sharp corner). When an edge is
   * too short to fit the smoothed corners at both of its ends, the smoothness is reduced first;
   * the radii are then scaled down proportionally only if the corners still overlap at zero
   * smoothness.
   * @param rect  bounds of rounded rectangle
   * @param radii  per-corner radii in the order [top-left, top-right, bottom-right, bottom-left]
   * @param smoothness  corner smoothness in [0, 1]. A value of 0 produces circular corner arcs;
   * larger values blend each arc into the adjacent edges over a longer span.
   */
  void setRectRadii(const Rect& rect, const std::array<Point, 4>& radii, float smoothness = 0.0f);

  /**
   * Sets bounds to oval, x-axis radii to half oval.width(), and all y-axis radii to half
   * oval.height().
   * @param oval  bounds of the oval
   */
  void setOval(const Rect& oval);

  /**
   * Scales the round rectangle by scaleX and scaleY.
   * @param scaleX  horizontal scale factor
   * @param scaleY  vertical scale factor
   */
  void scale(float scaleX, float scaleY);

  /**
   * Translates the round rectangle by (dx, dy) without changing the radii or type.
   * @param dx  horizontal offset
   * @param dy  vertical offset
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

  /**
   * Returns the corner smoothness in [0, 1], after reduction by edge competition. It is always 0
   * when the type is Rect or Oval.
   */
  float smoothness() const {
    return _smoothness;
  }

  /**
   * Compares a and b; returns true if they have the same bounding rect, corner radii, and corner
   * smoothness.
   */
  friend bool operator==(const RRect& a, const RRect& b) {
    return a._rect == b._rect && a._radii == b._radii && a._smoothness == b._smoothness;
  }

  /**
   * Compares a and b; returns true if they differ in bounding rect, any corner radius, or corner
   * smoothness.
   */
  friend bool operator!=(const RRect& a, const RRect& b) {
    return !(a == b);
  }

 private:
  Rect _rect = {};
  std::array<Point, 4> _radii = {};
  // The resolved smoothness after edge competition, not the authored input.
  float _smoothness = 0.0f;
  Type _type = Type::Rect;
};
}  // namespace tgfx
