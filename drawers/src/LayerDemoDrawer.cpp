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

#include "drawers/LayerDemoDrawer.h"
#include "CustomLayer.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/Gradient.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/ImagePattern.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/filters/DropShadowFilter.h"

namespace drawers {

static std::shared_ptr<tgfx::Layer> MakeRoundRectMask(const tgfx::Rect& rect, float radius) {
  auto mask = tgfx::ShapeLayer::Make();
  auto maskPath = tgfx::Path();
  maskPath.addRoundRect(rect, radius, radius);
  mask->setPath(maskPath);
  mask->setFillStyle(tgfx::SolidColor::Make(tgfx::Color::FromRGBA(0, 0, 0)));
  return nullptr;
}

static std::shared_ptr<tgfx::Layer> createProgressBar() {
  auto progressBar = tgfx::Layer::Make();
  progressBar->setMatrix(tgfx::Matrix::MakeTrans(24, 670));
  progressBar->setBlendMode(tgfx::BlendMode::PlusDarker);
  auto backLineLayer = tgfx::ShapeLayer::Make();
  auto backLinePath = tgfx::Path();
  backLinePath.lineTo(327, 0);
  backLineLayer->setPath(backLinePath);
  backLineLayer->setStrokeStyle(tgfx::SolidColor::Make(tgfx::Color::FromRGBA(143, 195, 228)));
  backLineLayer->setLineWidth(6);
  backLineLayer->setAlpha(0.1);
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

static std::shared_ptr<tgfx::Layer> CreateBackground(double displayListWidth,
                                                     double displayListHeight) {
  auto background = tgfx::ShapeLayer::Make();
  auto backPath = tgfx::Path();
  backPath.addRect(tgfx::Rect::MakeXYWH(0, 0, displayListWidth, displayListHeight));
  background->setFillStyle(tgfx::SolidColor::Make(tgfx::Color::FromRGBA(72, 154, 209)));
  background->setPath(backPath);
  auto backgroundMask =
      MakeRoundRectMask(tgfx::Rect::MakeXYWH(0, 0, displayListWidth, displayListHeight), 40);
  background->setMask(backgroundMask);
  background->addChild(backgroundMask);

  auto backgroundGradient = tgfx::ShapeLayer::Make();
  auto gradient = tgfx::Gradient::MakeLinear({0, 0}, {0, 430});
  gradient->setColors({tgfx::Color::FromRGBA(233, 0, 100), tgfx::Color::FromRGBA(134, 93, 255, 0)});
  auto gradientPath = tgfx::Path();
  gradientPath.addRect(tgfx::Rect::MakeXYWH(0, 0, 375, 430));
  gradientPath.addPath(backPath, tgfx::PathOp::Intersect);
  backgroundGradient->setFillStyle(gradient);
  backgroundGradient->setPath(gradientPath);
  backgroundGradient->setAlpha(0.2);
  background->addChild(backgroundGradient);
  return background;
}

void LayerDemoDrawer::initDisplayList() {
  auto displayListWidth = 375.0;
  auto displayListHeight = 812.0;
  root = tgfx::Layer::Make();

  // background
  root->addChild(CreateBackground(displayListWidth, displayListHeight));

  // image and mask
  auto card = tgfx::Layer::Make();
  card->setMatrix(tgfx::Matrix::MakeTrans(24, 150));

  imageLayer = tgfx::ImageLayer::Make();
  //   imageMaskLayer = tgfx::ShapeLayer::Make();
  //   imageMaskLayer->setFillStyle(tgfx::SolidColor::Make(tgfx::Color::FromRGBA(0, 0, 0)));
  imageLayer->addChild(imageMaskLayer);
  card->addChild(imageLayer);
  card->setFilters(
      {tgfx::DropShadowFilter::Make(0, 8, 32, 32, tgfx::Color::FromRGBA(6, 0, 71, 51))});
  root->addChild(card);

  // customLayer (text)
  textLayer = CustomLayer::Make();
  textLayer->setText("612: Eliza Jackson  |  The Real Life \n            of a UI Designer");
  textLayer->setMatrix(tgfx::Matrix::MakeTrans(48, 550));
  root->addChild(textLayer);

  // progress shape
  root->addChild(createProgressBar());

  displayList.root()->addChild(root);
}

bool LayerDemoDrawer::onDraw(tgfx::Surface* surface, const AppHost* host) {
  updateRootMatrix(host);
  updateImage(host);
  updateFont(host);
  return displayList.render(surface);
}

void LayerDemoDrawer::updateRootMatrix(const AppHost* host) {
  auto padding = 30.0;
  auto displayListWidth = 375.0;
  auto displayListHeight = 812.0;
  auto totalScale = std::min(host->width() / (padding * 2 + displayListWidth),
                             host->height() / (padding * 2 + displayListHeight));

  auto rootMatrix = tgfx::Matrix::MakeScale(totalScale);
  rootMatrix.postTranslate((host->width() - displayListWidth * totalScale) / 2,
                           (host->height() - displayListHeight * totalScale) / 2);

  root->setMatrix(rootMatrix);
}

void LayerDemoDrawer::updateImage(const AppHost* host) {
  auto image = host->getImage("bridge");
  if (image == imageLayer->image()) {
    return;
  }
  auto imageScale = std::min(327.0 / image->width(), 344.0 / image->height());
  imageLayer->setImage(image);
  imageLayer->setMatrix(tgfx::Matrix::MakeScale(imageScale));
  tgfx::Path maskPath;
  auto radius = 20.0f / imageScale;
  maskPath.addRoundRect(tgfx::Rect::MakeWH(image->width(), image->height()), radius, radius);
  //  imageMaskLayer->setPath(maskPath);
}

void LayerDemoDrawer::updateFont(const AppHost* host) {
  tgfx::Font font(host->getTypeface("default"), 18);
  textLayer->setFont(font);
}

void LayerDemoDrawer::changeText() {
  auto text = textLayer->text();
  textLayer->setText(text + "!");
}

}  // namespace drawers
