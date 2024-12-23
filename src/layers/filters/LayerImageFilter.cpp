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

#include "tgfx/layers/filters/LayerImageFilter.h"
#include "tgfx/layers/Layer.h"
namespace tgfx {

bool LayerImageFilter::applyFilter(Canvas* canvas, std::shared_ptr<Image> image,
                                   float contentScale) {
  auto filter = getImageFilter(contentScale);
  if (!filter) {
    return false;
  }
  Paint paint = {};
  paint.setImageFilter(filter);
  canvas->drawImage(image, &paint);
  return true;
}

Rect LayerImageFilter::filterBounds(const Rect& srcRect, float contentScale) {
  auto filter = getImageFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  return filter->filterBounds(srcRect);
}

void LayerImageFilter::invalidateFilter() {
  dirty = true;
  invalidate();
}

std::shared_ptr<ImageFilter> LayerImageFilter::getImageFilter(float scale) {
  if (dirty || lastScale != scale) {
    lastFilter = onCreateImageFilter(scale);
    lastScale = scale;
    dirty = false;
  }
  return lastFilter;
}

}  // namespace tgfx