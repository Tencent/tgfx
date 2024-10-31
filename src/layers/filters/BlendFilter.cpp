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

#include "tgfx/layers/filters/BlendFilter.h"

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

std::shared_ptr<ImageFilter> BlendFilter::onCreateImageFilter(float) {
  return ImageFilter::ColorFilter(ColorFilter::Blend(_color, _blendMode));
}

BlendFilter::BlendFilter(const Color& color, BlendMode blendMode)
    : LayerFilter(), _color(std::move(color)), _blendMode(blendMode) {
}

}  // namespace tgfx
