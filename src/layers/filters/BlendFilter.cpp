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

#include "tgfx/layers/filters/BlendFilter.h"
#include "core/images/FilterImage.h"

namespace tgfx {

std::shared_ptr<BlendFilter> BlendFilter::Make(const Color& color, BlendMode mode) {
  return std::shared_ptr<BlendFilter>(new BlendFilter(color, mode));
}

void BlendFilter::setBlendMode(BlendMode mode) {
  if (_blendMode == mode) {
    return;
  }
  _blendMode = mode;
  invalidateFilter();
}

void BlendFilter::setColor(const Color& color) {
  if (_color == color) {
    return;
  }
  _color = color;
  invalidateFilter();
}

std::shared_ptr<Image> BlendFilter::onFilterImage(std::shared_ptr<Image> input, float scale,
                                                  const Rect&, const Rect* clipBounds,
                                                  Point* offset) {
  auto filter = getImageFilter(scale);
  if (!filter) {
    return input;
  }
  return FilterImage::MakeFrom(std::move(input), std::move(filter), offset, clipBounds);
}

Rect BlendFilter::filterBounds(const Rect& srcRect, float contentScale, MapDirection direction) {
  auto filter = getImageFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  return filter->filterBounds(srcRect, direction);
}

void BlendFilter::invalidateFilter() {
  lastFilter = nullptr;
  dirty = true;
  LayerFilter::invalidateFilter();
}

BlendFilter::BlendFilter(const Color& color, BlendMode blendMode)
    : _color(std::move(color)), _blendMode(blendMode) {
}

std::shared_ptr<ImageFilter> BlendFilter::getImageFilter(float scale) {
  if (lastScale != scale || dirty) {
    lastFilter = onCreateImageFilter(scale);
    lastScale = scale;
    dirty = false;
  }
  return lastFilter;
}

std::shared_ptr<ImageFilter> BlendFilter::onCreateImageFilter(float) {
  return ImageFilter::ColorFilter(ColorFilter::Blend(_color, _blendMode));
}

}  // namespace tgfx
