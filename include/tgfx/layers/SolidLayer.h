/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "SolidColor.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/ShapeStyle.h"

namespace tgfx {
/**
 * A layer that fills its bounds with a solid color, gradient, or image pattern.
 */
class SolidLayer : public Layer {
 public:
  /**
   * Creates a new solid layer with the given width, height, and fill style.
   */
  static std::shared_ptr<SolidLayer> Make(int width, int height, std::shared_ptr<ShapeStyle> style);

  LayerType type() const override {
    return LayerType::Solid;
  }
  /**
   * Returns the style used to fill the layer, which can be a solid color, gradient, or image
   * pattern.
   */
  std::shared_ptr<ShapeStyle> fillStyle() const {
    return _fillStyle;
  }

  /**
   * Sets the style used to fill the layer. If the fill style is set to nullptr, the shape will not
   * be filled.
   */
  void setFillStyle(std::shared_ptr<ShapeStyle> style);

 protected:
  SolidLayer(int width, int height, std::shared_ptr<ShapeStyle> style);

  std::unique_ptr<LayerContent> onUpdateContent() override;

 private:
  std::shared_ptr<ShapeStyle> _fillStyle = nullptr;

  Path _path = {};
};

}  // namespace tgfx
