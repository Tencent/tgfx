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

namespace tgfx {

std::shared_ptr<class DropShadowStyle> DropShadowStyle::Make(float offsetX, float offsetY,
                                                             float blurrinessX, float blurrinessY,
                                                             const Color& color,
                                                             bool showBehindLayer) {
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
  return filter->filterBounds(srcRect);
}

void DropShadowStyle::onDrawWithExtraSource(Canvas* canvas, std::shared_ptr<Image> content,
                                            float contentScale, std::shared_ptr<Image> extraSource,
                                            const Point& extraSourceOffset, float alpha,
                                            BlendMode blendMode) {
  Point offset = {};
  auto filter = getShadowFilter(contentScale);
  if (!filter) {
    return;
  }
  auto shadowImage = content->makeWithFilter(filter, &offset);
  // Use nearest filtering when there's no blur to avoid edge artifacts caused by linear
  // interpolation. When the texture is scaled up, linear filtering produces intermediate alpha
  // values at edges, which causes visible borders in the shadow.
  auto sampling = (_blurrinessX == 0 && _blurrinessY == 0)
                      ? SamplingOptions(FilterMode::Nearest, MipmapMode::None)
                      : SamplingOptions();
  Paint paint = {};
  if (!_showBehindLayer) {
    auto shader = Shader::MakeImageShader(extraSource, TileMode::Decal, TileMode::Decal, sampling);
    auto matrixShader =
        shader->makeWithMatrix(Matrix::MakeTrans(extraSourceOffset.x, extraSourceOffset.y));
    paint.setMaskFilter(MaskFilter::MakeShader(matrixShader, true));
  }
  paint.setBlendMode(blendMode);
  paint.setAlpha(alpha);
  canvas->drawImage(shadowImage, offset.x, offset.y, sampling, &paint);
}

void DropShadowStyle::onDraw(Canvas* canvas, std::shared_ptr<Image> content, float contentScale,
                             float alpha, BlendMode blendMode) {
  onDrawWithExtraSource(canvas, content, contentScale, nullptr, {}, alpha, blendMode);
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
