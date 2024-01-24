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

namespace tgfx {
std::shared_ptr<Image> SubsetImage::MakeFrom(std::shared_ptr<Image> source, EncodedOrigin origin,
                                             const Rect& bounds) {
  if (source == nullptr || bounds.isEmpty()) {
    return nullptr;
  }
  auto image = std::shared_ptr<SubsetImage>(new SubsetImage(std::move(source), origin, bounds));
  image->weakThis = image;
  return image;
}

SubsetImage::SubsetImage(std::shared_ptr<Image> source, EncodedOrigin origin, const Rect& bounds)
    : OrientedImage(std::move(source), origin), bounds(bounds) {
}

std::shared_ptr<Image> SubsetImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  return SubsetImage::MakeFrom(std::move(newSource), origin, bounds);
}

std::shared_ptr<Image> SubsetImage::onMakeSubset(const Rect& subset) const {
  auto newBounds = subset;
  newBounds.offset(bounds.x(), bounds.y());
  return SubsetImage::MakeFrom(source, origin, newBounds);
}

std::shared_ptr<Image> SubsetImage::onApplyOrigin(EncodedOrigin encodedOrigin) const {
  auto newOrigin = concatOrigin(encodedOrigin);
  auto orientedWidth = OrientedImage::width();
  auto orientedHeight = OrientedImage::height();
  auto matrix = EncodedOriginToMatrix(encodedOrigin, orientedWidth, orientedHeight);
  auto newBounds = matrix.mapRect(bounds);
  return SubsetImage::MakeFrom(source, newOrigin, newBounds);
}

Matrix SubsetImage::computeLocalMatrix() const {
  auto matrix = EncodedOriginToMatrix(origin, source->width(), source->height());
  matrix.postTranslate(-bounds.x(), -bounds.y());
  matrix.invert(&matrix);
  return matrix;
}
}  // namespace tgfx
