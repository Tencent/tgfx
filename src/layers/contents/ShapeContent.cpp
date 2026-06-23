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
#include "tgfx/core/Canvas.h"

namespace tgfx {

ShapeContent::ShapeContent(std::shared_ptr<Shape> shape, const LayerPaint& paint)
    : DrawContent(paint), shape(std::move(shape)) {
}

ShapeContent::ShapeContent(std::shared_ptr<Shape> shape, std::shared_ptr<Shape> clipShape,
                           const LayerPaint& paint)
    : DrawContent(paint), shape(std::move(shape)), clipShape(std::move(clipShape)) {
}

Rect ShapeContent::getBounds() const {
  auto bounds = onGetBounds();
  if (stroke) {
    // Shape may contain sharp corners, so we need to apply miter limit to the bounds.
    ApplyStrokeToBounds(*stroke, &bounds, Matrix::I(), true);
  }
  // Inverse-fill clips (Outside-stroke) clip to the area outside the path; the stroke's own
  // bounds are already the correct visible extent in that case, so skip the intersect.
  if (clipShape && !clipShape->isInverseFillType()) {
    auto clipBounds = clipShape->getBounds();
    if (!bounds.intersect(clipBounds)) {
      bounds.setEmpty();
    }
  }
  return bounds;
}

Rect ShapeContent::getTightBounds(const Matrix& matrix) const {
  auto strokedPath = getFilledPath();
  strokedPath.transform(matrix);
  auto bounds = strokedPath.getBounds();
  if (clipShape && !clipShape->isInverseFillType()) {
    auto clipBounds = clipShape->getPath().getBounds();
    matrix.mapRect(&clipBounds);
    if (!bounds.intersect(clipBounds)) {
      bounds.setEmpty();
    }
  }
  return bounds;
}

bool ShapeContent::hitTestPoint(float localX, float localY) const {
  if (color.alpha <= 0) {
    return false;
  }
  if (!getFilledPath().contains(localX, localY)) {
    return false;
  }
  if (clipShape) {
    return clipShape->getPath().contains(localX, localY);
  }
  return true;
}

Rect ShapeContent::onGetBounds() const {
  return shape->getBounds();
}

void ShapeContent::onDraw(Canvas* canvas, const Paint& paint) const {
  if (clipShape) {
    AutoCanvasRestore autoRestore(canvas);
    canvas->clipPath(clipShape->getPath(), paint.isAntiAlias());
    canvas->drawShape(shape, paint);
    return;
  }
  canvas->drawShape(shape, paint);
}

bool ShapeContent::onHasSameGeometry(const GeometryContent* other) const {
  const auto* otherShape = static_cast<const ShapeContent*>(other);
  if (shape != otherShape->shape) {
    return false;
  }
  return clipShape == otherShape->clipShape;
}

Path ShapeContent::getFilledPath() const {
  auto path = shape->getPath();
  if (stroke) {
    stroke->applyToPath(&path);
  }
  return path;
}

}  // namespace tgfx
