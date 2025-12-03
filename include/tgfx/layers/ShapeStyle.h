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

#include "tgfx/core/Shader.h"
#include "tgfx/layers/LayerProperty.h"

namespace tgfx {
/**
 * ShapeStyle specifies the source color(s) for what is being drawn in a shape layer. There are
 * three types of ShapeStyle: SolidColor, Gradient, and ImagePattern. Note: All ShapeStyle objects
 * are not thread-safe and should only be accessed from a single thread.
 */
class ShapeStyle : public LayerProperty {
 public:
  /**
   * Returns the alpha transparency value of the shape style. Valid values are 0 (fully transparent)
   * to 1 (fully opaque). The default value is 1.
   */
  float alpha() const {
    return _alpha;
  }

  /**
   * Sets the alpha transparency of the shape style.
   */
  void setAlpha(float value);

  /**
   * Returns the blend mode used to composite the shape style with the content below it. The default
   * value is BlendMode::SrcOver.
   */
  BlendMode blendMode() const {
    return _blendMode;
  }

  /**
   * Sets the blend mode of the shape style.
   */
  void setBlendMode(BlendMode value);

  /**
   * Returns the transformation matrix applied to the ShapeStyle.
   */
  const Matrix& matrix() const {
    return _matrix;
  }

  /**
   * Sets the transformation matrix applied to the ShapeStyle.
   */
  void setMatrix(const Matrix& value);

  std::shared_ptr<Shader> getShader() const;

 protected:
  enum class Type { Gradient, ImagePattern, SolidColor };

  virtual Type getType() const = 0;

  /**
   * Returns the current shader that will be used to draw the shape.
   */
  virtual std::shared_ptr<Shader> onGetShader() const = 0;

 private:
  float _alpha = 1.0f;
  BlendMode _blendMode = BlendMode::SrcOver;
  Matrix _matrix = {};

  friend class Types;
  friend class ShapeLayer;
};
}  // namespace tgfx
