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

#include "GridBackground.h"
#include "tgfx/layers/LayerRecorder.h"

namespace hello2d {

std::shared_ptr<GridBackgroundLayer> GridBackgroundLayer::Make(int width, int height,
                                                               float density) {
  return std::shared_ptr<GridBackgroundLayer>(new GridBackgroundLayer(width, height, density));
}

GridBackgroundLayer::GridBackgroundLayer(int width, int height, float density)
    : _width(width), _height(height), _density(density) {
  invalidateContent();
}

void GridBackgroundLayer::onUpdateContent(tgfx::LayerRecorder* recorder) {
  tgfx::LayerPaint backgroundPaint(tgfx::Color::White());
  recorder->addRect(tgfx::Rect::MakeWH(static_cast<float>(_width), static_cast<float>(_height)),
                    backgroundPaint);

  tgfx::LayerPaint tilePaint(tgfx::Color{0.8f, 0.8f, 0.8f, 1.f});
  int tileSize = 8 * static_cast<int>(_density);
  if (tileSize <= 0) {
    tileSize = 8;
  }
  for (int y = 0; y < _height; y += tileSize) {
    bool draw = (y / tileSize) % 2 == 1;
    for (int x = 0; x < _width; x += tileSize) {
      if (draw) {
        recorder->addRect(
            tgfx::Rect::MakeXYWH(static_cast<float>(x), static_cast<float>(y),
                                 static_cast<float>(tileSize), static_cast<float>(tileSize)),
            tilePaint);
      }
      draw = !draw;
    }
  }
}

}  // namespace hello2d
