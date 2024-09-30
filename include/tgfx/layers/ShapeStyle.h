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

#include "tgfx/core/Shader.h"

namespace tgfx {
/**
 * ShapeStyle specifies the source color(s) for what is being drawn in a shape layer. There are
 * three types of ShapeStyle: SolidColor, Gradient, and ImagePattern.
 */
class ShapeStyle {
 public:
  virtual ~ShapeStyle() = default;

 protected:
  /**
   * Returns the current shader that will be used to draw the shape.
   */
  virtual std::shared_ptr<Shader> getShader() const = 0;

  friend class ShapeLayer;
};
}  // namespace tgfx
