/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidColor.h"
#include "tgfx/layers/filters/DropShadowFilter.h"

namespace hello2d {
std::shared_ptr<tgfx::Layer> ImageWithShadow::buildLayerTree(const hello2d::AppHost* host) {
  auto root = tgfx::Layer::Make();
  auto scale = host->density();
  // The value 80 is the DropShadowFilter bound offset
  padding = 75.f * scale - 80;
  auto width = host->width();
  auto height = host->height();
  auto screenSize = std::min(width, height);
  auto size = screenSize - static_cast<int>(75.f * scale * 2);
  size = std::max(size, 50);
  auto image = host->getImage("bridge");
  if (image == nullptr) {
    return root;
  }
  image = image->makeMipmapped(true);

  auto imageLayer = tgfx::ImageLayer::Make();
  imageLayer->setImage(image);
  auto matrix =
      tgfx::Matrix::MakeScale(static_cast<float>(size) / static_cast<float>(image->width()));
  matrix.postTranslate(80, 80);
  auto maskLayer = tgfx::ShapeLayer::Make();
  maskLayer->setFillStyle(tgfx::SolidColor::Make());
  auto maskPath = tgfx::Path();
  maskPath.addOval(tgfx::Rect::MakeWH(image->width(), image->height()));
  maskLayer->setPath(maskPath);
  maskLayer->setMatrix(matrix);
  imageLayer->setMask(maskLayer);
  imageLayer->setMatrix(matrix);

  root->addChild(imageLayer);
  root->addChild(maskLayer);
  root->setFilters({tgfx::DropShadowFilter::Make(0, 0, 40, 40, tgfx::Color::Black())});
  return root;
}
}  // namespace hello2d
