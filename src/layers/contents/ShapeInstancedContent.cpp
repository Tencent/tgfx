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

#include "ShapeInstancedContent.h"
#include "core/utils/StrokeUtils.h"

namespace tgfx {

ShapeInstancedContent::ShapeInstancedContent(std::shared_ptr<Shape> shape,
                                             std::vector<Matrix> matrices,
                                             std::vector<Color> colors, const LayerPaint& paint)
    : DrawContent(paint), shape(std::move(shape)), matrices(std::move(matrices)),
      instanceColors(std::move(colors)) {
}

Rect ShapeInstancedContent::getBounds() const {
  auto bounds = onGetBounds();
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &bounds, Matrix::I(), true);
  }
  return bounds;
}

Rect ShapeInstancedContent::getTightBounds(const Matrix& matrix) const {
  auto filledPath = getFilledPath();
  Rect result = {};
  for (const auto& instanceMatrix : matrices) {
    auto path = filledPath;
    auto combined = matrix * instanceMatrix;
    path.transform(combined);
    result.join(path.getBounds());
  }
  return result;
}

bool ShapeInstancedContent::hitTestPoint(float localX, float localY) const {
  if (color.alpha <= 0.0f) {
    return false;
  }
  auto filledPath = getFilledPath();
  for (const auto& instanceMatrix : matrices) {
    Matrix inverse = {};
    if (!instanceMatrix.invert(&inverse)) {
      continue;
    }
    auto localPoint = inverse.mapXY(localX, localY);
    if (filledPath.contains(localPoint.x, localPoint.y)) {
      return true;
    }
  }
  return false;
}

Rect ShapeInstancedContent::onGetBounds() const {
  auto shapeBounds = shape->getBounds();
  Rect result = {};
  for (const auto& instanceMatrix : matrices) {
    result.join(instanceMatrix.mapRect(shapeBounds));
  }
  return result;
}

void ShapeInstancedContent::onDraw(Canvas* canvas, const Paint& paint) const {
  canvas->drawShapeInstanced(shape, matrices.data(),
                             instanceColors.empty() ? nullptr : instanceColors.data(),
                             matrices.size(), paint);
}

bool ShapeInstancedContent::onHasSameGeometry(const GeometryContent* other) const {
  auto otherContent = static_cast<const ShapeInstancedContent*>(other);
  return shape == otherContent->shape && matrices == otherContent->matrices;
}

Path ShapeInstancedContent::getFilledPath() const {
  auto path = shape->getPath();
  if (stroke) {
    stroke->applyToPath(&path);
  }
  return path;
}

}  // namespace tgfx
