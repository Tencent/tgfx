/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

std::shared_ptr<ImageFilter> LayerFilter::getImageFilter(float scale) {
  if (lastScale != scale || dirty) {
    lastFilter = onCreateImageFilter(scale);
    lastScale = scale;
    dirty = false;
  }
  return lastFilter;
}

Rect LayerFilter::filterBounds(const Rect& srcRect, float contentScale) {
  auto filter = getImageFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  return filter->filterBounds(srcRect);
}

void LayerFilter::invalidateFilter() {
  lastFilter = nullptr;
  dirty = true;
  invalidate();
}

}  // namespace tgfx
