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
std::shared_ptr<SolidLayer> SolidLayer::Make(int width, int height,
                                             std::shared_ptr<ShapeStyle> style) {
  if (width == 0 || height == 0) {
    return nullptr;
  }
  auto layer = std::shared_ptr<SolidLayer>(new SolidLayer(width, height, std::move(style)));
  layer->weakThis = layer;
  return layer;
}

SolidLayer::SolidLayer(int width, int height, std::shared_ptr<ShapeStyle> style) {
  _path.addRect(0, 0, static_cast<float>(width), static_cast<float>(height));
  setFillStyle(std::move(style));
}

void SolidLayer::setFillStyle(std::shared_ptr<ShapeStyle> style) {
  if (_fillStyle == style) {
    return;
  }
  _fillStyle = std::move(style);
  invalidateContent();
}

std::unique_ptr<LayerContent> SolidLayer::onUpdateContent() {
  std::vector<std::unique_ptr<LayerContent>> contents = {};
  auto shader = _fillStyle ? _fillStyle->getShader() : nullptr;
  auto content = std::make_unique<ShapeContent>(_path, shader);
  contents.push_back(std::move(content));
  return LayerContent::Compose(std::move(contents));
}

}  // namespace tgfx