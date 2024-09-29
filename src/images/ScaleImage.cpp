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

#include "ScaleImage.h"
#include "gpu/ops/DrawOp.h"

namespace tgfx {

std::shared_ptr<Image> ScaleImage::MakeFrom(std::shared_ptr<Image> source, Orientation orientation,
                                            const Rect& bounds, const Point& scale) {
  if (scale.x <= 0 || scale.y <= 0 || source == nullptr) {
    return nullptr;
  }
  if (scale.x == 1.0f && scale.y == 1.0f) {
    return source;
  }
  auto result = std::shared_ptr<ScaleImage>(
      new ScaleImage(std::move(source), orientation, bounds, scale.x, scale.y));
  result->weakThis = result;
  return result;
}

static int GetScaledSize(int size, float scale) {
  return static_cast<int>(roundf(static_cast<float>(size) * scale));
}

int ScaleImage::width() const {
  return GetScaledSize(SubsetImage::width(), scaleX);
}

int ScaleImage::height() const {
  return GetScaledSize(SubsetImage::height(), scaleY);
}

std::shared_ptr<Image> ScaleImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  return MakeFrom(std::move(newSource), orientation, bounds, Point::Make(scaleX, scaleY));
}

std::shared_ptr<Image> ScaleImage::onMakeOriented(Orientation orientation) const {
  auto newOrientation = concatOrientation(orientation);
  auto orientedWidth = OrientImage::width();
  auto orientedHeight = OrientImage::height();
  auto matrix = OrientationToMatrix(orientation, orientedWidth, orientedHeight);
  auto newBounds = matrix.mapRect(bounds);
  return MakeFrom(source, newOrientation, newBounds, Point::Make(scaleX, scaleY));
}

std::shared_ptr<Image> ScaleImage::onMakeSubset(const Rect& subset) const {
  auto newBounds = subset;
  newBounds.scale(1 / scaleX, 1 / scaleY);
  newBounds = newBounds.makeOffset(bounds.x(), bounds.y());
  return MakeFrom(source, orientation, newBounds, Point::Make(scaleX, scaleY));
}

std::shared_ptr<Image> ScaleImage::onMakeScaled(float newScaleX, float newScaleY) const {
  auto newBounds = bounds;
  newBounds.scale(newScaleX, newScaleY);
  return MakeFrom(source, orientation, bounds, Point::Make(scaleX * newScaleX, scaleY * newScaleY));
}

std::optional<Matrix> ScaleImage::concatUVMatrix(const Matrix* uvMatrix) const {
  Matrix matrix = Matrix::MakeScale(1 / scaleX, 1 / scaleY);
  if (uvMatrix) {
    matrix.preConcat(*uvMatrix);
  }
  return SubsetImage::concatUVMatrix(&matrix);
}

ScaleImage::ScaleImage(std::shared_ptr<Image> source, Orientation orientation, const Rect& bounds,
                       float scaleX, float scaleY)
    : SubsetImage(std::move(source), orientation, bounds), scaleX(scaleX), scaleY(scaleY) {
}

}  // namespace tgfx