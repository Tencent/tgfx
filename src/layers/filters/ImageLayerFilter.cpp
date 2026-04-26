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

#include "tgfx/layers/filters/ImageLayerFilter.h"
#include "tgfx/core/Image.h"

namespace tgfx {

std::shared_ptr<ImageFilter> ImageLayerFilter::getImageFilter(float scale) {
  if (lastScale != scale || dirty) {
    lastFilter = onCreateImageFilter(scale);
    lastScale = scale;
    dirty = false;
  }
  return lastFilter;
}

Rect ImageLayerFilter::filterBounds(const Rect& srcRect, float contentScale) {
  auto filter = getImageFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  return filter->filterBounds(srcRect);
}

std::shared_ptr<Image> ImageLayerFilter::onFilterImage(std::shared_ptr<Image> input, const Rect&,
                                                       float scale, Point* offset) {
  auto filter = getImageFilter(scale);
  if (!filter) {
    return input;
  }
  return input->makeWithFilter(std::move(filter), offset);
}

void ImageLayerFilter::invalidateFilter() {
  lastFilter = nullptr;
  dirty = true;
  invalidateTransform();
}

}  // namespace tgfx
