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

#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

/**
 * A filter that applies blends between the constant color (src) and input color (dst) based on the
 * BlendMode.
 */
class BlendFilter : public LayerFilter {
 public:
  /**
   * Creates a new ColorFilter that applies blends between the constant color (src) and input color
   * (dst) based on the BlendMode.
   */
  static std::shared_ptr<BlendFilter> Make(const Color& color, BlendMode mode);

  /**
   * Returns the constant color to be used when blending.
   */
  Color color() const {
    return _color;
  }

  /**
   * Sets the constant color to be used when blending.
   * @param color The color to be used when blending.
   */
  void setColor(const Color& color);

  /**
   * Returns the blend mode to be used when blending.
   */
  BlendMode blendMode() const {
    return _blendMode;
  }

  /**
   * Sets the blend mode to be used when blending.
   * @param mode The blend mode to be used when blending.
   */
  void setBlendMode(BlendMode mode);

 protected:
  Type type() const override {
    return Type::BlendFilter;
  }

  std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) override;

 private:
  BlendFilter(const Color& color, BlendMode blendMode);

  Color _color = Color::Transparent();
  BlendMode _blendMode = BlendMode::SrcOver;
};
}  // namespace tgfx
