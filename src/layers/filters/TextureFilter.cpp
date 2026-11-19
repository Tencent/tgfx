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
#include <algorithm>
#include <cmath>
#include "core/filters/TextureImageFilter.h"
#include "core/images/FilterImage.h"

namespace tgfx {

namespace {

constexpr float MaxTextureParameter = 100.0f;

float NormalizeParameter(float value) {
  return std::clamp(value, 0.0f, MaxTextureParameter);
}

}  // namespace

uint32_t TextureFilter::ToSVGSeed(uint32_t seed) {
  seed ^= seed >> 16;
  seed *= 0x7feb352dU;
  seed ^= seed >> 15;
  seed *= 0x846ca68bU;
  seed ^= seed >> 16;
  return 1U + seed % 16777215U;
}

const std::string& TextureFilter::SVGClipAlphaTable() {
  static const std::string table = [] {
    std::string result = "0";
    result.reserve(512);
    for (int index = 1; index < 255; index++) {
      result += " 1";
    }
    return result;
  }();
  return table;
}

std::shared_ptr<TextureFilter> TextureFilter::Make(float sizeX, float sizeY, float radius,
                                                   uint32_t seed, bool clipToShape) {
  if (!std::isfinite(sizeX) || !std::isfinite(sizeY) || !std::isfinite(radius)) {
    return nullptr;
  }
  return std::shared_ptr<TextureFilter>(
      new TextureFilter(NormalizeParameter(sizeX), NormalizeParameter(sizeY),
                        NormalizeParameter(radius), seed, clipToShape));
}

TextureFilter::TextureFilter(float sizeX, float sizeY, float radius, uint32_t seed,
                             bool clipToShape)
    : _sizeX(sizeX), _sizeY(sizeY), _radius(radius), _seed(seed), _clipToShape(clipToShape) {
}

void TextureFilter::setSizeX(float value) {
  if (!std::isfinite(value)) {
    return;
  }
  value = NormalizeParameter(value);
  if (_sizeX == value) return;
  _sizeX = value;
  invalidateFilter();
}

void TextureFilter::setSizeY(float value) {
  if (!std::isfinite(value)) {
    return;
  }
  value = NormalizeParameter(value);
  if (_sizeY == value) return;
  _sizeY = value;
  invalidateFilter();
}

void TextureFilter::setRadius(float value) {
  if (!std::isfinite(value)) {
    return;
  }
  value = NormalizeParameter(value);
  if (_radius == value) return;
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
  auto params = MakeTextureGrainParams(_sizeX, _sizeY, _radius, Point::Zero(), _seed, Point::Zero(),
                                       contentScale, _clipToShape);
  TextureImageFilter filter(params);
  return filter.filterBounds(srcRect, direction);
}

std::shared_ptr<Image> TextureFilter::onFilterImage(std::shared_ptr<Image> input, float scale,
                                                    const Rect& contentBounds, Point* offset) {
  return onFilterImage(std::move(input), scale, contentBounds, offset, Point::Zero());
}

std::shared_ptr<Image> TextureFilter::onFilterImage(std::shared_ptr<Image> input, float scale,
                                                    const Rect& contentBounds, Point* offset,
                                                    const Point& imageOrigin) {
  if (input == nullptr || !std::isfinite(scale) || scale <= 0.0f) {
    return input;
  }
  auto localBounds = Rect::MakeLTRB(contentBounds.left / scale, contentBounds.top / scale,
                                    contentBounds.right / scale, contentBounds.bottom / scale);
  auto center = Point::Make(localBounds.width() * 0.5f, localBounds.height() * 0.5f);
  auto params = MakeTextureGrainParams(_sizeX, _sizeY, _radius, center, _seed, imageOrigin, scale,
                                       _clipToShape);
  auto filter = std::make_shared<TextureImageFilter>(params);
  return FilterImage::MakeFrom(std::move(input), std::move(filter), offset, nullptr);
}

}  // namespace tgfx
