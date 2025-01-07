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
#include "layers/contents/SolidContent.h"

namespace tgfx {
std::shared_ptr<SolidLayer> SolidLayer::Make() {
  auto layer = std::shared_ptr<SolidLayer>(new SolidLayer());
  layer->weakThis = layer;
  return layer;
}

void SolidLayer::setWidth(float width) {
  if (width < 0) {
    width = 0;
  }
  if (_width == width) {
    return;
  }
  _width = width;
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
  invalidateContent();
}

void SolidLayer::setRadiusX(float radiusX) {
  if (_radiusX == radiusX) {
    return;
  }
  _radiusX = radiusX;
  invalidateContent();
}

void SolidLayer::setRadiusY(float radiusY) {
  if (_radiusY == radiusY) {
    return;
  }
  _radiusY = radiusY;
  invalidateContent();
}

void SolidLayer::setColor(const Color& color) {
  if (_color == color) {
    return;
  }
  _color = color;
  invalidateContent();
}

std::unique_ptr<LayerContent> SolidLayer::onUpdateContent() {
  if (_width == 0 || _height == 0) {
    return nullptr;
  }
  RRect rRect = {};
  rRect.setRectXY(Rect::MakeLTRB(0, 0, _width, _height), _radiusX, _radiusY);
  auto content = std::make_unique<SolidContent>(rRect, _color);
  return content;
}

}  // namespace tgfx