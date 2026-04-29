/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"

namespace hello2d {
std::shared_ptr<tgfx::Layer> ImageWithBackgroundBlur::onBuildLayerTree(
    const hello2d::AppHost* host) {
  auto root = tgfx::Layer::Make();

  constexpr auto kContent = 620.0f;
  constexpr auto kPadding = 50.0f;

  auto image = host->getImage("bridge");
  if (image == nullptr) {
    return root;
  }
  image = image->makeMipmapped(true);

  auto backdrop = tgfx::ImageLayer::Make();
  backdrop->setImage(image);
  auto imageScale = kContent / static_cast<float>(image->width());
  auto imageMatrix = tgfx::Matrix::MakeScale(imageScale);
  imageMatrix.postTranslate(kPadding, kPadding);
  backdrop->setMatrix(imageMatrix);
  root->addChild(backdrop);

  // A translucent rounded-rect pill sampling the image backdrop through BackgroundBlur.
  auto pill = tgfx::ShapeLayer::Make();
  tgfx::Path pillPath;
  pillPath.addRoundRect(tgfx::Rect::MakeWH(440, 200), 100, 100);
  pill->setPath(pillPath);
  pill->setFillStyle(tgfx::ShapeStyle::Make(tgfx::Color::FromRGBA(255, 255, 255, 60)));
  pill->setMatrix(tgfx::Matrix::MakeTrans(kPadding + 90, kPadding + 210));
  pill->setLayerStyles({tgfx::BackgroundBlurStyle::Make(24, 24)});
  root->addChild(pill);

  // A small tinted square nested one level deeper to exercise the sub BackgroundSource path (an
  // offscreen carrier is triggered by the parent's filter-equivalent setup below).
  auto stack = tgfx::Layer::Make();
  stack->setMatrix(tgfx::Matrix::MakeTrans(kPadding + 200, kPadding + 430));

  auto swatch = tgfx::SolidLayer::Make();
  swatch->setColor(tgfx::Color::FromRGBA(255, 90, 90, 120));
  swatch->setWidth(240);
  swatch->setHeight(120);
  swatch->setLayerStyles({tgfx::BackgroundBlurStyle::Make(12, 12)});
  stack->addChild(swatch);

  root->addChild(stack);
  return root;
}
}  // namespace hello2d
