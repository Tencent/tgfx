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

#include "tgfx/layers/filters/InnerShadowFilter.h"

namespace tgfx {
std::shared_ptr<InnerShadowFilter> InnerShadowFilter::Make(float offsetX, float offsetY,
                                                           float blurrinessX, float blurrinessY,
                                                           const Color& color,
                                                           bool innerShadowOnly) {
  return std::shared_ptr<InnerShadowFilter>(
      new InnerShadowFilter(offsetX, offsetY, blurrinessX, blurrinessY, color, innerShadowOnly));
}

void InnerShadowFilter::setOffsetX(float offsetX) {
  if (_offsetX == offsetX) {
    return;
  }
  _offsetX = offsetX;
  invalidateFilter();
}

void InnerShadowFilter::setOffsetY(float offsetY) {
  if (_offsetY == offsetY) {
    return;
  }
  _offsetY = offsetY;
  invalidateFilter();
}

void InnerShadowFilter::setBlurrinessX(float blurrinessX) {
  if (_blurrinessX == blurrinessX) {
    return;
  }
  _blurrinessX = blurrinessX;
  invalidateFilter();
}

void InnerShadowFilter::setBlurrinessY(float blurrinessY) {
  if (_blurrinessY == blurrinessY) {
    return;
  }
  _blurrinessY = blurrinessY;
  invalidateFilter();
}

void InnerShadowFilter::setColor(const Color& color) {
  if (_color == color) {
    return;
  }
  _color = color;
  invalidateFilter();
}

void InnerShadowFilter::setInnerShadowOnly(bool value) {
  if (_innerShadowOnly == value) {
    return;
  }
  _innerShadowOnly = value;
  invalidateFilter();
}

std::shared_ptr<ImageFilter> InnerShadowFilter::onCreateImageFilter(float scale) {
  if (_innerShadowOnly) {
    return ImageFilter::InnerShadowOnly(_offsetX * scale, _offsetY * scale, _blurrinessX * scale,
                                        _blurrinessY * scale, _color);
  }
  return ImageFilter::InnerShadow(_offsetX * scale, _offsetY * scale, _blurrinessX * scale,
                                  _blurrinessY * scale, _color);
}

InnerShadowFilter::InnerShadowFilter(float offsetX, float offsetY, float blurrinessX,
                                     float blurrinessY, const Color& color, bool innerShadowOnly)
    : _offsetX(offsetX), _offsetY(offsetY), _blurrinessX(blurrinessX), _blurrinessY(blurrinessY),
      _color(color), _innerShadowOnly(innerShadowOnly) {
}

}  // namespace tgfx
