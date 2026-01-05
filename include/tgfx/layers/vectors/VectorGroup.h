/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Point.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * VectorGroup is a container that holds multiple child vector elements with transform properties.
 * Styles and modifiers within this group only affect elements inside this group.
 */
class VectorGroup : public VectorElement {
 public:
  VectorGroup() = default;

  /**
   * Returns the list of child vector elements in this group.
   */
  const std::vector<std::shared_ptr<VectorElement>>& elements() const {
    return _elements;
  }

  /**
   * Sets the list of child vector elements in this group.
   */
  void setElements(std::vector<std::shared_ptr<VectorElement>> value);

  // ==================== Transform Properties ====================

  /**
   * Returns the anchor point for transformations, in local coordinates.
   */
  const Point& anchorPoint() const {
    return _anchorPoint;
  }

  /**
   * Sets the anchor point for transformations.
   */
  void setAnchorPoint(const Point& value);

  /**
   * Returns the position of this group relative to its parent.
   */
  const Point& position() const {
    return _position;
  }

  /**
   * Sets the position of this group relative to its parent.
   */
  void setPosition(const Point& value);

  /**
   * Returns the scale factor applied to this group. Default is {1.0, 1.0}.
   */
  const Point& scale() const {
    return _scale;
  }

  /**
   * Sets the scale factor applied to this group.
   */
  void setScale(const Point& value);

  /**
   * Returns the rotation angle in degrees.
   */
  float rotation() const {
    return _rotation;
  }

  /**
   * Sets the rotation angle in degrees.
   */
  void setRotation(float value);

  /**
   * Returns the alpha of this group. Valid values are 0.0 (fully transparent) to 1.0 (fully
   * opaque).
   */
  float alpha() const {
    return _alpha;
  }

  /**
   * Sets the alpha of this group.
   */
  void setAlpha(float value);

  /**
   * Returns the skew angle in degrees.
   */
  float skew() const {
    return _skew;
  }

  /**
   * Sets the skew angle in degrees.
   */
  void setSkew(float value);

  /**
   * Returns the skew axis angle in degrees.
   */
  float skewAxis() const {
    return _skewAxis;
  }

  /**
   * Sets the skew axis angle in degrees.
   */
  void setSkewAxis(float value);

  /**
   * Returns the transformation matrix for this group.
   */
  Matrix getMatrix();

 protected:
  Type type() const override {
    return Type::VectorGroup;
  }

  void attachToLayer(Layer* layer) override;

  void detachFromLayer(Layer* layer) override;

  void apply(VectorContext* context) override;

 private:
  std::vector<std::shared_ptr<VectorElement>> _elements = {};
  Point _anchorPoint = Point::Zero();
  Point _position = Point::Zero();
  Point _scale = {1.0f, 1.0f};
  float _rotation = 0.0f;
  float _alpha = 1.0f;
  float _skew = 0.0f;
  float _skewAxis = 0.0f;
  bool _matrixDirty = true;
  Matrix _cachedMatrix = Matrix::I();
};

}  // namespace tgfx
