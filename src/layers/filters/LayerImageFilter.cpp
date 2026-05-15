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

#include "tgfx/layers/filters/LayerImageFilter.h"
#include "core/images/FilterImage.h"

namespace tgfx {

std::shared_ptr<Image> LayerImageFilter::onFilterImage(std::shared_ptr<Image> input, float scale,
                                                      Point* offset) {
  auto filter = getImageFilter(scale);
  if (!filter) {
    return input;
  }
  return FilterImage::MakeFrom(std::move(input), std::move(filter), offset);
}

std::shared_ptr<ImageFilter> LayerImageFilter::getImageFilter(float scale) {
  if (lastScale != scale || dirty) {
    lastFilter = onCreateImageFilter(scale);
    lastScale = scale;
    dirty = false;
  }
  return lastFilter;
}

Rect LayerImageFilter::filterBounds(const Rect& srcRect, float contentScale) {
  auto filter = getImageFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  return filter->filterBounds(srcRect);
}

void LayerImageFilter::invalidateFilter() {
  lastFilter = nullptr;
  dirty = true;
  LayerFilter::invalidateFilter();
}

std::shared_ptr<ImageFilter> LayerImageFilter::getComposeFilter(float scale, float, float,
                                                                const Point&) {
  return getImageFilter(scale);
}

}  // namespace tgfx
