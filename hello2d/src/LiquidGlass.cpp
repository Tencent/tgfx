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
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/layerstyles/GlassStyle.h"

namespace hello2d {

// Shared glass layer for mouse interaction.
static std::shared_ptr<tgfx::Layer> glassLayerRef = nullptr;

// Glass position (updated by mouse drag).
static float glassPosX = 110.0f;
static float glassPosY = 110.0f;

// Cell size matching GlassStyle test.
static constexpr float CELL_SIZE = 720.0f;

std::shared_ptr<tgfx::Layer> LiquidGlass::onBuildLayerTree(const hello2d::AppHost* host) {
  auto root = tgfx::Layer::Make();

  // Background: checker image scaled to fill cell.
  // Use bridge image as fallback if checker not available.
  auto bgImage = host->getImage("checker");
  if (!bgImage) {
    bgImage = host->getImage("bridge");
  }
  if (bgImage) {
    auto imgLayer = tgfx::ImageLayer::Make();
    imgLayer->setImage(bgImage);
    auto scale = CELL_SIZE / static_cast<float>(std::max(bgImage->width(), bgImage->height()));
    imgLayer->setMatrix(tgfx::Matrix::MakeScale(scale, scale));
    root->addChild(imgLayer);
  }

  // Blue rectangle for refraction contrast.
  auto blueRect = tgfx::ShapeLayer::Make();
  tgfx::Path bluePath = {};
  bluePath.addRect(tgfx::Rect::MakeXYWH(CELL_SIZE * 0.15f, CELL_SIZE * 0.15f, CELL_SIZE * 0.35f,
                                        CELL_SIZE * 0.35f));
  blueRect->setPath(bluePath);
  blueRect->setFillStyle(tgfx::ShapeStyle::Make(tgfx::Color::FromRGBA(0, 100, 255, 255)));
  root->addChild(blueRect);

  // Green circle for refraction contrast.
  auto greenCircle = tgfx::ShapeLayer::Make();
  tgfx::Path greenPath = {};
  greenPath.addOval(tgfx::Rect::MakeXYWH(CELL_SIZE * 0.45f, CELL_SIZE * 0.45f, CELL_SIZE * 0.4f,
                                         CELL_SIZE * 0.4f));
  greenCircle->setPath(greenPath);
  greenCircle->setFillStyle(tgfx::ShapeStyle::Make(tgfx::Color::FromRGBA(50, 200, 80, 200)));
  root->addChild(greenCircle);

  // Glass panel: rounded rectangle for clear refraction visibility when dragging.
  float glassSize = 200.0f;

  auto glassLayer = tgfx::SolidLayer::Make();
  glassLayer->setColor(tgfx::Color::FromRGBA(255, 255, 255, 128));
  glassLayer->setWidth(glassSize);
  glassLayer->setHeight(glassSize);
  glassLayer->setRadiusX(32);
  glassLayer->setRadiusY(32);
  glassLayer->setMatrix(tgfx::Matrix::MakeTrans(glassPosX, glassPosY));
  auto style = tgfx::GlassStyle::Make(50, 30, 10, 0, 0, 135, 100);
  style->setCornerRadius(32);
  glassLayer->setLayerStyles({style});
  root->addChild(glassLayer);

  // Store reference for mouse interaction.
  glassLayerRef = glassLayer;

  return root;
}

void LiquidGlass::setGlassPosition(float x, float y) {
  glassPosX = x;
  glassPosY = y;
  if (glassLayerRef) {
    glassLayerRef->setMatrix(tgfx::Matrix::MakeTrans(x, y));
  }
}

}  // namespace hello2d
