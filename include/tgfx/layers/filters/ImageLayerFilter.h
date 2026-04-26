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

#include "tgfx/core/ImageFilter.h"
#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

/**
 * ImageLayerFilter is an intermediate base class for layer filters whose effect is fully expressed
 * by a single ImageFilter. Subclasses implement onCreateImageFilter(scale) and this class handles
 * the rest: caching the most recently built ImageFilter, applying it to the input image, and
 * computing bounds via ImageFilter::filterBounds.
 *
 * Filters that need a custom pixel pipeline (for example, one that cannot be expressed as a single
 * ImageFilter, or that needs to anchor transforms at the content center) should subclass
 * LayerFilter directly and implement onFilterImage() instead.
 */
class ImageLayerFilter : public LayerFilter {
 public:
  /**
   * Returns the cached ImageFilter for the given scale, creating it via onCreateImageFilter() when
   * necessary.
   */
  std::shared_ptr<ImageFilter> getImageFilter(float scale);

  Rect filterBounds(const Rect& srcRect, float contentScale) override;

  ImageLayerFilter* asImageLayerFilter() override {
    return this;
  }

 protected:
  /**
   * Creates a new image filter for the given scale factor. Called by getImageFilter() whenever the
   * cache is invalidated or the scale changes.
   */
  virtual std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) = 0;

  /**
   * Default implementation applies the ImageFilter from onCreateImageFilter() to the input image
   * via Image::makeWithFilter(). Ignores contentBounds because a single ImageFilter is
   * translation-invariant at the layer-filter level.
   */
  std::shared_ptr<Image> onFilterImage(std::shared_ptr<Image> input, const Rect& contentBounds,
                                       float scale, Point* offset) override;

  /**
   * Marks the cached filter as dirty so it will be rebuilt on the next access, and notifies the
   * owning layer that its transform may need updating.
   */
  void invalidateFilter();

 private:
  bool dirty = true;
  float lastScale = 1.0f;
  std::shared_ptr<ImageFilter> lastFilter;
};

}  // namespace tgfx
