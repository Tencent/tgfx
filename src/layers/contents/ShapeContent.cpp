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

#include "ShapeContent.h"
#include "core/utils/StrokeUtils.h"

namespace tgfx {

ShapeContent::ShapeContent(std::shared_ptr<Shape> shape, const LayerPaint& paint)
    : DrawContent(paint), shape(std::move(shape)) {
}

Rect ShapeContent::onGetBounds() const {
  return shape->getBounds();
}

Rect ShapeContent::getBounds() const {
  auto result = onGetBounds();
  if (stroke) {
    // Shape may contain sharp corners, so we need to apply miter limit to the bounds.
    ApplyStrokeToBounds(*stroke, &result, Matrix::I(), true);
  }
  return result;
}

Path ShapeContent::getFilledPath() const {
  auto path = shape->getPath();
  if (stroke) {
    stroke->applyToPath(&path);
  }
  return path;
}

Rect ShapeContent::getTightBounds(const Matrix& matrix) const {
  auto strokedPath = getFilledPath();
  strokedPath.transform(matrix);
  return strokedPath.getBounds();
}

bool ShapeContent::hitTestPoint(float localX, float localY) const {
  if (color.alpha <= 0) {
    return false;
  }
  return getFilledPath().contains(localX, localY);
}

void ShapeContent::onDraw(Canvas* canvas, const Paint& paint) const {
  canvas->drawShape(shape, paint);
}

bool ShapeContent::onHasSameGeometry(const GeometryContent* other) const {
  return shape == static_cast<const ShapeContent*>(other)->shape;
}

}  // namespace tgfx
