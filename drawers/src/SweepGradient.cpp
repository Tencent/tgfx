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
void SweepGradient::onDraw(tgfx::Canvas* canvas, const drawers::AppHost* host) const {
  auto scale = host->density();
  auto width = host->width();
  auto height = host->height();
  tgfx::Color cyan = {0.0f, 1.0f, 1.0f, 1.0f};
  tgfx::Color magenta = {1.0f, 0.0f, 1.0f, 1.0f};
  tgfx::Color yellow = {1.0f, 1.0f, 0.0f, 1.0f};
  auto shader = tgfx::Shader::MakeSweepGradient(tgfx::Point::Make(width / 2, height / 2), 0, 360,
                                                {cyan, magenta, yellow, cyan}, {});
  tgfx::Paint paint = {};
  paint.setShader(shader);
  auto screenSize = std::min(width, height);
  auto size = screenSize - static_cast<int>(150 * scale);
  size = std::max(size, 50);
  auto rect = tgfx::Rect::MakeXYWH((width - size) / 2, (height - size) / 2, size, size);
  tgfx::Path path = {};
  path.addRoundRect(rect, 20 * scale, 20 * scale);
  canvas->drawPath(path, paint);
}

}  // namespace drawers
