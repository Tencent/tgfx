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

Rect ShapeContent::getTightBounds(const Matrix& matrix) const {
  Rect tightBounds = {};
  if (fillShape) {
    auto path = fillShape->getPath();
    path.transform(matrix);
    tightBounds = path.getBounds();
  }
  if (strokeShape) {
    auto path = strokeShape->getPath();
    path.transform(matrix);
    tightBounds.join(path.getBounds());
  }
  return tightBounds;
}

void ShapeContent::draw(Canvas* canvas, const Paint& paint) const {
  drawFills(canvas, paint, false);
  drawStrokes(canvas, paint, false);
}

static void DrawShape(Canvas* canvas, const Paint& paint, std::shared_ptr<Shape> shape,
                      bool forContour, const std::vector<ShapePaint>::const_iterator& begin,
                      const std::vector<ShapePaint>::const_iterator& end) {
  if (forContour) {
    auto hasNonImageShader = std::any_of(
        begin, end, [](const auto& shapePaint) { return !shapePaint.shader->isAImage(); });
    if (hasNonImageShader) {
      canvas->drawShape(std::move(shape), paint);
      return;
    }
  }
  for (auto iter = begin; iter != end; iter++) {
    auto drawPaint = paint;
    drawPaint.setAlpha(paint.getAlpha() * iter->alpha);
    // The blend mode in the paint is always SrcOver, use our own blend mode instead.
    drawPaint.setBlendMode(iter->blendMode);
    drawPaint.setShader(iter->shader);
    canvas->drawShape(shape, drawPaint);
  }
}

bool ShapeContent::drawFills(Canvas* canvas, const Paint& paint, bool forContour) const {
  if (!fillShape) {
    return false;
  }
  DrawShape(canvas, paint, fillShape, forContour, paintList.begin(),
            paintList.begin() + static_cast<std::ptrdiff_t>(fillPaintCount));
  return true;
}

bool ShapeContent::drawStrokes(Canvas* canvas, const Paint& paint, bool forContour) const {
  if (!strokeShape) {
    return false;
  }
  DrawShape(canvas, paint, strokeShape, forContour,
            paintList.begin() + static_cast<std::ptrdiff_t>(fillPaintCount), paintList.end());
  return true;
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
