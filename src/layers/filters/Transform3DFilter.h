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
   * Creates a Transform3DFilter with the specified transformation matrix.
   * The transformation matrix transforms 3D model coordinates to destination coordinates for x and
   * y before perspective division. The z value is mapped to the [-1, 1] range before perspective
   * division; content outside this z range will be clipped.
   */
  static std::shared_ptr<Transform3DFilter> Make(const Matrix3D& matrix);

  /**
   * Returns the 3D transformation matrix. This matrix transforms 3D model coordinates to
   * destination coordinates for x and y before perspective division. The z value is mapped to the
   * [-1, 1] range before perspective division; content outside this z range will be clipped.
   */
  Matrix3D matrix() const {
    return _matrix;
  }

  void setMatrix(const Matrix3D& matrix);

  /**
   * Returns whether to hide the back face of the content after the 3D transformation. The default
   * value is false, which means both the front and back faces are drawn.
   * When the layer is first created, the front face is oriented toward the user by default. After
   * applying certain 3D transformations, such as rotating 180 degrees around the X axis, the back
   * face of the layer may face the user.
   */
  bool hideBackFace() const {
    return _hideBackFace;
  }

  /**
   * Sets whether to hide the back face of the content after the 3D transformation.
   */
  void setHideBackFace(bool hideBackFace);

 private:
  explicit Transform3DFilter(const Matrix3D& matrix);

  Type type() const override {
    return Type::Transform3DFilter;
  }

  std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) override;

  Matrix3D _matrix = Matrix3D::I();

  bool _hideBackFace = false;
};

}  // namespace tgfx
