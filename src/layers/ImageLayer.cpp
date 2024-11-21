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
#include "layers/contents/ImageContent.h"
#include "core/utils/Profiling.h"

namespace tgfx {
std::shared_ptr<ImageLayer> ImageLayer::Make() {
  TRACE_ZONE_SCOPED_N("ImageLayer::Make");
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

std::unique_ptr<LayerContent> ImageLayer::onUpdateContent() {
  if (!_image) {
    return nullptr;
  }
  return std::make_unique<ImageContent>(_image, _sampling);
}
}  // namespace tgfx
