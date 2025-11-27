/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/Paint.h"

namespace tgfx {
void Paint::setShader(std::shared_ptr<Shader> newShader) {
  brush.shader = std::move(newShader);
  if (brush.shader) {
    Color color = {};
    if (brush.shader->asColor(&color)) {
      color.alpha *= brush.color.alpha;
      brush.color = color;
      brush.shader = nullptr;
    }
  }
}

void Paint::reset() {
  stroke = Stroke(0);
  brush = {};
  imageFilter = nullptr;
  style = PaintStyle::Fill;
}
}  // namespace tgfx