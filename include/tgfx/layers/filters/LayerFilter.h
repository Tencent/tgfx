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

#pragma once

#include "tgfx/core/ImageFilter.h"
#include "tgfx/layers/LayerProperty.h"

namespace tgfx {
class Image;

/**
 * LayerFilter represents a filter that applies effects to a layer, such as blurs, shadows, or color
 * adjustments. It creates a new offscreen image that replaces the original layer content.
 * LayerFilters are mutable and can be changed at any time.
 */
class LayerFilter : public LayerProperty {
 public:
  /**
   * Applies this filter to the given input image at the specified scale factor. The offset stores
   * the translation of the filtered image relative to the input image origin. Subclasses that need
   * custom rendering should override onFilterImage().
   * @param input The source image to filter.
   * @param scale The scale factor to apply to scale-dependent filter parameters.
   * @param offset If non-null, receives the (x, y) translation of the filtered image.
   * @return The filtered image, or nullptr on failure.
   */
  std::shared_ptr<Image> filterImage(std::shared_ptr<Image> input, float scale,
                                     Point* offset = nullptr);

  /**
   * Returns the bounds of the layer filter after applying it to the scaled layer bounds.
   * @param srcRect The scaled bounds of the layer content.
   * @param contentScale The scale factor of the layer bounds relative to its original size.
   * Some layer filters have size-related parameters that must be adjusted with this scale factor.
   * @return The bounds of the layer filter.
   */
  Rect filterBounds(const Rect& srcRect, float contentScale);

 protected:
  enum class Type {
    LayerFilter,
    BlendFilter,
    BlurFilter,
    ColorMatrixFilter,
    DropShadowFilter,
    InnerShadowFilter
  };

  virtual Type type() const {
    return Type::LayerFilter;
  }

  /**
   * Creates a new image filter for the given scale factor. Subclasses that can express their effect
   * as a single ImageFilter should override this method. The default implementation returns nullptr.
   */
  virtual std::shared_ptr<ImageFilter> onCreateImageFilter(float scale);

  /**
   * Applies this filter to the given input image. Subclasses that need custom rendering beyond
   * what a single ImageFilter can express should override this method. The default implementation
   * applies the ImageFilter from onCreateImageFilter() to the input image via FilterImage::MakeFrom.
   */
  virtual std::shared_ptr<Image> onFilterImage(std::shared_ptr<Image> input, float scale,
                                               Point* offset);

  /**
   * Marks the filter as dirty and invalidates the cached filter.
   */
  void invalidateFilter();

  /**
   * Returns the cached ImageFilter for the given scale, creating it via onCreateImageFilter() if
   * needed. Available for subclasses that override onFilterImage() and need the internal ImageFilter.
   */
  std::shared_ptr<ImageFilter> getImageFilter(float scale);

 private:
  bool dirty = true;
  float lastScale = 1.0f;
  std::unique_ptr<Rect> _clipBounds = nullptr;
  std::shared_ptr<ImageFilter> lastFilter;

  friend class Layer;
  friend class Types;
};
}  // namespace tgfx
