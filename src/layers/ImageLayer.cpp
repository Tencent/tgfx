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

#include "tgfx/layers/ImageLayer.h"

namespace tgfx {
std::shared_ptr<ImageLayer> ImageLayer::Make() {
  auto layer = std::shared_ptr<ImageLayer>(new ImageLayer());
  layer->weakThis = layer;
  return layer;
}

void ImageLayer::setSampling(const SamplingOptions& value) {
  if (_sampling == value) {
    return;
  }
  _sampling = value;
  invalidateContent();
}

void ImageLayer::setImage(std::shared_ptr<Image> value) {
  if (_image == value) {
    return;
  }
  _image = value;
  invalidateContent();
}

void ImageLayer::onDraw(Canvas* canvas, const Paint& paint) {
  if (!_image) {
    return;
  }
  canvas->drawImage(_image, _sampling, &paint);
}

Rect ImageLayer::measureContentBounds() const {
  if (!_image) {
    return Rect::MakeEmpty();
  }
  return Rect::MakeWH(_image->width(), _image->height());
}

}  // namespace tgfx
