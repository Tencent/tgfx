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

// Glass depth (updated by arrow keys).
static float glassDepth = 30.0f;

// Glass refraction (updated by arrow keys).
static float glassRefraction = 50.0f;

// Glass light angle (updated by Q/E keys).
static float glassLightAngle = 135.0f;

// Shared glass style for interactive parameter updates.
static std::shared_ptr<tgfx::GlassStyle> glassStyleRef = nullptr;

// Cell size matching GlassStyle test.
static constexpr float CELL_SIZE = 720.0f;

std::shared_ptr<tgfx::Layer> LiquidGlass::onBuildLayerTree(const hello2d::AppHost* host) {
  auto root = tgfx::Layer::Make();

  // Background: checker image scaled to fill cell.
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

  // Glass panel: star-shaped AlphaMask glass.
  float glassSize = 400.0f;
  float halfSize = glassSize * 0.5f;
  float outerR = halfSize * 0.9f;
  float innerR = outerR * 0.382f;
  float startAngle = -3.14159265358979323846f * 0.5f;

  auto glassLayer = tgfx::ShapeLayer::Make();
  tgfx::Path starPath = {};
  for (int i = 0; i < 5; i++) {
    float outerAngle = startAngle + static_cast<float>(i) * 3.14159265358979323846f * 0.8f;
    float innerAngle = outerAngle + 3.14159265358979323846f * 0.2f;
    if (i == 0) {
      starPath.moveTo(halfSize + outerR * cosf(outerAngle), halfSize + outerR * sinf(outerAngle));
    } else {
      starPath.lineTo(halfSize + outerR * cosf(outerAngle), halfSize + outerR * sinf(outerAngle));
    }
    starPath.lineTo(halfSize + innerR * cosf(innerAngle), halfSize + innerR * sinf(innerAngle));
  }
  starPath.close();
  glassLayer->setPath(starPath);
  glassLayer->setFillStyle(tgfx::ShapeStyle::Make(tgfx::Color::FromRGBA(255, 255, 255, 128)));
  glassLayer->setMatrix(tgfx::Matrix::MakeTrans(glassPosX, glassPosY));
  auto style = tgfx::GlassStyle::Make(glassRefraction, glassDepth, 0, 0, 0, glassLightAngle, 100);
  glassStyleRef = style;
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

void LiquidGlass::setDepth(float depth) {
  glassDepth = std::clamp(depth, 0.0f, 100.0f);
  if (glassStyleRef) {
    glassStyleRef->setDepth(glassDepth);
  }
}

void LiquidGlass::setRefraction(float refraction) {
  glassRefraction = std::clamp(refraction, 0.0f, 100.0f);
  if (glassStyleRef) {
    glassStyleRef->setRefraction(glassRefraction);
  }
}

void LiquidGlass::setLightAngle(float angle) {
  glassLightAngle = std::fmod(angle, 360.0f);
  if (glassLightAngle < 0.0f) {
    glassLightAngle += 360.0f;
  }
  if (glassStyleRef) {
    glassStyleRef->setLightAngle(glassLightAngle);
  }
}

}  // namespace hello2d
