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

#include "LayerDemoTree.h"
#include "CustomLayer.h"
#include "tgfx/layers/Gradient.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/ImagePattern.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/filters/DropShadowFilter.h"

namespace drawers {

static std::shared_ptr<tgfx::Layer> CreateProgressBar() {
  auto progressBar = tgfx::Layer::Make();
  progressBar->setMatrix(tgfx::Matrix::MakeTrans(24, 670));
  progressBar->setBlendMode(tgfx::BlendMode::PlusDarker);
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

static std::shared_ptr<tgfx::Layer> CreateBackground() {
  auto background = tgfx::ShapeLayer::Make();
  tgfx::Rect displayRect = tgfx::Rect::MakeWH(375, 812);
  auto backPath = tgfx::Path();
  backPath.addRoundRect(displayRect, 40, 40);
  background->setFillStyle(tgfx::SolidColor::Make(tgfx::Color::FromRGBA(72, 154, 209)));
  background->setPath(backPath);

  auto backgroundGradient = tgfx::ShapeLayer::Make();
  auto gradient = tgfx::Gradient::MakeLinear({0, 0}, {0, 430});
  gradient->setColors({tgfx::Color::FromRGBA(233, 0, 100), tgfx::Color::FromRGBA(134, 93, 255, 0)});
  auto gradientPath = tgfx::Path();
  gradientPath.addRect(tgfx::Rect::MakeXYWH(0, 0, 375, 430));
  gradientPath.addPath(backPath, tgfx::PathOp::Intersect);
  backgroundGradient->setFillStyle(gradient);
  backgroundGradient->setPath(gradientPath);
  backgroundGradient->setAlpha(0.2f);
  background->addChild(backgroundGradient);
  return background;
}

static std::shared_ptr<tgfx::Layer> CreateImageLayer(const AppHost* host) {
  auto card = tgfx::Layer::Make();
  card->setMatrix(tgfx::Matrix::MakeTrans(24, 150));

  auto imageLayer = tgfx::ShapeLayer::Make();
  auto image = host->getImage("bridge");
  auto imageScale = static_cast<float>(std::min(327.0 / image->width(), 344.0 / image->height()));
  auto imagePath = tgfx::Path();
  auto radius = 20.f / imageScale;
  imagePath.addRoundRect(tgfx::Rect::MakeWH(image->width(), image->height()), radius, radius);
  imageLayer->setPath(imagePath);
  imageLayer->setFillStyle(tgfx::ImagePattern::Make(image));
  imageLayer->setMatrix(tgfx::Matrix::MakeScale(imageScale));
  card->addChild(imageLayer);
  card->setFilters(
      {tgfx::DropShadowFilter::Make(0, 8, 32, 32, tgfx::Color::FromRGBA(6, 0, 71, 51))});
  return card;
}

std::shared_ptr<tgfx::Layer> LayerDemoTree::buildLayerTree(const AppHost* host) {
  auto root = tgfx::Layer::Make();
  // background
  root->addChild(CreateBackground());

  // image
  root->addChild(CreateImageLayer(host));

  // text
  auto textLayer = tgfx::TextLayer::Make();
  textLayer->setText("612: Eliza Jackson  |  The Real Life \n            of a UI Designer");
  textLayer->setMatrix(tgfx::Matrix::MakeTrans(48, 550));
  tgfx::Font font(host->getTypeface("default"), 18);
  textLayer->setFont(font);
  root->addChild(textLayer);

  // progress shape
  progressBar = CreateProgressBar();
  root->addChild(progressBar);
  return root;
}

void LayerDemoTree::prepare(const AppHost*) {
  changeMode();
}

void LayerDemoTree::changeMode() {
  if (progressBar->blendMode() == tgfx::BlendMode::PlusDarker) {
    progressBar->setBlendMode(tgfx::BlendMode::PlusLighter);
  } else {
    progressBar->setBlendMode(tgfx::BlendMode::PlusDarker);
  }
}

}  // namespace drawers