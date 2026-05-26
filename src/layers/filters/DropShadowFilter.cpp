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

#include "tgfx/layers/filters/DropShadowFilter.h"
#include "core/images/FilterImage.h"

namespace tgfx {
std::shared_ptr<DropShadowFilter> DropShadowFilter::Make(float offsetX, float offsetY,
                                                         float blurrinessX, float blurrinessY,
                                                         const Color& color, bool dropsShadowOnly) {
  return std::shared_ptr<DropShadowFilter>(
      new DropShadowFilter(offsetX, offsetY, blurrinessX, blurrinessY, color, dropsShadowOnly));
}

void DropShadowFilter::setOffsetX(float offsetX) {
  if (_offsetX == offsetX) {
    return;
  }
  _offsetX = offsetX;
  invalidateFilter();
}

void DropShadowFilter::setOffsetY(float offsetY) {
  if (_offsetY == offsetY) {
    return;
  }
  _offsetY = offsetY;
  invalidateFilter();
}

void DropShadowFilter::setBlurrinessX(float blurrinessX) {
  if (_blurrinessX == blurrinessX) {
    return;
  }
  _blurrinessX = blurrinessX;
  invalidateFilter();
}

void DropShadowFilter::setBlurrinessY(float blurrinessY) {
  if (_blurrinessY == blurrinessY) {
    return;
  }
  _blurrinessY = blurrinessY;
  invalidateFilter();
}

void DropShadowFilter::setColor(const Color& color) {
  if (_color == color) {
    return;
  }
  _color = color;
  invalidateFilter();
}

void DropShadowFilter::setDropsShadowOnly(bool value) {
  if (_dropsShadowOnly == value) {
    return;
  }
  _dropsShadowOnly = value;
  invalidateFilter();
}

std::shared_ptr<Image> DropShadowFilter::onFilterImage(std::shared_ptr<Image> input, float scale,
                                                       const Rect&, const Rect* clipBounds,
                                                       Point* offset) {
  auto filter = getImageFilter(scale);
  if (!filter) {
    return input;
  }
  return FilterImage::MakeFrom(std::move(input), std::move(filter), offset, clipBounds);
}

Rect DropShadowFilter::filterBounds(const Rect& srcRect, float contentScale,
                                    MapDirection direction) {
  auto filter = getImageFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  return filter->filterBounds(srcRect, direction);
}

void DropShadowFilter::invalidateFilter() {
  lastFilter = nullptr;
  dirty = true;
  LayerFilter::invalidateFilter();
}

DropShadowFilter::DropShadowFilter(float offsetX, float offsetY, float blurrinessX,
                                   float blurrinessY, const Color& color, bool dropsShadowOnly)
    : _offsetX(offsetX), _offsetY(offsetY), _blurrinessX(blurrinessX), _blurrinessY(blurrinessY),
      _color(std::move(color)), _dropsShadowOnly(dropsShadowOnly) {
}

std::shared_ptr<ImageFilter> DropShadowFilter::getImageFilter(float scale) {
  if (lastScale != scale || dirty) {
    lastFilter = onCreateImageFilter(scale);
    lastScale = scale;
    dirty = false;
  }
  return lastFilter;
}

std::shared_ptr<ImageFilter> DropShadowFilter::onCreateImageFilter(float scale) {
  if (_dropsShadowOnly) {
    return ImageFilter::DropShadowOnly(_offsetX * scale, _offsetY * scale, _blurrinessX * scale,
                                       _blurrinessY * scale, _color);
  }
  return ImageFilter::DropShadow(_offsetX * scale, _offsetY * scale, _blurrinessX * scale,
                                 _blurrinessY * scale, _color);
}

}  // namespace tgfx
