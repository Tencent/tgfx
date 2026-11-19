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

#pragma once

#include <cstdint>
#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

/**
 * Displaces rendered layer content with a deterministic procedural texture.
 *
 * Add with Layer::setFilters(). Sizes and radius are in layer-local units. Larger sizes produce
 * coarser grain. A zero size or zero radius makes the filter an identity operation.
 */
class TextureFilter : public LayerFilter {
 public:
  /**
   * @param sizeX Non-negative horizontal size; zero disables the texture.
   * @param sizeY Non-negative vertical size; zero disables the texture.
   * @param radius Non-negative displacement radius; zero disables the texture.
   * @param seed Texture pattern selector.
   * @param clipToShape Preserve the input alpha.
   * @return nullptr if any numeric parameter is negative, NaN, or infinite.
   */
  static std::shared_ptr<TextureFilter> Make(float sizeX, float sizeY, float radius,
                                             uint32_t seed = 0, bool clipToShape = true);

  // Numeric property setters ignore negative, NaN, or infinite values.
  float sizeX() const {
    return _sizeX;
  }

  void setSizeX(float value);

  float sizeY() const {
    return _sizeY;
  }

  void setSizeY(float value);

  float radius() const {
    return _radius;
  }

  void setRadius(float value);

  uint32_t seed() const {
    return _seed;
  }

  void setSeed(uint32_t value);

  bool clipToShape() const {
    return _clipToShape;
  }

  void setClipToShape(bool value);

  Rect filterBounds(const Rect& srcRect, float contentScale,
                    MapDirection direction = MapDirection::Forward) override;

 protected:
  Type type() const override {
    return Type::TextureFilter;
  }

  std::shared_ptr<Image> onFilterImage(std::shared_ptr<Image> input, float scale,
                                       const Rect& contentBounds, Point* offset) override;
  std::shared_ptr<Image> onFilterImage(std::shared_ptr<Image> input, float scale,
                                       const Rect& contentBounds, Point* offset,
                                       const Point& imageOrigin) override;

 private:
  TextureFilter(float sizeX, float sizeY, float radius, uint32_t seed, bool clipToShape);

  float _sizeX = 25.0f;
  float _sizeY = 25.0f;
  float _radius = 20.0f;
  uint32_t _seed = 0;
  bool _clipToShape = true;
};

}  // namespace tgfx
