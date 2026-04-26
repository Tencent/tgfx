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

#include "tgfx/core/Rect.h"
#include "tgfx/layers/LayerProperty.h"

namespace tgfx {
class Image;
class ImageLayerFilter;

/**
 * LayerFilter represents a filter that applies effects to a layer, such as blurs, shadows, or color
 * adjustments. Subclasses implement their effect as an image-in / image-out transform via
 * onFilterImage(). Filters that can be expressed as a single ImageFilter should prefer subclassing
 * ImageLayerFilter, which implements onFilterImage() on top of onCreateImageFilter() with caching.
 *
 * The contentBounds parameter passed to filterImage() / onFilterImage() describes the layer
 * content region in layer-local coordinates (unscaled). Filters that need a stable anchor across
 * layer size changes should use contentBounds.center() as the origin of their internal transforms,
 * multiplying by the scale factor when they need pixel-space coordinates.
 *
 * LayerFilters are mutable and can be changed at any time.
 */
class LayerFilter : public LayerProperty {
 public:
  /**
   * Applies this filter to the given input image. The offset, if non-null, receives the (x, y)
   * translation of the filtered image relative to the input image origin.
   * @param input         The source image to filter.
   * @param contentBounds The layer content region in layer-local coordinates (unscaled).
   * @param scale         The scale factor applied to scale-dependent filter parameters.
   * @param offset        If non-null, receives the translation of the filtered image relative to
   *                      the input image origin.
   * @return The filtered image, or nullptr on failure.
   */
  std::shared_ptr<Image> filterImage(std::shared_ptr<Image> input, const Rect& contentBounds,
                                     float scale, Point* offset = nullptr);

  /**
   * Returns the bounds of the layer filter after applying it to the scaled layer bounds. The
   * default implementation returns srcRect unchanged; subclasses whose effect changes the bounds
   * should override this.
   * @param srcRect The scaled bounds of the layer content.
   * @param contentScale The scale factor of the layer bounds relative to its original size.
   */
  virtual Rect filterBounds(const Rect& srcRect, float contentScale);

  /**
   * Returns this filter cast to ImageLayerFilter if it is one, else nullptr. Used by Layer to
   * compose an aggregate ImageFilter for paint-time effects (blur background, filter-bounds
   * reverse mapping) from the subset of filters that can be expressed as a single ImageFilter.
   */
  virtual ImageLayerFilter* asImageLayerFilter() {
    return nullptr;
  }

 protected:
  enum class Type {
    LayerFilter,
    BlendFilter,
    BlurFilter,
    ColorMatrixFilter,
    DropShadowFilter,
    InnerShadowFilter,
    NoiseFilter
  };

  virtual Type type() const {
    return Type::LayerFilter;
  }

  /**
   * Applies this filter to the input image. Subclasses implement their effect here.
   * @param input         The source image to filter. Never null.
   * @param contentBounds The layer content region in layer-local coordinates (unscaled).
   * @param scale         The scale factor applied to scale-dependent filter parameters.
   * @param offset        If non-null, receives the translation of the filtered image relative to
   *                      the input image origin.
   */
  virtual std::shared_ptr<Image> onFilterImage(std::shared_ptr<Image> input,
                                               const Rect& contentBounds, float scale,
                                               Point* offset) = 0;

 private:
  friend class Layer;
  friend class Types;
};
}  // namespace tgfx
