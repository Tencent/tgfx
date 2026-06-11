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

#include "tgfx/layers/layerstyles/StyledShape.h"

namespace tgfx {

static inline float StrokeOutset(float strokeWidth, StrokeAlign align) {
  switch (align) {
    case StrokeAlign::Center:
      return strokeWidth * 0.5f;
    case StrokeAlign::Outside:
      return strokeWidth;
    case StrokeAlign::Inside:
      return 0.0f;
  }
  return 0.0f;
}

StyledShape StyledShape::Make(std::shared_ptr<Shape> shape, bool hasFill, bool hasStroke,
                              float strokeWidth, StrokeAlign strokeAlign) {
  StyledShape result = {};
  result.shape = std::move(shape);
  if (hasFill) {
    result.style = PaintStyle::Fill;
    if (hasStroke) {
      result.fillOutset = StrokeOutset(strokeWidth, strokeAlign);
    }
  } else {
    result.style = PaintStyle::Stroke;
    result.strokeWidth = strokeWidth;
    result.strokeAlign = strokeAlign;
  }
  return result;
}

}  // namespace tgfx
