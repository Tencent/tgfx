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
#include "tgfx/core/Shader.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/SolidLayer.h"
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
  backLineLayer->setStrokeStyle(tgfx::ShapeStyle::Make(tgfx::Color::FromRGBA(143, 195, 228)));
  backLineLayer->setLineWidth(6);
  backLineLayer->setAlpha(0.1f);
  progressBar->addChild(backLineLayer);

  auto frontLineLayer = tgfx::ShapeLayer::Make();
  auto frontLinePath = tgfx::Path();
  frontLinePath.lineTo(222, 0);
  frontLineLayer->setPath(frontLinePath);
  frontLineLayer->setStrokeStyle(tgfx::ShapeStyle::Make(tgfx::Color::FromRGBA(167, 223, 246)));
  frontLineLayer->setLineWidth(6);
  progressBar->addChild(frontLineLayer);

  auto circleLayer = tgfx::ShapeLayer::Make();
  auto circlePath = tgfx::Path();
  circlePath.addOval(tgfx::Rect::MakeWH(22, 22));
  circleLayer->setFillStyle(tgfx::ShapeStyle::Make(tgfx::Color::FromRGBA(192, 221, 241)));
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
  background->setFillStyle(tgfx::ShapeStyle::Make(tgfx::Color::FromRGBA(72, 154, 209)));
  background->setPath(backPath);
  layers.push_back(background);

  auto backgroundGradient = tgfx::ShapeLayer::Make();
  auto gradientShader = tgfx::Shader::MakeLinearGradient(
      {0, 0}, {0, 430},
      {tgfx::Color::FromRGBA(233, 0, 100), tgfx::Color::FromRGBA(134, 93, 255, 0)}, {});
  auto gradientPath = tgfx::Path();
  gradientPath.addRect(tgfx::Rect::MakeXYWH(0, 0, 375, 430));
  gradientPath.addPath(backPath, tgfx::PathOp::Intersect);
  backgroundGradient->setFillStyle(tgfx::ShapeStyle::Make(gradientShader));
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
  maskLayer->setFillStyle(tgfx::ShapeStyle::Make(tgfx::Color::White()));
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

static inline tgfx::Matrix3D MakePerspectiveMatrix() {
  auto perspectiveMatrix = tgfx::Matrix3D::I();
  constexpr float eyeDistance = 1200.f;
  perspectiveMatrix.setRowColumn(3, 2, -1.f / eyeDistance);
  return perspectiveMatrix;
}

static inline std::shared_ptr<tgfx::Layer> Create3DBackLayer(const tgfx::Point& origin) {
  auto layer = tgfx::ShapeLayer::Make();
  auto rect = tgfx::Rect::MakeWH(600, 400);
  tgfx::Path path = {};
  path.addRect(rect);
  layer->setPath(path);
  std::shared_ptr<tgfx::Shader> shader = tgfx::Shader::MakeLinearGradient(
      {rect.left, 0}, {rect.right, 0}, {tgfx::Color::Red(), tgfx::Color::Green()});
  layer->addFillStyle(tgfx::ShapeStyle::Make(shader, 1.f));
  auto matrix = tgfx::Matrix::MakeScale(0.5, 0.5);
  matrix.postTranslate(origin.x, origin.y);
  layer->setMatrix(matrix);
  return layer;
}

static inline std::shared_ptr<tgfx::Layer> Create3DContainerLayer(const tgfx::Point& origin) {
  auto layer = tgfx::SolidLayer::Make();
  layer->setColor(tgfx::Color::FromRGBA(151, 153, 46, 255));
  auto layerSize = tgfx::Size::Make(360.f, 320.f);
  layer->setWidth(layerSize.width);
  layer->setHeight(layerSize.height);
  auto anchor = tgfx::Point::Make(0.3f, 0.3f);
  auto offsetToAnchorMatrix =
      tgfx::Matrix3D::MakeTranslate(-anchor.x * layerSize.width, -anchor.y * layerSize.height, 0.f);
  auto invOffsetToAnchorMatrix =
      tgfx::Matrix3D::MakeTranslate(anchor.x * layerSize.width, anchor.y * layerSize.height, 0.f);
  auto modelMatrix = tgfx::Matrix3D::MakeRotate({0.f, 1.f, 0.f}, -45.f);
  auto perspectiveMatrix = MakePerspectiveMatrix();
  auto originTranslateMatrix = tgfx::Matrix3D::MakeTranslate(origin.x, origin.y, 0.f);
  auto transformMatrix = originTranslateMatrix * invOffsetToAnchorMatrix * perspectiveMatrix *
                         modelMatrix * offsetToAnchorMatrix;
  layer->setMatrix3D(transformMatrix);
  return layer;
}

