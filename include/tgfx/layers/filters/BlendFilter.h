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

#include "tgfx/layers/filters/LayerImageFilter.h"

namespace tgfx {

class BlendFilter : public LayerImageFilter {
 public:
  ~BlendFilter() override = default;

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
  std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) override;

 private:
  BlendFilter(const Color& color, BlendMode blendMode);

  Color _color = Color::Transparent();
  BlendMode _blendMode = BlendMode::SrcOver;
};
}  // namespace tgfx