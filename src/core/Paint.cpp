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

#include "tgfx/core/Paint.h"

namespace tgfx {
void Paint::setShader(std::shared_ptr<Shader> newShader) {
  fill.shader = std::move(newShader);
  if (fill.shader) {
    Color color = {};
    if (fill.shader->asColor(&color)) {
      color.alpha *= getAlpha();
      fill.color = color;
      fill.shader = nullptr;
    }
  }
}

static bool AffectsAlpha(const ColorFilter* cf) {
  return cf && !cf->isAlphaUnchanged();
}

bool Paint::nothingToDraw() const {
  switch (fill.blendMode) {
    case BlendMode::SrcOver:
    case BlendMode::SrcATop:
    case BlendMode::DstOut:
    case BlendMode::DstOver:
    case BlendMode::PlusLighter:
      if (fill.color.alpha == 0) {
        return !AffectsAlpha(fill.colorFilter.get()) && imageFilter == nullptr;
      }
      break;
    case BlendMode::Dst:
      return true;
    default:
      break;
  }
  if (PaintStyle::Stroke == style && stroke.width <= 0.0f) {
    return true;
  }
  return false;
}

void Paint::reset() {
  stroke = Stroke(0);
  fill = {};
  imageFilter = nullptr;
  style = PaintStyle::Fill;
}
}  // namespace tgfx