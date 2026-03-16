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

static tgfx::Path MakeStarPath(float radius) {
  tgfx::Path path = {};
  // 5-pointed star: alternate between outer radius and inner radius
  auto innerRadius = radius * 0.382f;
  constexpr float startAngle = -90.0f;
  for (int i = 0; i < 10; i++) {
    auto angle = startAngle + i * 36.0f;
    auto radians = angle * M_PI / 180.0f;
    auto r = (i % 2 == 0) ? radius : innerRadius;
    auto x = r * cosf(static_cast<float>(radians));
    auto y = r * sinf(static_cast<float>(radians));
    if (i == 0) {
      path.moveTo(x, y);
    } else {
      path.lineTo(x, y);
    }
  }
  path.close();
  return path;
}

std::shared_ptr<tgfx::Layer> ShapeInstanced::onBuildLayerTree(const AppHost*) {
  auto root = tgfx::Layer::Make();

  // Create a single shared star shape so all four ShapeLayers share the same uniqueKey,
  // which triggers instanced drawing when only translation differs.
  auto starPath = MakeStarPath(100);
  auto starShape = tgfx::Shape::MakeFrom(starPath);

  // Four colors for the four stars
  tgfx::Color colors[] = {
      tgfx::Color::FromRGBA(255, 80, 80),   // red
      tgfx::Color::FromRGBA(80, 180, 255),  // blue
      tgfx::Color::FromRGBA(80, 220, 120),  // green
      tgfx::Color::FromRGBA(255, 200, 60),  // yellow
  };

  // Place four stars in a 2x2 grid, centered in a 620x620 area
  tgfx::Point positions[] = {
      {155, 155},
      {465, 155},
      {155, 465},
      {465, 465},
  };

  for (int i = 0; i < 4; i++) {
    auto layer = tgfx::ShapeLayer::Make();
    layer->setShape(starShape);
    layer->setFillStyle(tgfx::ShapeStyle::Make(colors[i]));
    layer->setMatrix(tgfx::Matrix::MakeTrans(positions[i].x, positions[i].y));
    root->addChild(layer);
  }

  return root;
}

}  // namespace hello2d
