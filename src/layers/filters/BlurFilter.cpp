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

#include "tgfx/layers/filters/BlurFilter.h"

namespace tgfx {

std::shared_ptr<BlurFilter> BlurFilter::Make(float blurrinessX, float blurrinessY,
                                             TileMode tileMode) {
  return std::shared_ptr<BlurFilter>(new BlurFilter(blurrinessX, blurrinessY, tileMode));
}

void BlurFilter::setBlurrinessX(float blurrinessX) {
  if (_blurrinessX == blurrinessX) {
    return;
  }
  _blurrinessX = blurrinessX;
  invalidateFilter();
}

void BlurFilter::setBlurrinessY(float blurrinessY) {
  if (_blurrinessY == blurrinessY) {
    return;
  }
  _blurrinessY = blurrinessY;
  invalidateFilter();
}

void BlurFilter::setTileMode(TileMode tileMode) {
  if (_tileMode == tileMode) {
    return;
  }
  _tileMode = tileMode;
  invalidateFilter();
}

BlurFilter::BlurFilter(float blurrinessX, float blurrinessY, TileMode tileMode)
    : _blurrinessX(blurrinessX), _blurrinessY(blurrinessY), _tileMode(tileMode) {
}

std::shared_ptr<ImageFilter> BlurFilter::onCreateImageFilter(float scale) {
  return ImageFilter::Blur(_blurrinessX * scale, _blurrinessY * scale, _tileMode);
}

}  // namespace tgfx
