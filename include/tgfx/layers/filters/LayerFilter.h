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

#pragma once

#include "tgfx/core/ImageFilter.h"

namespace tgfx {
/**
 * LayerFilter represents a filter that applies effects to a layer, such as blurs, shadows, or color
 * adjustments. LayerFilters are mutable and can be changed at any time.
 */
class LayerFilter {
 public:
  virtual ~LayerFilter() = default;

  /**
   * Applies the filter to the given image, returning a new image with the filter applied.
   *
   * @param image The image to apply the filter to.
   * @param filterScale The scale factor to apply to the filter. This is used to adjust the filter
   * to the resolution of the image.
   * @param offset The offset stores the translation information for the filtered Image.
   * @return A new image with the filter applied.
   */
  std::shared_ptr<Image> applyFilter(const std::shared_ptr<Image>& image, float filterScale,
                                     Point* offset = nullptr);

  /**
   * Returns the bounds of the image that will be produced by this filter when it is applied to an
   * image of the given bounds.
   * @param bounds The bounds of the image to apply the filter to.
   * @param filterScale The scale factor to apply to the filter. This is used to adjust the filter
   */
  Rect filterBounds(const Rect& bounds, float filterScale = 1.0f);

  /**
   * Sets the clip bounds of the filter. The clip bounds are the bounds of the image that will be
   * produced by the filter. If the clip bounds are empty, the filter will produce the entire image.
   * @param clipBounds The clip bounds to set.
   */
  void setClipBounds(const Rect& clipBounds);

  /**
   * Returns the clip bounds of the filter.
   */
  Rect clipBounds() const {
    if (_clipBounds) {
      return *_clipBounds;
    } else {
      return Rect::MakeEmpty();
    }
  }

 protected:
  /**
   * Invalidates the filter, causing it to be re-computed the next time it is requested.
   */
  void invalidate();

  /**
   * Creates a new image filter for the given scale factor. When it is necessary to recreate the
   * ImageFilter, the onCreateImageFilter method will be called.
   * @param scale The scale factor to apply to the filter.
   * @return A new image filter.
   */
  virtual std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) = 0;

 private:
  std::shared_ptr<ImageFilter> getImageFilter(float scale);

  bool dirty = true;

  float lastScale = 1.0f;

  std::unique_ptr<Rect> _clipBounds = nullptr;

  std::shared_ptr<ImageFilter> lastFilter;
};
}  // namespace tgfx
