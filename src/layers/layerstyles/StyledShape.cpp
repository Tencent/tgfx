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

StyledShape StyledShape::Make(std::shared_ptr<Shape> shape, bool hasFill, bool hasStroke,
                              float strokeWidth, StrokeAlign strokeAlign) {
  StyledShape result = {};
  result.shape = std::move(shape);
  if (hasFill && hasStroke) {
    result.type = StyledShapeType::FillStroke;
    result.strokeWidth = strokeWidth;
    result.strokeAlign = strokeAlign;
  } else if (hasFill) {
    result.type = StyledShapeType::Fill;
  } else {
    result.type = StyledShapeType::Stroke;
    result.strokeWidth = strokeWidth;
    result.strokeAlign = strokeAlign;
  }
  return result;
}

}  // namespace tgfx
