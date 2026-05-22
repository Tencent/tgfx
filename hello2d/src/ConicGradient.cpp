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
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"

namespace hello2d {
std::shared_ptr<tgfx::Layer> ConicGradient::onBuildLayerTree(const AppHost*) {
  auto root = tgfx::Layer::Make();
  auto shapeLayer = tgfx::ShapeLayer::Make();

  // Build a closed shape with a flat bottom: the upper outline (right -> top -> left) is formed
  // by two cubic (3rd-order) Bezier segments, while the lower outline (left -> bottom-left ->
  // bottom-right -> right) is composed of straight line segments. The shape is positioned so
  // that its bounding box starts at the origin, allowing the framework to center it correctly.
  constexpr float Radius = 250;
  constexpr float Bulge = 100;
  constexpr float CenterX = Radius;
  constexpr float CenterY = Radius;

  tgfx::Path path = {};
  path.moveTo(CenterX + Radius, CenterY);
  // Right -> Top
  path.cubicTo(CenterX + Radius, CenterY - Bulge, CenterX + Bulge, CenterY - Radius, CenterX,
               CenterY - Radius);
  // Top -> Left
  path.cubicTo(CenterX - Bulge, CenterY - Radius, CenterX - Radius, CenterY - Bulge,
               CenterX - Radius, CenterY);
  // Left -> bottom-left -> bottom-right -> Right (straight segments).
  path.lineTo(CenterX - Radius, CenterY + Radius);
  path.lineTo(CenterX + Radius, CenterY + Radius);
  path.lineTo(CenterX + Radius, CenterY);
  path.close();

  shapeLayer->setPath(path);
  shapeLayer->setFillStyle(tgfx::ShapeStyle::Make(tgfx::Color{1.0f, 0.0f, 0.0f, 1.0f}));

  root->addChild(shapeLayer);
  return root;
}

}  // namespace hello2d
