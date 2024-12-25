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

#include "tgfx/layers/layerstyles/DropShadowStyle.h"

namespace tgfx {

std::shared_ptr<class DropShadowStyle> DropShadowStyle::Make(float offsetX, float offsetY,
                                                             float blurrinessX, float blurrinessY,
                                                             const Color& color,
                                                             bool showBehindTransparent,
                                                             BlendMode blendMode) {
  return std::shared_ptr<DropShadowStyle>(new DropShadowStyle(
      offsetX, offsetY, blurrinessX, blurrinessY, color, showBehindTransparent, blendMode));
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

void DropShadowStyle::setShowBehindTransparent(bool showBehindTransparent) {
  if (_showBehindTransparent == showBehindTransparent) {
    return;
  }
  _showBehindTransparent = showBehindTransparent;
  invalidate();
}

DropShadowStyle::DropShadowStyle(float offsetX, float offsetY, float blurrinessX, float blurrinessY,
                                 const Color& color, bool showBehindTransparent,
                                 BlendMode blendMode)
    : LayerStyle(blendMode), _offsetX(offsetX), _offsetY(offsetY), _blurrinessX(blurrinessX),
      _blurrinessY(blurrinessY), _color(color), _showBehindTransparent(showBehindTransparent) {
}

void DropShadowStyle::apply(Canvas* canvas, std::shared_ptr<Image> content, float contentScale) {
  // create opaque image
  auto opaqueFilter = ImageFilter::ColorFilter(ColorFilter::AlphaThreshold(0));
  auto opaqueImage = content->makeWithFilter(opaqueFilter);

  Point offset = Point::Zero();

  auto filter = getShadowFilter(contentScale);
  if (!filter) {
    return;
  }
  auto shadowImage = opaqueImage->makeWithFilter(filter, &offset);
  Paint paint = {};
  if (!_showBehindTransparent) {
    auto shader = Shader::MakeImageShader(opaqueImage, TileMode::Decal, TileMode::Decal);
    auto matrixShader = shader->makeWithMatrix(Matrix::MakeTrans(-offset.x, -offset.y));
    paint.setMaskFilter(MaskFilter::MakeShader(matrixShader, true));
  }
  paint.setBlendMode(_blendMode);
  canvas->drawImage(shadowImage, offset.x, offset.y, &paint);
}

Rect DropShadowStyle::filterBounds(const Rect& srcRect, float contentScale) {
  auto filter = getShadowFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  return filter->filterBounds(srcRect);
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
  invalidate();
}

}  // namespace tgfx
