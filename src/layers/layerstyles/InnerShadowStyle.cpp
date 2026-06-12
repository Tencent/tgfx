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
#include "core/filters/InnerShadowImageFilter.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/SpreadUtils.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ImageFilter.h"

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

void InnerShadowStyle::setSpread(float spread) {
  if (_spread == spread) {
    return;
  }
  _spread = spread;
  // Spread does not affect the cached ImageFilter. Only trigger a redraw to regenerate the
  // spread shape image.
  invalidateTransform();
}

InnerShadowStyle::InnerShadowStyle(float offsetX, float offsetY, float blurrinessX,
                                   float blurrinessY, const Color& color)
    : _offsetX(offsetX), _offsetY(offsetY), _blurrinessX(blurrinessX), _blurrinessY(blurrinessY),
      _color(color) {
}

Rect InnerShadowStyle::filterBounds(const Rect& srcRect, float contentScale) {
  // Spread does not change the output bounds because inner shadow remains within the content area.
  auto filter = getShadowFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  return filter->filterBounds(srcRect);
}

void InnerShadowStyle::onDraw(Canvas* canvas, const LayerStyleInput& input, float alpha,
                              BlendMode blendMode) {
  auto scale = input.contentScale;
  auto filter = getShadowFilter(scale);
  if (!filter) {
    return;
  }

  std::shared_ptr<Image> filterSource = input.content;
  Point filterSourceOffset = {};
  if (!FloatNearlyZero(_spread)) {
    auto shapeSource = SpreadUtils::MakeSpreadShapeImage(input, 0);
    auto maskSource = SpreadUtils::MakeSpreadShapeImage(input, -_spread);
    if (maskSource.collapsed) {
      // The mask fully collapsed — shadow fills the entire content area.
      if (shapeSource.image == nullptr) {
        return;
      }
      Paint paint = {};
      paint.setBlendMode(blendMode);
      paint.setAlpha(alpha);
      paint.setColorFilter(ColorFilter::Blend(_color, BlendMode::SrcIn));
      canvas->drawImage(shapeSource.image, shapeSource.offset.x, shapeSource.offset.y, {}, &paint);
      return;
    }
    if (shapeSource.image != nullptr && maskSource.image != nullptr) {
      filterSource = shapeSource.image;
      filterSourceOffset = shapeSource.offset;
      filter->maskImage = maskSource.image;
      filter->maskOffset = Point::Make(_spread * scale, _spread * scale);
    }
  }
  DEBUG_ASSERT(filterSource != nullptr);
  if (filterSource == nullptr) {
    return;
  }
  auto content = filterSource->makeWithFilter(filter);
  DEBUG_ASSERT(content != nullptr);
  if (content == nullptr) {
    return;
  }

  Paint paint = {};
  paint.setBlendMode(blendMode);
  paint.setAlpha(alpha);
  // Use nearest filtering when there's no blur to avoid edge artifacts caused by linear
  // interpolation. When the texture is scaled up, linear filtering produces intermediate alpha
  // values at edges, which causes visible gray borders in the inner shadow.
  auto sampling = SamplingOptions();
  if (_blurrinessX == 0 && _blurrinessY == 0) {
    sampling = SamplingOptions(FilterMode::Nearest, MipmapMode::None);
  }
  canvas->drawImage(content, filterSourceOffset.x, filterSourceOffset.y, sampling, &paint);
}

std::shared_ptr<InnerShadowImageFilter> InnerShadowStyle::getShadowFilter(float scale) {
  if (shadowFilter && scale == currentScale) {
    return shadowFilter;
  }

  shadowFilter = std::make_shared<InnerShadowImageFilter>(
      _offsetX * scale, _offsetY * scale, _blurrinessX * scale, _blurrinessY * scale, _color, true);
  currentScale = scale;

  return shadowFilter;
}

void InnerShadowStyle::invalidateFilter() {
  shadowFilter = nullptr;
  currentScale = 0.0f;
  invalidateTransform();
}

}  // namespace tgfx
