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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "tgfx/layers/filters/TextureFilter.h"
#include <cmath>
#include "core/filters/TextureImageFilter.h"
#include "core/images/FilterImage.h"

namespace tgfx {

namespace {

bool IsValidSize(float value) {
  return std::isfinite(value) && value > 0.0f;
}

bool IsValidRadius(float value) {
  return std::isfinite(value) && value >= 0.0f;
}

}  // namespace

std::shared_ptr<TextureFilter> TextureFilter::Make(float sizeX, float sizeY, float radius,
                                                   uint32_t seed, bool clipToShape) {
  if (!IsValidSize(sizeX) || !IsValidSize(sizeY) || !IsValidRadius(radius)) {
    return nullptr;
  }
  return std::shared_ptr<TextureFilter>(new TextureFilter(sizeX, sizeY, radius, seed, clipToShape));
}

TextureFilter::TextureFilter(float sizeX, float sizeY, float radius, uint32_t seed,
                             bool clipToShape)
    : _sizeX(sizeX), _sizeY(sizeY), _radius(radius), _seed(seed), _clipToShape(clipToShape) {
}

void TextureFilter::setSizeX(float value) {
  if (_sizeX == value || !IsValidSize(value)) {
    return;
  }
  _sizeX = value;
  invalidateFilter();
}

void TextureFilter::setSizeY(float value) {
  if (_sizeY == value || !IsValidSize(value)) {
    return;
  }
  _sizeY = value;
  invalidateFilter();
}

void TextureFilter::setRadius(float value) {
  if (_radius == value || !IsValidRadius(value)) {
    return;
  }
  _radius = value;
  invalidateFilter();
}

void TextureFilter::setSeed(uint32_t value) {
  if (_seed == value) {
    return;
  }
  _seed = value;
  invalidateFilter();
}

void TextureFilter::setClipToShape(bool value) {
  if (_clipToShape == value) {
    return;
  }
  _clipToShape = value;
  invalidateFilter();
}

Rect TextureFilter::filterBounds(const Rect& srcRect, float contentScale, MapDirection direction) {
  if (!std::isfinite(contentScale) || contentScale <= 0.0f) {
    return srcRect;
  }
  auto params = MakeTextureGrainParams(_sizeX, _sizeY, _radius, Point::Zero(), _seed,
                                       Point::Zero(), contentScale, _clipToShape);
  TextureImageFilter filter(params);
  return filter.filterBounds(srcRect, direction);
}

std::shared_ptr<Image> TextureFilter::onFilterImage(std::shared_ptr<Image> input, float scale,
                                                    const Rect& contentBounds, Point* offset) {
  if (input == nullptr || !std::isfinite(scale) || scale <= 0.0f) {
    return input;
  }
  auto localBounds = Rect::MakeLTRB(contentBounds.left / scale, contentBounds.top / scale,
                                    contentBounds.right / scale, contentBounds.bottom / scale);
  auto center = Point::Make(localBounds.centerX(), localBounds.centerY());
  auto sourceOrigin = Point::Make(localBounds.left, localBounds.top);
  auto params = MakeTextureGrainParams(_sizeX, _sizeY, _radius, center, _seed, sourceOrigin, scale,
                                       _clipToShape);
  auto filter = std::make_shared<TextureImageFilter>(params);
  return FilterImage::MakeFrom(std::move(input), std::move(filter), offset, nullptr);
}

}  // namespace tgfx
