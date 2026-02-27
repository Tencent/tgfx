/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "base/LayerBuilders.h"
#include "tgfx/layers/ImageLayer.h"

namespace hello2d {
std::shared_ptr<tgfx::Layer> ImageWithMipmap::onBuildLayerTree(const AppHost* host) {
  auto root = tgfx::Layer::Make();

  // Fixed size: 720x720 with 50px padding, content area: 620x620
  constexpr auto size = 620;

  auto image = host->getImage("bridge");
  if (image == nullptr) {
    return root;
  }
  image = image->makeMipmapped(true);
  auto imageScale = static_cast<float>(size) / static_cast<float>(image->width());
  auto matrix = tgfx::Matrix::MakeScale(imageScale);
  auto imageLayer = tgfx::ImageLayer::Make();
  imageLayer->setImage(image);
  imageLayer->setSampling(
      tgfx::SamplingOptions(tgfx::FilterMode::Linear, tgfx::MipmapMode::Linear));
  imageLayer->setMatrix(matrix);
  root->addChild(imageLayer);
  return root;
}
}  // namespace hello2d
