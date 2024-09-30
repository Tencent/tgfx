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

#include "SubsetImage.h"
#include "utils/UVMatrix.h"

namespace tgfx {
std::shared_ptr<Image> SubsetImage::MakeFrom(std::shared_ptr<Image> source, Orientation orientation,
                                             const Point& scale, const Rect& bounds) {
  if (source == nullptr || bounds.isEmpty()) {
    return nullptr;
  }
  auto width = static_cast<int>(static_cast<float>(source->width()) * scale.x);
  auto height = static_cast<int>(static_cast<float>(source->height()) * scale.y);
  if (OrientationSwapsWidthHeight(orientation)) {
    std::swap(width, height);
  }
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  auto sourceBounds = Rect::MakeWH(width, height);
  if (sourceBounds == bounds) {
    return ScaleImage::MakeFrom(source, orientation, scale);
  }
  auto image =
      std::shared_ptr<SubsetImage>(new SubsetImage(std::move(source), orientation, scale, bounds));
  image->weakThis = image;
  return image;
}

SubsetImage::SubsetImage(std::shared_ptr<Image> source, Orientation orientation, const Point& scale,
                         const Rect& bounds)
    : ScaleImage(std::move(source), orientation, scale), bounds(bounds) {
}

std::shared_ptr<Image> SubsetImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  return SubsetImage::MakeFrom(std::move(newSource), orientation, scale, bounds);
}

std::shared_ptr<Image> SubsetImage::onMakeSubset(const Rect& subset) const {
  auto newBounds = subset.makeOffset(bounds.x(), bounds.y());
  return SubsetImage::MakeFrom(source, orientation, scale, newBounds);
}

std::shared_ptr<Image> SubsetImage::onMakeOriented(Orientation orientation) const {
  auto newOrientation = concatOrientation(orientation);
  auto orientedWidth = ScaleImage::width();
  auto orientedHeight = ScaleImage::height();
  auto matrix = OrientationToMatrix(orientation, orientedWidth, orientedHeight);
  auto newBounds = matrix.mapRect(bounds);
  return SubsetImage::MakeFrom(source, newOrientation, scale, newBounds);
}

std::shared_ptr<Image> SubsetImage::onMakeScaled(float scaleX, float scaleY) const {
  auto newBounds = bounds;
  newBounds.scale(scaleX, scaleY);
  return SubsetImage::MakeFrom(source, orientation, Point::Make(scaleX, scaleY), newBounds);
}

std::optional<Matrix> SubsetImage::concatUVMatrix(const Matrix* uvMatrix) const {
  auto matrix = UVMatrix::Concat(bounds, uvMatrix);
  return ScaleImage::concatUVMatrix(AddressOf(matrix));
}
}  // namespace tgfx
