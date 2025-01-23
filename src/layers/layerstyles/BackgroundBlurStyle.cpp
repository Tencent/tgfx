//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
  invalidate();
}

void BackgroundBlurStyle::setBlurrinessY(float blurriness) {
  if (_blurrinessY == blurriness) {
    return;
  }
  _blurrinessY = blurriness;
  invalidate();
}

void BackgroundBlurStyle::setTileMode(TileMode tileMode) {
  if (_tileMode == tileMode) {
    return;
  }
  _tileMode = tileMode;
  invalidate();
}

void BackgroundBlurStyle::onDrawWithExtraSource(Canvas* canvas, std::shared_ptr<Image> content,
                                                float contentScale,
                                                std::shared_ptr<Image> extraSource,
                                                const Point& extraSourceOffset, float,
                                                BlendMode blendMode) {
  if (_blurrinessX <= 0 && _blurrinessY <= 0) {
    return;
  }

  // draw background out of the mask
  Paint maskPaint = {};
  maskPaint.setImageFilter(ImageFilter::ColorFilter(ColorFilter::AlphaThreshold(0)));
  maskPaint.setBlendMode(BlendMode::DstOut);
  canvas->drawImage(content, &maskPaint);

  // create blurred background
  auto blur =
      ImageFilter::Blur(_blurrinessX * contentScale, _blurrinessX * contentScale, _tileMode);
  Point backgroundOffset = Point::Zero();
  auto blurBackground = extraSource->makeWithFilter(blur, &backgroundOffset);
  backgroundOffset += extraSourceOffset;

  auto maskShader = Shader::MakeImageShader(content, TileMode::Decal, TileMode::Decal);
  maskShader = maskShader->makeWithColorFilter(ColorFilter::AlphaThreshold(0));
  Matrix matrix = Matrix::MakeTrans(-backgroundOffset.x, -backgroundOffset.y);
  maskShader = maskShader->makeWithMatrix(matrix);

  // draw blurred background in the mask
  Paint paint = {};
  paint.setMaskFilter(MaskFilter::MakeShader(maskShader, false));
  paint.setBlendMode(blendMode);
  canvas->drawImage(blurBackground, backgroundOffset.x, backgroundOffset.y, &paint);
}

BackgroundBlurStyle::BackgroundBlurStyle(float blurrinessX, float blurrinessY, TileMode tileMode)
    : _blurrinessX(blurrinessX), _blurrinessY(blurrinessY), _tileMode(tileMode) {
}

}  // namespace tgfx