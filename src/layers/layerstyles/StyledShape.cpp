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
#include "layers/SpreadUtils.h"

namespace tgfx {

StyledShape StyledShape::Make(std::shared_ptr<Shape> shape, StyledShapeType type, float strokeWidth,
                              StrokeAlign strokeAlign) {
  StyledShape result = {};
  result.shape = std::move(shape);
  result.type = type;
  switch (type) {
    case StyledShapeType::Fill:
      break;
    case StyledShapeType::Stroke:
    case StyledShapeType::FillStroke:
      result.strokeWidth = strokeWidth;
      result.strokeAlign = strokeAlign;
      break;
  }
  return result;
}

Rect StyledShape::getBounds() const {
  auto [innerShape, matrix] = SpreadUtils::UnwrapMatrixShape(shape);
  auto bounds = innerShape->getPath().getBounds();
  if (type == StyledShapeType::Stroke || type == StyledShapeType::FillStroke) {
    const auto outset = SpreadUtils::StrokeOutset(strokeWidth, strokeAlign);
    bounds.outset(outset, outset);
  }
  return matrix.mapRect(bounds);
}

}  // namespace tgfx
