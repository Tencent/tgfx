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

#include "tgfx/layers/SolidLayer.h"
#include "layers/contents/ShapeContent.h"

namespace tgfx {
std::shared_ptr<SolidLayer> SolidLayer::Make(float width, float height,
                                             std::shared_ptr<SolidColor> color) {
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  auto layer = std::shared_ptr<SolidLayer>(new SolidLayer(width, height, std::move(color)));
  layer->weakThis = layer;
  return layer;
}

SolidLayer::SolidLayer(float width, float height, std::shared_ptr<SolidColor> color)
    : _width(width), _height(height) {
  _path.addRoundRect(Rect::MakeXYWH(0.0f, 0.0f, _width, _height), _radiusX, _radiusY);
  setSolidColor(std::move(color));
}

void SolidLayer::setWidth(float width) {
  if (width < 0) {
    width = 0;
  }
  if (_width == width) {
    return;
  }
  _width = width;
  _path.reset();
  _path.addRoundRect(Rect::MakeXYWH(0.0f, 0.0f, _width, _height), _radiusX, _radiusY);
  invalidateContent();
}

void SolidLayer::setHeight(float height) {
  if (height < 0) {
    height = 0;
  }
  if (_height == height) {
    return;
  }
  _height = height;
  _path.reset();
  _path.addRoundRect(Rect::MakeXYWH(0.0f, 0.0f, _width, _height), _radiusX, _radiusY);
  invalidateContent();
}

void SolidLayer::setRadiusX(float radiusX) {
  if (_radiusX == radiusX) {
    return;
  }
  _radiusX = radiusX;
  _path.reset();
  _path.addRoundRect(Rect::MakeXYWH(0.0f, 0.0f, _width, _height), _radiusX, _radiusY);
  invalidateContent();
}

void SolidLayer::setRadiusY(float radiusY) {
  if (_radiusY == radiusY) {
    return;
  }
  _radiusY = radiusY;
  _path.reset();
  _path.addRoundRect(Rect::MakeXYWH(0.0f, 0.0f, _width, _height), _radiusX, _radiusY);
  invalidateContent();
}

void SolidLayer::setSolidColor(std::shared_ptr<SolidColor> color) {
  if (_solidColor == color) {
    return;
  }
  _solidColor = std::move(color);
  invalidateContent();
}

std::unique_ptr<LayerContent> SolidLayer::onUpdateContent() {
  std::vector<std::unique_ptr<LayerContent>> contents = {};
  auto shader = _solidColor ? _solidColor->getShader() : nullptr;
  auto content = std::make_unique<ShapeContent>(_path, shader);
  contents.push_back(std::move(content));
  return LayerContent::Compose(std::move(contents));
}

}  // namespace tgfx