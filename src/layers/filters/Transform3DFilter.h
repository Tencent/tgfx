/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

/**
 * Transform3DFilter is a filter that applies a perspective transformation to the input layer.
 */
class Transform3DFilter final : public LayerFilter {
 public:
  /**
   * Creates a Transform3DFilter with the specified LayerPerspectiveInfo.
   */
  static std::shared_ptr<Transform3DFilter> Make(const Matrix3D& matrix, const Size& viewportSize);

  /**
   * Returns the 3D transformation matrix. This matrix maps the layer's bounding rectangle model
   * from world coordinates to clip coordinates. When mapping the layer's final coordinates to
   * window coordinates.
   */
  Matrix3D matrix() const {
    return _matrix;
  }

  void setMatrix(const Matrix3D& matrix);

  /**
   * Sets the anchor point for the layer's 3D transformation, as well as the center point observed
   * by the camera in the projection model. Valid value range is [0, 1], values outside this range
   * will be clamped.
   * Point (0, 0) represents the top-left corner of the layer, point (1, 1) represents the
   * bottom-right corner of the layer. Default value is (0.5, 0.5), which is the center point of the
   * layer.
   * Affine transformations such as rotation implied in the matrix3D are performed based on this
   * anchor point. The X and Y coordinates of the camera's position in 3D space during layer
   * projection are also determined by this origin point.
   */
  void setOrigin(const Point& origin);

  /**
   * View port size, used to map NDC coordinates to window coordinates.
   */
  Size viewportSize() const {
    return _viewportSize;
  }

  void setViewportSize(const Size& size);

 private:
  explicit Transform3DFilter(const Matrix3D& matrix, const Size& viewportSize);

  Type type() const override {
    return Type::Transform3DFilter;
  }

  std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) override;

  Matrix3D _matrix = Matrix3D::I();

  Point _origin = {0.5, 0.5};

  Size _viewportSize = {0, 0};
};

}  // namespace tgfx
