/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/layers/Layer.h"
#include "tgfx/layers/vectors/SolidColor.h"

namespace tgfx {
/**
 * A layer that fills its bounds with a solid color.
 */
class SolidLayer : public Layer {
 public:
  /**
   * Creates a new solid layer.
   */
  static std::shared_ptr<SolidLayer> Make();

  LayerType type() const override {
    return LayerType::Solid;
  }

  /**
   * Returns the width of the solid layer. The default value is 0.
   */
  float width() const {
    return _width;
  }

  /**
   * Sets the width of the solid layer.
   */
  void setWidth(float width);

  /**
   * Returns the height of the solid layer. The default value is 0.
   */
  float height() const {
    return _height;
  }

  /**
   * Sets the height of the solid layer.
   */
  void setHeight(float height);

  /**
   * Returns the x-axis radius of corners. The default value is 0.
   */
  float radiusX() const {
    return _radiusX;
  }

  /**
   * Sets the x-axis radius of corners.
   */
  void setRadiusX(float radiusX);

  /**
   * Returns the y-axis radius of corners. The default value is 0.
   */
  float radiusY() const {
    return _radiusY;
  }

  /**
   * Sets the y-axis radius of corners.
   */
  void setRadiusY(float radiusY);

  /**
   * Returns the color used to fill the solid layer. The default color is opaque white.
   */
  Color color() const {
    return _color;
  }

  /**
   * Sets the color used to fill the solid layer.
   */
  void setColor(const Color& color);

 protected:
  SolidLayer() = default;

  void onUpdateContent(LayerRecorder* recorder) override;

 private:
  Color _color = {};
  float _width = 0;
  float _height = 0;
  float _radiusX = 0;
  float _radiusY = 0;
};

}  // namespace tgfx
