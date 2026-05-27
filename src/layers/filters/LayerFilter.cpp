/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

std::shared_ptr<Image> LayerFilter::filterImage(std::shared_ptr<Image> input, float scale,
                                                const Rect& contentBounds, const Rect* clipBounds,
                                                Point* offset) {
  if (!input) {
    return nullptr;
  }
  return onFilterImage(std::move(input), scale, contentBounds, clipBounds, offset);
}

Rect LayerFilter::filterBounds(const Rect& srcRect, float, MapDirection) {
  return srcRect;
}

void LayerFilter::invalidateFilter() {
  onInvalidateFilter();
  invalidateTransform();
}

void LayerFilter::onInvalidateFilter() {
}

}  // namespace tgfx
