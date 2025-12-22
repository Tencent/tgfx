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
#include "tgfx/layers/Gradient.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidColor.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/filters/DropShadowFilter.h"

namespace hello2d {

static std::shared_ptr<tgfx::Layer> CreateProgressBar() {
  auto progressBar = tgfx::Layer::Make();
  progressBar->setMatrix(tgfx::Matrix::MakeTrans(24, 670));
  progressBar->setBlendMode(tgfx::BlendMode::ColorDodge);
  auto backLineLayer = tgfx::ShapeLayer::Make();
  auto backLinePath = tgfx::Path();
  backLinePath.lineTo(327, 0);
  backLineLayer->setPath(backLinePath);
  backLineLayer->setStrokeStyle(tgfx::SolidColor::Make(tgfx::Color::FromRGBA(143, 195, 228)));
  backLineLayer->setLineWidth(6);
  backLineLayer->setAlpha(0.1f);
  progressBar->addChild(backLineLayer);

  auto frontLineLayer = tgfx::ShapeLayer::Make();
  auto frontLinePath = tgfx::Path();
  frontLinePath.lineTo(222, 0);
  frontLineLayer->setPath(frontLinePath);
  frontLineLayer->setStrokeStyle(tgfx::SolidColor::Make(tgfx::Color::FromRGBA(167, 223, 246)));
  frontLineLayer->setLineWidth(6);
  progressBar->addChild(frontLineLayer);

  auto circleLayer = tgfx::ShapeLayer::Make();
  auto circlePath = tgfx::Path();
  circlePath.addOval(tgfx::Rect::MakeWH(22, 22));
  circleLayer->setFillStyle(tgfx::SolidColor::Make(tgfx::Color::FromRGBA(192, 221, 241)));
  circleLayer->setPath(circlePath);
  circleLayer->setMatrix(tgfx::Matrix::MakeTrans(211, -11));
  progressBar->addChild(circleLayer);

  return progressBar;
}

static std::vector<std::shared_ptr<tgfx::Layer>> CreateBackground() {
  std::vector<std::shared_ptr<tgfx::Layer>> layers;
  layers.reserve(4);

  auto background = tgfx::ShapeLayer::Make();
  tgfx::Rect displayRect = tgfx::Rect::MakeWH(375, 812);
  auto backPath = tgfx::Path();
  backPath.addRoundRect(displayRect, 40, 40);
  background->setFillStyle(tgfx::SolidColor::Make(tgfx::Color::FromRGBA(72, 154, 209)));
  background->setPath(backPath);
  layers.push_back(background);

  auto backgroundGradient = tgfx::ShapeLayer::Make();
  auto gradient = tgfx::Gradient::MakeLinear(
      {0, 0}, {0, 430},
      {tgfx::Color::FromRGBA(233, 0, 100), tgfx::Color::FromRGBA(134, 93, 255, 0)});
  auto gradientPath = tgfx::Path();
  gradientPath.addRect(tgfx::Rect::MakeXYWH(0, 0, 375, 430));
  gradientPath.addPath(backPath, tgfx::PathOp::Intersect);
  backgroundGradient->setFillStyle(gradient);
  backgroundGradient->setPath(gradientPath);
  backgroundGradient->setAlpha(0.2f);
  layers.push_back(backgroundGradient);

  return layers;
}

static std::shared_ptr<tgfx::Layer> CreateImageLayer(const AppHost* host) {
  auto image = host->getImage("bridge");
  if (!image) {
    return nullptr;
  }
  auto card = tgfx::Layer::Make();
  auto cardMatrix = tgfx::Matrix::MakeTrans(24, 150);
  auto imageLayer = tgfx::ImageLayer::Make();
  imageLayer->setImage(image);
  auto imageScale = static_cast<float>(std::min(327.0 / image->width(), 344.0 / image->height()));
  auto maskLayer = tgfx::ShapeLayer::Make();
  maskLayer->setFillStyle(tgfx::SolidColor::Make());
  auto maskPath = tgfx::Path();
  auto radius = 20.f / imageScale;
  maskPath.addRoundRect(tgfx::Rect::MakeWH(image->width(), image->height()), radius, radius);
  maskLayer->setPath(maskPath);
  imageLayer->setMask(maskLayer);
  cardMatrix.preScale(imageScale, imageScale);
  card->setMatrix(cardMatrix);
  card->addChild(imageLayer);
  card->addChild(maskLayer);
  card->setFilters(
      {tgfx::DropShadowFilter::Make(0, 8, 32, 32, tgfx::Color::FromRGBA(6, 0, 71, 51))});
  return card;
}

std::shared_ptr<tgfx::Layer> SimpleLayerTree::onBuildLayerTree(const AppHost* host) {
  auto root = tgfx::Layer::Make();

  // background
  for (auto& layer : CreateBackground()) {
    root->addChild(layer);
  }

  // image
  root->addChild(CreateImageLayer(host));

  // text
  auto textLayer = tgfx::TextLayer::Make();
  textLayer->setText("        TGFX  |  Image of bridge");
  textLayer->setMatrix(tgfx::Matrix::MakeTrans(48, 550));
  tgfx::Font font(host->getTypeface("default"), 18);
  textLayer->setFont(font);
  textLayer->setTextColor(tgfx::Color::Black());
  root->addChild(textLayer);

  // progress shape
  auto progressBar = CreateProgressBar();
  root->addChild(progressBar);

  return root;
}
}  // namespace hello2d