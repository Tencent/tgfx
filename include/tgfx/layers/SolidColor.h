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
#include "tgfx/core/Color.h"
#include "tgfx/layers/ColorSource.h"

namespace tgfx {
/**
 * SolidColor represents a solid color that can be drawn on a shape layer.
 */
class SolidColor : public ColorSource {
 public:
  /**
   * Creates a new SolidColor with the given color.
   */
  static std::shared_ptr<SolidColor> Make(const Color& color = Color::Black());

  /**
   * Returns the color of this SolidColor.
   */
  const Color& color() const {
    return _color;
  }

  /**
   * Sets the color of this SolidColor.
   */
  void setColor(const Color& color);

 protected:
  Type getType() const override {
    return Type::SolidColor;
  }

  std::shared_ptr<Shader> onGetShader() const override;

 private:
  explicit SolidColor(const Color& color) : _color(color) {
  }

 private:
  Color _color;

  friend class SolidLayer;
};
}  // namespace tgfx
