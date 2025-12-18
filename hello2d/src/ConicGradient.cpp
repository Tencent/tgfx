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
#include "tgfx/layers/ShapeLayer.h"

namespace hello2d {
std::shared_ptr<tgfx::Layer> ConicGradient::onBuildLayerTree(const AppHost*) {
  auto root = tgfx::Layer::Make();
  auto shapeLayer = tgfx::ShapeLayer::Make();

  // Fixed size: 720x720 with 50px padding, content area: 620x620
  constexpr auto size = 620;

  tgfx::Color cyan = {0.0f, 1.0f, 1.0f, 1.0f};
  tgfx::Color magenta = {1.0f, 0.0f, 1.0f, 1.0f};
  tgfx::Color yellow = {1.0f, 1.0f, 0.0f, 1.0f};

  auto conicGradient = tgfx::Gradient::MakeConic(tgfx::Point::Make(size / 2, size / 2), 0, 360,
                                                 {cyan, magenta, yellow, cyan}, {});
  auto rect = tgfx::Rect::MakeXYWH(0, 0, size, size);

  tgfx::Path path = {};
  path.addRoundRect(rect, 20, 20);
  shapeLayer->setPath(path);
  shapeLayer->setFillStyle(conicGradient);

  root->addChild(shapeLayer);
  return root;
}

}  // namespace hello2d
