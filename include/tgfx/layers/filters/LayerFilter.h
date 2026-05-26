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
#include "tgfx/core/MapDirection.h"
#include "tgfx/layers/LayerProperty.h"

namespace tgfx {
class Image;

/**
 * LayerFilter is the abstract base class for filters applied to a layer's rendered image, such as
 * blurs, shadows, color adjustments, or procedural overlays. A LayerFilter consumes the rendered
 * layer image and produces a new image. LayerFilters are mutable and can be changed at any time.
 */
class LayerFilter : public LayerProperty {
 public:
  /**
   * Applies this filter to the given input image at the specified scale factor.
   * @param input The source image to filter.
   * @param scale The scale factor to apply to scale-dependent filter parameters.
   * @param contentBounds The layer content bounds, expressed in the input image coordinate space
   * (the input image origin is the coordinate origin). Filters whose effect is anchored to the
   * layer geometry use this rectangle to recover the anchor regardless of how the input image is
   * clipped relative to the content bounds.
   * @param clipBounds Optional clip rectangle in the input image coordinate space. When provided,
   * the filter output is restricted to the pixels visible inside this rectangle, enabling backends
   * to skip processing for off-screen regions (e.g. large-radius blurs).
   * @param offset If non-null, receives the (x, y) translation of the filtered image relative to
   * the input image origin.
   * @return The filtered image, or nullptr on failure.
   */
  std::shared_ptr<Image> filterImage(std::shared_ptr<Image> input, float scale,
                                     const Rect& contentBounds, const Rect* clipBounds = nullptr,
                                     Point* offset = nullptr);

  /**
   * Returns the bounds of the layer filter after applying it to the scaled layer bounds.
   * @param srcRect The scaled bounds of the layer content.
   * @param contentScale The scale factor of the layer bounds relative to its original size. Some
   * layer filters have size-related parameters that must be adjusted with this scale factor.
   * @param direction Forward computes the destination bounds produced by filtering a source rect.
   * Reverse computes the source rect needed to fill a destination rect (typically a clip rect).
   * @return The bounds of the layer filter.
   */
  virtual Rect filterBounds(const Rect& srcRect, float contentScale,
                            MapDirection direction = MapDirection::Forward);

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
   * Subclasses must override this method to produce the filtered image. See filterImage() for
   * parameter semantics, including the optional clipBounds rectangle in the input image
   * coordinate space.
   */
  virtual std::shared_ptr<Image> onFilterImage(std::shared_ptr<Image> input, float scale,
                                               const Rect& contentBounds, const Rect* clipBounds,
                                               Point* offset) = 0;

  /**
   * Marks this filter as dirty. Subclasses that maintain cached state should override this method
   * to drop their caches, then call the base implementation.
   */
  virtual void invalidateFilter();

  friend class Types;
};
}  // namespace tgfx
