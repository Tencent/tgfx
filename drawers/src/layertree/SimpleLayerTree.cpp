/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "SimpleLayerTree.h"
#include <tgfx/layers/layerstyles/BackgroundBlurStyle.h>
#include <tgfx/layers/layerstyles/DropShadowStyle.h>
#include "tgfx/layers/Gradient.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidColor.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/filters/DropShadowFilter.h"

namespace drawers {

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

static std::shared_ptr<tgfx::Layer> CreateBackground() {
  auto background = tgfx::ShapeLayer::Make();
  tgfx::Rect displayRect = tgfx::Rect::MakeWH(375, 812);
  auto backPath = tgfx::Path();
  backPath.addRoundRect(displayRect, 40, 40);
  background->setFillStyle(tgfx::SolidColor::Make(tgfx::Color::FromRGBA(72, 154, 209)));
  background->setPath(backPath);

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
  background->addChild(backgroundGradient);
  return background;
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
  imageLayer->setAlpha(0.01f);
  cardMatrix.preScale(imageScale, imageScale);
  card->setMatrix(cardMatrix);
  card->addChild(imageLayer);
  card->addChild(maskLayer);
  // card->setFilters(
  //     {tgfx::DropShadowFilter::Make(0, 8, 32, 32, tgfx::Color::FromRGBA(6, 0, 71, 51))});
  // card->setLayerStyles(
      // {tgfx::DropShadowStyle::Make(0, 8, 8, 8, tgfx::Color::FromRGBA(6, 0, 71, 255)),
       // tgfx::BackgroundBlurStyle::Make(8.0f, 8.0f)});
  return card;
}

std::shared_ptr<tgfx::Layer> SimpleLayerTree::buildLayerTree(const AppHost* host) {
  auto root = tgfx::Layer::Make();
  // background
  root->addChild(CreateBackground());

  // image
  root->addChild(CreateImageLayer(host));

  // text
  auto textLayer = tgfx::ShapeLayer::Make();
  // textLayer->setText("        TGFX  |  Image of bridge");
  textLayer->setMatrix(tgfx::Matrix::MakeTrans(0, 400));
  // tgfx::Font font(host->getTypeface("default"), 18);
  // textLayer->setFont(font);
  auto shape = tgfx::Path();
  shape.addRect(tgfx::Rect::MakeWH(279, 300));
  textLayer->setPath(shape);
  textLayer->setFillStyle(tgfx::SolidColor::Make(tgfx::Color::FromRGBA(255, 255, 255, 100)));
  auto shapeLayer = tgfx::ShapeLayer::Make();
  auto textPath = tgfx::Path();
  textPath.addRect(tgfx::Rect::MakeWH(279, 24));
  shapeLayer->setPath(textPath);
  shapeLayer->setFillStyle(tgfx::SolidColor::Make(tgfx::Color::FromRGBA(255, 255, 255, 1)));
  shapeLayer->setMatrix(tgfx::Matrix::MakeTrans(48, 550));
  shapeLayer->setLayerStyles({tgfx::BackgroundBlurStyle::Make(5.0f, 5.0f, tgfx::TileMode::Clamp)});
  root->addChild(textLayer);
  root->addChild(shapeLayer);

  // progress shape
  auto progressBar = CreateProgressBar();
  root->addChild(progressBar);
  return root;
}
}  // namespace drawers
