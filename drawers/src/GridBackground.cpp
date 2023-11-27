/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "base/Drawers.h"

namespace drawers {
void GridBackground::onDraw(tgfx::Canvas* canvas, const drawers::AppHost* host) const {
  canvas->clear(tgfx::Color::White());
  tgfx::Paint paint;
  paint.setColor(tgfx::Color{0.8f, 0.8f, 0.8f, 1.f});
  int tileSize = 8 * static_cast<int>(host->density());
  auto width = host->width();
  auto height = host->height();
  for (int y = 0; y < height; y += tileSize) {
    bool draw = (y / tileSize) % 2 == 1;
    for (int x = 0; x < width; x += tileSize) {
      if (draw) {
        auto rect =
            tgfx::Rect::MakeXYWH(static_cast<float>(x), static_cast<float>(y),
                                 static_cast<float>(tileSize), static_cast<float>(tileSize));
        canvas->drawRect(rect, paint);
      }
      draw = !draw;
    }
  }
}
}  // namespace drawers
