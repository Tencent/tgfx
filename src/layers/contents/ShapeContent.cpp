/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "ShapeContent.h"

namespace tgfx {
ShapeContent::ShapeContent(std::shared_ptr<Shape> fill, std::shared_ptr<Shape> stroke,
                           std::vector<ShapePaint> paintList, size_t fillPaintCount)
    : fillShape(std::move(fill)), strokeShape(std::move(stroke)), paintList(std::move(paintList)),
      fillPaintCount(fillPaintCount) {
  if (fillShape) {
    bounds = fillShape->getBounds();
  }
  if (strokeShape) {
    bounds.join(strokeShape->getBounds());
  }
}
void ShapeContent::draw(Canvas* canvas, const Paint& paint) const {
  size_t index = 0;
  for (auto& shapePaint : paintList) {
    auto shape = index++ < fillPaintCount ? fillShape : strokeShape;
    drawShape(canvas, paint, std::move(shape), shapePaint);
  }
}

void ShapeContent::drawShape(Canvas* canvas, const Paint& paint, std::shared_ptr<Shape> shape,
                             const ShapePaint& shapePaint) const {
  auto drawPaint = paint;
  drawPaint.setAlpha(paint.getAlpha() * shapePaint.alpha);
  // The blend mode in the paint is always SrcOver, use our own blend mode instead.
  drawPaint.setBlendMode(shapePaint.blendMode);
  drawPaint.setShader(shapePaint.shader);
  canvas->drawShape(std::move(shape), drawPaint);
}

void ShapeContent::drawContour(Canvas* canvas, const Paint& paint) const {
  if (fillShape != nullptr) {
    drawShapeContour(canvas, paint, fillShape, paintList.begin(),
                     paintList.begin() + static_cast<std::ptrdiff_t>(fillPaintCount));
  }
  if (strokeShape != nullptr) {
    drawShapeContour(canvas, paint, strokeShape,
                     paintList.begin() + static_cast<std::ptrdiff_t>(fillPaintCount),
                     paintList.end());
  }
}

void ShapeContent::drawShapeContour(Canvas* canvas, const Paint& paint,
                                    std::shared_ptr<Shape> shape,
                                    std::vector<ShapePaint>::const_iterator begin,
                                    std::vector<ShapePaint>::const_iterator end) const {
  auto allShadersAreImages = std::none_of(
      begin, end, [](const auto& shapePaint) { return !shapePaint.shader->isAImage(); });
  if (allShadersAreImages) {
    for (auto it = begin; it != end; it++) {
      drawShape(canvas, paint, shape, *it);
    }
  } else {
    canvas->drawShape(shape, paint);
  }
}

bool ShapeContent::hitTestPoint(float localX, float localY, bool pixelHitTest) {
  if (!pixelHitTest) {
    return bounds.contains(localX, localY);
  }
  if (fillShape != nullptr) {
    auto path = fillShape->getPath();
    if (path.contains(localX, localY)) {
      return true;
    }
  }
  if (strokeShape != nullptr) {
    auto path = strokeShape->getPath();
    if (path.contains(localX, localY)) {
      return true;
    }
  }
  return false;
}
}  // namespace tgfx
