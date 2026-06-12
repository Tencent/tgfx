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

#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/SpreadUtils.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {

std::shared_ptr<DropShadowStyle> DropShadowStyle::Make(float offsetX, float offsetY,
                                                       float blurrinessX, float blurrinessY,
                                                       const Color& color, bool showBehindLayer) {
  return std::shared_ptr<DropShadowStyle>(
      new DropShadowStyle(offsetX, offsetY, blurrinessX, blurrinessY, color, showBehindLayer));
}

void DropShadowStyle::setOffsetX(float offsetX) {
  if (_offsetX == offsetX) {
    return;
  }
  _offsetX = offsetX;
  invalidateFilter();
}

void DropShadowStyle::setOffsetY(float offsetY) {
  if (_offsetY == offsetY) {
    return;
  }
  _offsetY = offsetY;
  invalidateFilter();
}

void DropShadowStyle::setBlurrinessX(float blurrinessX) {
  if (_blurrinessX == blurrinessX) {
    return;
  }
  _blurrinessX = blurrinessX;
  invalidateFilter();
}

void DropShadowStyle::setBlurrinessY(float blurrinessY) {
  if (_blurrinessY == blurrinessY) {
    return;
  }
  _blurrinessY = blurrinessY;
  invalidateFilter();
}

void DropShadowStyle::setColor(const Color& color) {
  if (_color == color) {
    return;
  }
  _color = color;
  invalidateFilter();
}

void DropShadowStyle::setShowBehindLayer(bool showBehindLayer) {
  if (_showBehindLayer == showBehindLayer) {
    return;
  }
  _showBehindLayer = showBehindLayer;
  invalidateTransform();
}

void DropShadowStyle::setSpread(float spread) {
  if (_spread == spread) {
    return;
  }
  _spread = spread;
  // Spread does not affect the cached ImageFilter. Only trigger a redraw to regenerate the
  // spread shape image.
  invalidateTransform();
}

DropShadowStyle::DropShadowStyle(float offsetX, float offsetY, float blurrinessX, float blurrinessY,
                                 const Color& color, bool showBehindLayer)
    : _offsetX(offsetX), _offsetY(offsetY), _blurrinessX(blurrinessX), _blurrinessY(blurrinessY),
      _color(color), _showBehindLayer(showBehindLayer) {
}

Rect DropShadowStyle::filterBounds(const Rect& srcRect, float contentScale) {
  auto filter = getShadowFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  auto bounds = srcRect;
  if (!FloatNearlyZero(_spread)) {
    bounds.outset(_spread * contentScale, _spread * contentScale);
  }
  return filter->filterBounds(bounds);
}

void DropShadowStyle::onDraw(Canvas* canvas, const LayerStyleInput& input, float alpha,
                             BlendMode blendMode) {
  Point offset = {};
  auto filter = getShadowFilter(input.contentScale);
  if (!filter) {
    return;
  }
  std::shared_ptr<Image> filterSource = input.content;
  Point filterSourceOffset = {};
  if (!FloatNearlyZero(_spread)) {
    auto spreadImage = SpreadUtils::MakeSpreadShapeImage(input, _spread);
    if (spreadImage.collapsed) {
      return;
    }
    if (spreadImage.image != nullptr) {
      filterSource = std::move(spreadImage.image);
      filterSourceOffset = spreadImage.offset;
    }
  }
  DEBUG_ASSERT(filterSource != nullptr);
  if (filterSource == nullptr) {
    return;
  }
  auto shadowImage = filterSource->makeWithFilter(filter, &offset);
  DEBUG_ASSERT(shadowImage != nullptr);
  if (shadowImage == nullptr) {
    return;
  }

  // Use nearest filtering when there's no blur to avoid edge artifacts caused by linear
  // interpolation. When the texture is scaled up, linear filtering produces intermediate alpha
  // values at edges, which causes visible borders in the shadow.
  auto sampling = (_blurrinessX == 0 && _blurrinessY == 0)
                      ? SamplingOptions(FilterMode::Nearest, MipmapMode::None)
                      : SamplingOptions();
  Paint paint = {};
  if (!_showBehindLayer && input.extraSource != nullptr) {
    auto shader =
        Shader::MakeImageShader(input.extraSource, TileMode::Decal, TileMode::Decal, sampling);
    auto matrixShader = shader->makeWithMatrix(
        Matrix::MakeTrans(input.extraSourceOffset.x, input.extraSourceOffset.y));
    paint.setMaskFilter(MaskFilter::MakeShader(matrixShader, true));
  }
  paint.setBlendMode(blendMode);
  paint.setAlpha(alpha);
  canvas->drawImage(shadowImage, filterSourceOffset.x + offset.x, filterSourceOffset.y + offset.y,
                    sampling, &paint);
}

std::shared_ptr<ImageFilter> DropShadowStyle::getShadowFilter(float scale) {
  if (shadowFilter && scale == currentScale) {
    return shadowFilter;
  }

  shadowFilter = ImageFilter::DropShadowOnly(_offsetX * scale, _offsetY * scale,
                                             _blurrinessX * scale, _blurrinessY * scale, _color);
  currentScale = scale;

  return shadowFilter;
}

void DropShadowStyle::invalidateFilter() {
  shadowFilter = nullptr;
  invalidateTransform();
}

}  // namespace tgfx
