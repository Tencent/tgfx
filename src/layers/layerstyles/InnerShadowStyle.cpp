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

#include "tgfx/layers/layerstyles/InnerShadowStyle.h"

namespace tgfx {

std::shared_ptr<InnerShadowStyle> InnerShadowStyle::Make(float offsetX, float offsetY,
                                                         float blurrinessX, float blurrinessY,
                                                         const Color& color) {

  return std::shared_ptr<InnerShadowStyle>(
      new InnerShadowStyle(offsetX, offsetY, blurrinessX, blurrinessY, color));
}

void InnerShadowStyle::setOffsetX(float offsetX) {
  if (_offsetX == offsetX) {
    return;
  }
  _offsetX = offsetX;
  invalidateFilter();
}

void InnerShadowStyle::setOffsetY(float offsetY) {
  if (_offsetY == offsetY) {
    return;
  }
  _offsetY = offsetY;
  invalidateFilter();
}

void InnerShadowStyle::setBlurrinessX(float blurrinessX) {
  if (_blurrinessX == blurrinessX) {
    return;
  }
  _blurrinessX = blurrinessX;
  invalidateFilter();
}

void InnerShadowStyle::setBlurrinessY(float blurrinessY) {
  if (_blurrinessY == blurrinessY) {
    return;
  }
  _blurrinessY = blurrinessY;
  invalidateFilter();
}

void InnerShadowStyle::setColor(const Color& color) {
  if (_color == color) {
    return;
  }
  _color = color;
  invalidateFilter();
}

InnerShadowStyle::InnerShadowStyle(float offsetX, float offsetY, float blurrinessX,
                                   float blurrinessY, const Color& color)
    : _offsetX(offsetX), _offsetY(offsetY), _blurrinessX(blurrinessX), _blurrinessY(blurrinessY),
      _color(color) {
}

Rect InnerShadowStyle::filterBounds(const Rect& srcRect, float contentScale) {
  auto filter = getShadowFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  return filter->filterBounds(srcRect);
}

void InnerShadowStyle::onDraw(Canvas* canvas, std::shared_ptr<Image> content, float contentScale,
                              float alpha, BlendMode blendMode) {
  auto filter = getShadowFilter(contentScale);
  if (!filter) {
    return;
  }
  content = content->makeWithFilter(filter);
  Paint paint = {};
  paint.setBlendMode(blendMode);
  paint.setAlpha(alpha);
  canvas->drawImage(content, &paint);
}

std::shared_ptr<ImageFilter> InnerShadowStyle::getShadowFilter(float scale) {
  if (shadowFilter && scale == currentScale) {
    return shadowFilter;
  }

  shadowFilter = ImageFilter::InnerShadowOnly(_offsetX * scale, _offsetY * scale,
                                              _blurrinessX * scale, _blurrinessY * scale, _color);
  currentScale = scale;

  return shadowFilter;
}

void InnerShadowStyle::invalidateFilter() {
  shadowFilter = nullptr;
  currentScale = 0.0f;
  invalidateTransform();
}

}  // namespace tgfx
