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
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"

namespace hello2d {

// Creates a rounded rectangle path centered at the origin.
static tgfx::Path MakeRoundRectPath(float width, float height, float radius) {
  tgfx::Path path = {};
  path.addRoundRect(tgfx::Rect::MakeXYWH(-width / 2, -height / 2, width, height), radius, radius);
  return path;
}

std::shared_ptr<tgfx::Layer> ShapeInstanced::onBuildLayerTree(const AppHost*) {
  auto root = tgfx::Layer::Make();

  // Create shape directly from path. Shape::MakeFrom(Path) creates a PathShape which has
  // isSimplePath()=true, so Canvas::drawShape will inline it as a plain path draw instead of
  // going through the ShapeInstancedDrawOp batching path.
  constexpr float rectWidth = 180;
  constexpr float rectHeight = 180;
  constexpr float cornerRadius = 20;
  constexpr float gap = 30;
  auto roundRectPath = MakeRoundRectPath(rectWidth, rectHeight, cornerRadius);
  auto roundRectShape = tgfx::Shape::MakeFrom(std::move(roundRectPath));

  tgfx::Color colors[] = {
      tgfx::Color::FromRGBA(255, 80, 80),   // red
      tgfx::Color::FromRGBA(80, 180, 255),  // blue
      tgfx::Color::FromRGBA(80, 220, 120),  // green
      tgfx::Color::FromRGBA(255, 200, 60),  // yellow
  };

  // 2x2 grid layout. RoundRect path bounds: [-90, -90, 90, 90], size 180x180.
  // Centers placed so overall bounds start at (0, 0): center = halfSize, second = halfSize+size+gap.
  auto halfWidth = rectWidth / 2;
  auto halfHeight = rectHeight / 2;
  tgfx::Point positions[] = {
      {halfWidth, halfHeight},
      {halfWidth + rectWidth + gap, halfHeight},
      {halfWidth, halfHeight + rectHeight + gap},
      {halfWidth + rectWidth + gap, halfHeight + rectHeight + gap},
  };

  for (int i = 0; i < 4; i++) {
    auto layer = tgfx::ShapeLayer::Make();
    layer->setShape(roundRectShape);
    layer->setFillStyle(tgfx::ShapeStyle::Make(colors[i]));
    layer->setMatrix(tgfx::Matrix::MakeTrans(positions[i].x, positions[i].y));
    root->addChild(layer);
  }

  return root;
}

}  // namespace hello2d
