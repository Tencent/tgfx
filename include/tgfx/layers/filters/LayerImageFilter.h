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

#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

/**
 * LayerImageFilter is the base class for LayerFilters whose effect can be expressed as a single
 * ImageFilter that depends only on the scale factor. It owns the ImageFilter cache and exposes the
 * onCreateImageFilter() extension point. Subclasses include BlurFilter, DropShadowFilter,
 * InnerShadowFilter, ColorMatrixFilter, and BlendFilter.
 *
 * LayerImageFilter participates in Layer::getImageFilter() by overriding getImageFilter(scale) to
 * return its cached ImageFilter, which Layer composes for bounds reverse-mapping.
 */
class LayerImageFilter : public LayerFilter {
 public:
  /**
   * Returns the bounds of the filter after applying it to the scaled source bounds, using the
   * underlying ImageFilter's bounds calculation.
   */
  Rect filterBounds(const Rect& srcRect, float contentScale) override;

 protected:
  /**
   * Creates the ImageFilter that expresses this filter's effect at the given scale. Subclasses
   * must override this method. Returning nullptr indicates a no-op filter for the given scale.
   */
  virtual std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) = 0;

  /**
   * Applies the cached ImageFilter to the input image via FilterImage::MakeFrom. Subclasses
   * generally do not need to override this method.
   */
  std::shared_ptr<Image> onFilterImage(std::shared_ptr<Image> input, float scale,
                                       Point* offset) override;

  /**
   * Drops the cached ImageFilter and chains to LayerFilter::invalidateFilter().
   */
  void invalidateFilter() override;

  /**
   * Returns the cached ImageFilter for the given scale, building it via onCreateImageFilter() on
   * cache miss. Overrides LayerFilter::getImageFilter() so that Layer::getImageFilter() can pick
   * up this filter's contribution to the composed bounds-reverse-mapping ImageFilter.
   */
  std::shared_ptr<ImageFilter> getImageFilter(float scale) override;

 private:
  bool dirty = true;
  float lastScale = 1.0f;
  std::shared_ptr<ImageFilter> lastFilter;

  friend class Layer;
};

}  // namespace tgfx
