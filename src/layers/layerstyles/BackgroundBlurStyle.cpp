/////////////////////////////////////////////////////////////////////////////////////////////////
//
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
#include "core/utils/Log.h"
#include "layers/CanvasUtils.h"

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

Rect BackgroundBlurStyle::filterBackgroundSoft(const Rect& srcRect, float contentScale) {
  auto filter = getBackgroundFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  return filter->filterBounds(srcRect);
}

void BackgroundBlurStyle::onDraw(Canvas* canvas, const LayerStyleInput& input, float, BlendMode) {
  if (_blurrinessX <= 0 && _blurrinessY <= 0) {
    return;
  }
  auto* background = input.findExtraSource(StyleInputSource::Type::Base);
  if (background == nullptr || background->image() == nullptr) {
    DEBUG_ASSERT(false);
    return;
  }

  auto bgImage = background->image();
  auto bgOffset = background->imageOffset();
  auto bgWidth = static_cast<float>(bgImage->width());
  auto bgHeight = static_cast<float>(bgImage->height());

  auto blurFilter = getBackgroundFilter(input.contentScale);

  // Subset the background to the visible canvas clip + blur radius, so makeWithFilter
  // only evaluates within the region that will actually be drawn.
  auto imageRect = Rect::MakeWH(bgWidth, bgHeight);
  auto clipBounds = GetClipBounds(canvas);
  if (clipBounds.has_value() && !clipBounds->isEmpty()) {
    // clipBounds is in layer-local space; bgImage pixels are offset by bgOffset from that space.
    imageRect = clipBounds.value();
    imageRect.offset(-bgOffset.x, -bgOffset.y);
    if (!imageRect.intersect(Rect::MakeWH(bgWidth, bgHeight))) {
      return;
    }
    // Expand by blur radius so makeWithFilter has source data on both sides of the visible edge.
    auto blurOutset = blurFilter ? blurFilter->filterBounds(Rect::MakeEmpty()) : Rect::MakeEmpty();
    float outsetX = std::max(-blurOutset.left, blurOutset.right);
    float outsetY = std::max(-blurOutset.top, blurOutset.bottom);
    imageRect.outset(outsetX, outsetY);
    if (!imageRect.intersect(Rect::MakeWH(bgWidth, bgHeight))) {
      return;
    }
    imageRect.roundOut();
  }

  auto subsetImage = bgImage->makeSubset(imageRect);
  if (subsetImage == nullptr) {
    subsetImage = bgImage;
  } else {
    bgOffset.x += imageRect.left;
    bgOffset.y += imageRect.top;
  }

  Point backgroundOffset = {};
  auto clipRect = Rect::MakeWH(subsetImage->width(), subsetImage->height());
  auto blurBackground = subsetImage->makeWithFilter(blurFilter, &backgroundOffset, &clipRect);
  backgroundOffset.x += bgOffset.x;
  backgroundOffset.y += bgOffset.y;

  auto maskShader = Shader::MakeImageShader(input.content, TileMode::Decal, TileMode::Decal);

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
      ImageFilter::Blur(_blurrinessX * contentScale, _blurrinessY * contentScale, _tileMode);
  return backgroundFilter;
}

}  // namespace tgfx
