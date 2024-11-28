/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/Shader.h"

namespace tgfx {

enum class ShaderType {
  Color,
  ColorFilter,
  Image,
  Blend,
  Matrix,
  Gradient,
};

/**
 *  Linear:
 *      points[0] and points[1] represent the start and end points of the gradient.
 *  Radial:
 *      points[0] and radiuses[0] represent the center and radius of the gradient.
 *  Conic:
 *      points[0] represents the center, and radiuses[0] and radiuses[1] represent the start angle
 *      and end angle of the gradient.
 */
struct GradientInfo {
  std::vector<Color> colors;     // The colors in the gradient
  std::vector<float> positions;  // The positions of the colors in the gradient
  std::array<Point, 2> points;
  std::array<float, 2> radiuses;
};

class ShaderBase : public Shader {
 public:
  /**
   * Returns the type of this shader.
   */
  virtual ShaderType type() const = 0;

  /**
   * If this is a gradient shader, returns the valid type of gradient. If GradientInfo and Matrix
   * are not null pointers, also returns the corresponding values.
   */
  virtual GradientType asGradient(GradientInfo*) const {
    return GradientType::None;
  }

  /**
   * If this is an image shader, returns the image, and the tile modes for x and y.
   */
  virtual std::tuple<std::shared_ptr<Image>, TileMode, TileMode> asImage() const {
    return {nullptr, TileMode::Clamp, TileMode::Clamp};
  }
};

inline const ShaderBase* asShaderBase(const std::shared_ptr<Shader>& shader) {
  return static_cast<ShaderBase*>(shader.get());
}

}  // namespace tgfx