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

#include "tgfx/layers/filters/DropShadowLayerFilter.h"

namespace tgfx {
std::shared_ptr<DropShadowLayerFilter> DropShadowLayerFilter::Make() {
  return std::make_shared<DropShadowLayerFilter>();
}

void DropShadowLayerFilter::setDx(float dx) {
  if (_dx == dx) {
    return;
  }
  _dx = dx;
  invalidate();
}

void DropShadowLayerFilter::setDy(float dy) {
  if (_dy == dy) {
    return;
  }
  _dy = dy;
  invalidate();
}

void DropShadowLayerFilter::setBlurrinessX(float blurrinessX) {
  if (_blurrinessX == blurrinessX) {
    return;
  }
  _blurrinessX = blurrinessX;
  invalidate();
}

void DropShadowLayerFilter::setBlurrinessY(float blurrinessY) {
  if (_blurrinessY == blurrinessY) {
    return;
  }
  _blurrinessY = blurrinessY;
  invalidate();
}

void DropShadowLayerFilter::setColor(const Color& color) {
  if (_color == color) {
    return;
  }
  _color = color;
  invalidate();
}

void DropShadowLayerFilter::setDropsShadowOnly(bool value) {
  if (_dropsShadowOnly == value) {
    return;
  }
  _dropsShadowOnly = value;
  invalidate();
}

std::shared_ptr<ImageFilter> DropShadowLayerFilter::onCreateImageFilter(float scale) {
  if (_dropsShadowOnly) {
    return ImageFilter::DropShadowOnly(_dx * scale, _dy * scale, _blurrinessX * scale,
                                       _blurrinessY * scale, _color);
  } else {
    return ImageFilter::DropShadow(_dx * scale, _dy * scale, _blurrinessX * scale,
                                   _blurrinessY * scale, _color);
  }
}

}  // namespace tgfx
