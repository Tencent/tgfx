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

#include "tgfx/layers/filters/ColorBlendLayerFilter.h"

namespace tgfx {

std::shared_ptr<ColorBlendLayerFilter> ColorBlendLayerFilter::Make() {
  return std::make_shared<ColorBlendLayerFilter>();
}

void ColorBlendLayerFilter::setBlendMode(BlendMode mode) {
  if (_blendMode == mode) {
    return;
  }
  _blendMode = mode;
  invalidate();
}

void ColorBlendLayerFilter::setColor(const Color& color) {
  if (_color == color) {
    return;
  }
  _color = color;
  invalidate();
}

std::shared_ptr<ImageFilter> ColorBlendLayerFilter::onCreateImageFilter(float) {
  return ImageFilter::ColorFilter(ColorFilter::Blend(_color, _blendMode));
}

}  // namespace tgfx
