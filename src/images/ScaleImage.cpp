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
#include "images/SubsetImage.h"

namespace tgfx {

std::shared_ptr<Image> ScaleImage::MakeFrom(std::shared_ptr<Image> source, Orientation orientation,
                                            const Point& scale) {
  if (scale.x <= 0 || scale.y <= 0 || source == nullptr) {
    return nullptr;
  }
  if (scale.x == 1.0f && scale.y == 1.0f) {
    return OrientImage::MakeFrom(std::move(source), orientation);
  }
  auto result = std::shared_ptr<ScaleImage>(new ScaleImage(std::move(source), orientation, scale));
  result->weakThis = result;
  return result;
}

ScaleImage::ScaleImage(std::shared_ptr<Image> source, Orientation orientation, const Point& scale)
    : OrientImage(std::move(source), orientation), scale(scale) {
}

static int GetScaledSize(int size, float scale) {
  return static_cast<int>(roundf(static_cast<float>(size) * scale));
}

int ScaleImage::width() const {
  return GetScaledSize(OrientImage::width(), scale.x);
}

int ScaleImage::height() const {
  return GetScaledSize(OrientImage::height(), scale.y);
}

std::shared_ptr<Image> ScaleImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  return MakeFrom(std::move(newSource), orientation, scale);
}

std::shared_ptr<Image> ScaleImage::onMakeOriented(Orientation orientation) const {
  auto newOrientation = concatOrientation(orientation);
  return MakeFrom(source, newOrientation, scale);
}

std::shared_ptr<Image> ScaleImage::onMakeSubset(const Rect& subset) const {
  return SubsetImage::MakeFrom(source, orientation, scale, subset);
}

std::shared_ptr<Image> ScaleImage::onMakeScaled(float newScaleX, float newScaleY) const {
  return MakeFrom(source, orientation, Point::Make(scale.x * newScaleX, scale.y * newScaleY));
}

std::optional<Matrix> ScaleImage::concatUVMatrix(const Matrix* uvMatrix) const {
  Matrix matrix = Matrix::MakeScale(1 / scale.x, 1 / scale.y);
  if (uvMatrix) {
    matrix.preConcat(*uvMatrix);
  }
  return OrientImage::concatUVMatrix(&matrix);
}

}  // namespace tgfx