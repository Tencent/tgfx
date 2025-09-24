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
   * Window view size, used to map NDC coordinates to window coordinates.
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

  Size _viewportSize = {0, 0};
};

}  // namespace tgfx
