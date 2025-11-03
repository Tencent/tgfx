//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"

namespace tgfx {

std::shared_ptr<BackgroundBlurStyle> BackgroundBlurStyle::Make(float blurrinessX, float blurrinessY,
                                                               TileMode tileMode) {
  return std::shared_ptr<BackgroundBlurStyle>(
      new BackgroundBlurStyle(blurrinessX, blurrinessY, tileMode));
}

void BackgroundBlurStyle::setBlurrinessX(float blurriness) {
  if (_blurrinessX == blurriness) {
    return;
  }
  _blurrinessX = blurriness;
  invalidateTransform();
}

void BackgroundBlurStyle::setBlurrinessY(float blurriness) {
  if (_blurrinessY == blurriness) {
    return;
  }
  _blurrinessY = blurriness;
  invalidateTransform();
}

void BackgroundBlurStyle::setTileMode(TileMode tileMode) {
  if (_tileMode == tileMode) {
    return;
  }
  _tileMode = tileMode;
  invalidateTransform();
}

Rect BackgroundBlurStyle::filterBackground(const Rect& srcRect, float contentScale) {
  auto filter = getBackgroundFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  return filter->filterBounds(srcRect);
}

bool BackgroundBlurStyle::shouldDraw(float contentScale) const {
  return _blurrinessX * contentScale > 0.05f || _blurrinessY * contentScale > 0.05f;
}

void BackgroundBlurStyle::onDrawWithExtraSource(Canvas* canvas, std::shared_ptr<Image> contour,
                                                float contentScale,
                                                std::shared_ptr<Image> extraSource,
                                                const Point& extraSourceOffset, float, BlendMode) {
  if (_blurrinessX <= 0 && _blurrinessY <= 0) {
    return;
  }

  // create blurred background
  auto blurFilter = getBackgroundFilter(contentScale);
  Point backgroundOffset = {};
  auto clipRect = Rect::MakeWH(extraSource->width(), extraSource->height());
  auto blurBackground = extraSource->makeWithFilter(blurFilter, &backgroundOffset, &clipRect);
  backgroundOffset += extraSourceOffset;

  auto maskShader = Shader::MakeImageShader(contour, TileMode::Decal, TileMode::Decal);

  // draw blurred background in the mask
  Paint paint = {};
  paint.setMaskFilter(MaskFilter::MakeShader(maskShader, false));
  paint.setBlendMode(BlendMode::Src);
  canvas->drawImage(blurBackground, backgroundOffset.x, backgroundOffset.y, &paint);
}

BackgroundBlurStyle::BackgroundBlurStyle(float blurrinessX, float blurrinessY, TileMode tileMode)
    : _blurrinessX(blurrinessX), _blurrinessY(blurrinessY), _tileMode(tileMode) {
}

std::shared_ptr<ImageFilter> BackgroundBlurStyle::getBackgroundFilter(float contentScale) {
  if (backgroundFilter && contentScale == currentScale) {
    return backgroundFilter;
  }
  currentScale = contentScale;
  backgroundFilter =
      ImageFilter::Blur(_blurrinessX * contentScale, _blurrinessX * contentScale, _tileMode);
  return backgroundFilter;
}

}  // namespace tgfx
