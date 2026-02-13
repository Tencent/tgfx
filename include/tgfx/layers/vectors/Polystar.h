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

#include "tgfx/core/Point.h"
#include "tgfx/core/Shape.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * Defines whether a Polystar is a polygon or a star.
 */
enum class PolystarType {
  /**
   * A star shape with alternating inner and outer vertices.
   */
  Star,
  /**
   * A regular polygon with equal sides.
   */
  Polygon
};

/**
 * Polystar represents a polygon or star shape.
 */
class Polystar : public VectorElement {
 public:
  /**
   * Creates a new Polystar instance.
   */
  static std::shared_ptr<Polystar> Make();

  /**
   * Returns the center point of the polystar.
   */
  const Point& center() const {
    return _center;
  }

  /**
   * Sets the center point of the polystar.
   */
  void setCenter(const Point& value);

  /**
   * Returns whether this is a star or a polygon.
   */
  PolystarType polystarType() const {
    return _polystarType;
  }

  /**
   * Sets whether this is a star or a polygon.
   */
  void setPolystarType(PolystarType value);

  /**
   * Returns the number of points (vertices) in the polygon or star.
   */
  float pointCount() const {
    return _pointCount;
  }

  /**
   * Sets the number of points (vertices) in the polygon or star.
   */
  void setPointCount(float value);

  /**
   * Returns the rotation of the shape in degrees.
   */
  float rotation() const {
    return _rotation;
  }

  /**
   * Sets the rotation of the shape in degrees.
   */
  void setRotation(float value);

  /**
   * Returns the outer radius of the polygon or star.
   */
  float outerRadius() const {
    return _outerRadius;
  }

  /**
   * Sets the outer radius of the polygon or star.
   */
  void setOuterRadius(float value);

  /**
   * Returns the roundness of the outer corners (0.0 to 1.0). A value of 0 means sharp corners.
   */
  float outerRoundness() const {
    return _outerRoundness;
  }

  /**
   * Sets the roundness of the outer corners.
   */
  void setOuterRoundness(float value);

  /**
   * Returns the inner radius of the star. Only used when polystarType is Star.
   */
  float innerRadius() const {
    return _innerRadius;
  }

  /**
   * Sets the inner radius of the star.
   */
  void setInnerRadius(float value);

  /**
   * Returns the roundness of the inner corners (0.0 to 1.0). Only used when polystarType is Star.
   */
  float innerRoundness() const {
    return _innerRoundness;
  }

  /**
   * Sets the roundness of the inner corners.
   */
  void setInnerRoundness(float value);

  /**
   * Returns whether the path direction is reversed (counter-clockwise).
   */
  bool reversed() const {
    return _reversed;
  }

  /**
   * Sets whether the path direction is reversed.
   */
  void setReversed(bool value);

 protected:
  Type type() const override {
    return Type::Polystar;
  }

  void apply(VectorContext* context) override;

 protected:
  Polystar() = default;

 private:
  Point _center = Point::Zero();
  PolystarType _polystarType = PolystarType::Star;
  float _pointCount = 5.0f;
  float _rotation = 0.0f;
  float _outerRadius = 100.0f;
  float _outerRoundness = 0.0f;
  float _innerRadius = 50.0f;
  float _innerRoundness = 0.0f;
  bool _reversed = false;
  std::shared_ptr<Shape> _cachedShape = nullptr;
};

}  // namespace tgfx
