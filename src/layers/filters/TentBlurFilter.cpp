/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/layers/filters/TentBlurFilter.h"
#include "TentBlurImageFilter.h"

namespace tgfx {

std::shared_ptr<ImageFilter> TentBlurFilter::MakeImageFilter(float radius, TileMode tileMode) {
  if (radius < 1.0f) {
    return nullptr;
  }
  return std::make_shared<TentBlurImageFilter>(radius, radius, tileMode);
}

std::shared_ptr<TentBlurFilter> TentBlurFilter::Make(float blurrinessX, float blurrinessY,
                                                     TileMode tileMode) {
  return std::shared_ptr<TentBlurFilter>(new TentBlurFilter(blurrinessX, blurrinessY, tileMode));
}

void TentBlurFilter::setBlurrinessX(float blurrinessX) {
  if (_blurrinessX == blurrinessX) {
    return;
  }
  _blurrinessX = blurrinessX;
  invalidateFilter();
}

void TentBlurFilter::setBlurrinessY(float blurrinessY) {
  if (_blurrinessY == blurrinessY) {
    return;
  }
  _blurrinessY = blurrinessY;
  invalidateFilter();
}

void TentBlurFilter::setTileMode(TileMode tileMode) {
  if (_tileMode == tileMode) {
    return;
  }
  _tileMode = tileMode;
  invalidateFilter();
}

TentBlurFilter::TentBlurFilter(float blurrinessX, float blurrinessY, TileMode tileMode)
    : _blurrinessX(blurrinessX), _blurrinessY(blurrinessY), _tileMode(tileMode) {
}

std::shared_ptr<ImageFilter> TentBlurFilter::onCreateImageFilter(float scale) {
  return std::make_shared<TentBlurImageFilter>(_blurrinessX * scale, _blurrinessY * scale,
                                               _tileMode);
}

}  // namespace tgfx
