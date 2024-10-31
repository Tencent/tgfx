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
 * A layer that fills its bounds with a solid color.
 */
class SolidLayer : public Layer {
 public:
  /**
   * Creates a new solid layer with the given dimensions and solid color.
   */
  static std::shared_ptr<SolidLayer> Make(float width, float height,
                                          std::shared_ptr<SolidColor> color);

  LayerType type() const override {
    return LayerType::Solid;
  }

  /**
   * Returns the width of the layer.
   */
  float width() const {
    return _width;
  }

  /**
   * Sets the width of the layer.
   */
  void setWidth(float widht);

  /**
   * Returns the height of the layer.
   */
  float height() const {
    return _height;
  }

  /**
   * Sets the height of the layer.
   */
  void setHeight(float height);

  /**
   * Returns the axis length on x-axis of the rounded corners.
   */
  float radiusX() const {
    return _radiusX;
  }

  /**
   * Sets the axis length on x-axis of the rounded corners.
   */
  void setRadiusX(float radiusX);

  /**
   * Returns the axis length on y-axis of the rounded corners.
   */
  float radiusY() const {
    return _radiusY;
  }

  /**
   * Sets the axis length on y-axis of the rounded corners.
   */
  void setRadiusY(float radiusY);

  /**
   * Returns the solid color used to fill the layer.
   */
  std::shared_ptr<SolidColor> solidColor() const {
    return _solidColor;
  }

  /**
   * Sets the solid color used to fill the layer.
   */
  void setSolidColor(std::shared_ptr<SolidColor> color);

 protected:
  SolidLayer(float width, float height, std::shared_ptr<SolidColor> color);

  std::unique_ptr<LayerContent> onUpdateContent() override;

 private:
  std::shared_ptr<SolidColor> _solidColor = nullptr;
  float _width = 0;
  float _height = 0;
  float _radiusX = 0;
  float _radiusY = 0;
  Path _path = {};
};

}  // namespace tgfx