static inline float DegreesToRadians(float degrees) {
  return degrees * (static_cast<float>(M_PI) / 180.0f);
}

static inline std::shared_ptr<tgfx::Layer> Create3DLayer(const AppHost* host,
                                                         const tgfx::Point& origin) {
  auto image = host->getImage("imageReplacement");
  if (!image) {
    return nullptr;
  }

  auto shadowFilter = tgfx::DropShadowFilter::Make(-20, -20, 0, 0, tgfx::Color::Green());
  auto imageLayer = tgfx::ImageLayer::Make();
  imageLayer->setImage(image);
  imageLayer->setFilters({shadowFilter});
  auto imageMatrix3D = tgfx::Matrix3D::I();
  auto imageSize =
      tgfx::Size::Make(static_cast<float>(image->width()), static_cast<float>(image->height()));
  auto anchor = tgfx::Point::Make(0.5f, 0.5f);
  auto offsetToAnchorMatrix =
      tgfx::Matrix3D::MakeTranslate(-anchor.x * imageSize.width, -anchor.y * imageSize.height, 0.f);
  auto invOffsetToAnchorMatrix =
      tgfx::Matrix3D::MakeTranslate(anchor.x * imageSize.width, anchor.y * imageSize.height, 0.f);
  auto modelMatrix = tgfx::Matrix3D::MakeScale(2.f, 2.f, 1.f);
  constexpr float skewXDegrees = -15.f;
  constexpr float skewYDegrees = -15.f;
  modelMatrix.postSkewXY(tanf(DegreesToRadians(skewXDegrees)),
                         tanf(DegreesToRadians(skewYDegrees)));
  modelMatrix.postRotate({0.f, 0.f, 1.f}, 45.f);
  modelMatrix.preRotate({1.f, 0.f, 0.f}, 45.f);
  modelMatrix.preRotate({0.f, 1.f, 0.f}, 45.f);
  modelMatrix.postTranslate(0.f, 0.f, 20.f);
  auto perspectiveMatrix = MakePerspectiveMatrix();
  auto originTranslateMatrix = tgfx::Matrix3D::MakeTranslate(origin.x, origin.y, 0.f);
  imageMatrix3D = originTranslateMatrix * invOffsetToAnchorMatrix * perspectiveMatrix *
                  modelMatrix * offsetToAnchorMatrix;
  imageLayer->setMatrix3D(imageMatrix3D);
  return imageLayer;
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
  root->addChild(textLayer);

  // progress shape
  auto progressBar = CreateProgressBar();
  root->addChild(progressBar);

  // Flat 3D Container
  auto flat3DLayer = Create3DLayer(host, {125, 105});
  auto flat3DContainerLayer = Create3DContainerLayer({120, 40});
  flat3DContainerLayer->addChild(flat3DLayer);
  auto flat3DBackLayer = Create3DBackLayer({400, 0});
  flat3DBackLayer->addChild(flat3DContainerLayer);
  root->addChild(flat3DBackLayer);

  // Preserve 3D Container
  auto preserve3DLayer = Create3DLayer(host, {125, 105});
  auto preserve3DContainerLayer = Create3DContainerLayer({120, 40});
  preserve3DContainerLayer->setPreserve3D(true);
  preserve3DContainerLayer->addChild(preserve3DLayer);
  auto preserve3DBackLayer = Create3DBackLayer({400, 300});
  preserve3DBackLayer->addChild(preserve3DContainerLayer);
  root->addChild(preserve3DBackLayer);

  return root;
}
}  // namespace hello2d
