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
#include "tgfx/core/PathProvider.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"

namespace hello2d {

// Creates a 5-pointed star path centered at the origin.
static tgfx::Path MakeStarPath(float radius) {
  tgfx::Path path = {};
  auto innerRadius = radius * 0.382f;
  constexpr float startAngle = -90.0f;
  for (int i = 0; i < 10; i++) {
    auto angle = startAngle + static_cast<float>(i) * 36.0f;
    auto radians = angle * static_cast<float>(M_PI) / 180.0f;
    auto r = (i % 2 == 0) ? radius : innerRadius;
    auto x = r * cosf(radians);
    auto y = r * sinf(radians);
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

  // Use PathProvider to wrap the path so that Shape::MakeFrom(PathProvider) creates a
  // ProviderShape instead of a PathShape. ProviderShape has isSimplePath()=false, which
  // prevents Canvas::drawShape from inlining it as a plain path draw. This allows
  // OpsCompositor::drawShape to batch multiple draws of the same shape (same uniqueKey)
  // with different translations into a single ShapeInstancedDrawOp.
  auto starPath = MakeStarPath(100);
  auto pathProvider = tgfx::PathProvider::Wrap(std::move(starPath));
  auto starShape = tgfx::Shape::MakeFrom(std::move(pathProvider));

  tgfx::Color colors[] = {
      tgfx::Color::FromRGBA(255, 80, 80),   // red
      tgfx::Color::FromRGBA(80, 180, 255),  // blue
      tgfx::Color::FromRGBA(80, 220, 120),  // green
      tgfx::Color::FromRGBA(255, 200, 60),  // yellow
  };

  // 2x2 grid layout. Star path bounds: [-95, -100, 95, 81], size roughly 190x181.
  // Place star centers so the overall bounds start near (0, 0) with ~30px gap between stars.
  tgfx::Point positions[] = {
      {96, 100},
      {316, 100},
      {96, 311},
      {316, 311},
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
