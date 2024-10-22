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

#include "tgfx/layers/filters/ColorMatrixLayerFilter.h"

namespace tgfx {

std::shared_ptr<ColorMatrixLayerFilter> ColorMatrixLayerFilter::Make() {
  return std::shared_ptr<ColorMatrixLayerFilter>(new ColorMatrixLayerFilter());
}

void ColorMatrixLayerFilter::setMatrix(const std::array<float, 20>& matrix) {
  if (_matrix == matrix) {
    return;
  }
  _matrix = std::move(matrix);
  invalidate();
}

std::shared_ptr<ImageFilter> ColorMatrixLayerFilter::onCreateImageFilter(float) {
  return ImageFilter::ColorFilter(ColorFilter::Matrix(_matrix));
}

}  // namespace tgfx