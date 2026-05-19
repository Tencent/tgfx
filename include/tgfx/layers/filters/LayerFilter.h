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
 * LayerFilter is the abstract base class for filters that apply effects to a layer, such as blurs,
 * shadows, color adjustments, or procedural overlays. A LayerFilter consumes the rendered layer
 * image and produces a new image. LayerFilters are mutable and can be changed at any time.
 *
 * Subclasses fall into two broad categories:
 *   - Effects expressible as a single ImageFilter (blur, drop shadow, inner shadow, color matrix,
 *     blend) should derive from LayerImageFilter, which provides ImageFilter caching and overrides
 *     getImageFilter() used by Layer::getImageFilter() for bounds reverse-mapping.
 *   - Effects that need access to the input image (such as procedural overlays anchored to image
 *     dimensions) should derive directly from LayerFilter and override onFilterImage().
 */
class LayerFilter : public LayerProperty {
 public:
  /**
   * Applies this filter to the given input image at the specified scale factor. The offset stores
   * the translation of the filtered image relative to the input image origin.
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
   * Applies this filter to the given input image, providing the geometry of the layer content
   * bounds the input image was rendered from. Filters whose effect depends on the layer geometry
   * (such as procedural overlays anchored to content bounds center) use this overload to recover
   * the anchor regardless of how the input image is clipped relative to the content bounds. The
   * default implementation ignores the geometry and forwards to the basic filterImage().
   * @param input The source image to filter.
   * @param scale The scale factor to apply to scale-dependent filter parameters.
   * @param width The width of the layer content bounds in layer-local units.
   * @param height The height of the layer content bounds in layer-local units.
   * @param originOffset The (x, y) offset of the input image origin relative to the content bounds
   *                     origin, in layer-local units.
   * @param offset If non-null, receives the (x, y) translation of the filtered image.
   * @return The filtered image, or nullptr on failure.
   */
  std::shared_ptr<Image> filterImage(std::shared_ptr<Image> input, float scale, float width,
                                     float height, const Point& originOffset,
                                     Point* offset = nullptr);

  /**
   * Returns the bounds of the layer filter after applying it to the scaled layer bounds. The
   * default implementation returns srcRect unchanged. Subclasses whose effect grows or shrinks
   * the visible region must override this method.
   * @param srcRect The scaled bounds of the layer content.
   * @param contentScale The scale factor of the layer bounds relative to its original size. Some
   * layer filters have size-related parameters that must be adjusted with this scale factor.
   * @return The bounds of the layer filter.
   */
  virtual Rect filterBounds(const Rect& srcRect, float contentScale);

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
   * Applies this filter to the given input image. The default implementation returns the input
   * unchanged. Subclasses whose effect depends only on the input image (not on the layer content
   * bounds) should override this method; the default LayerFilter::onFilterImage(5-arg) overload
   * forwards to this method when subclasses do not override the 5-arg version.
   */
  virtual std::shared_ptr<Image> onFilterImage(std::shared_ptr<Image> input, float, Point*) {
    return input;
  }

  /**
   * Applies this filter to the given input image with knowledge of the layer content bounds. The
   * default implementation ignores the geometry parameters and forwards to onFilterImage().
   * Subclasses whose effect is anchored to the content bounds (rather than the input image)
   * should override this method.
   */
  virtual std::shared_ptr<Image> onFilterImage(std::shared_ptr<Image> input, float scale,
                                               float width, float height,
                                               const Point& originOffset, Point* offset);

  /**
   * Marks this filter as dirty. Subclasses that maintain cached state derived from the filter's
   * properties should override this method to drop their caches, then call the base implementation.
   */
  virtual void invalidateFilter();

  /**
   * Returns an ImageFilter that approximates this filter's effect at the given scale, used by
   * Layer::getImageFilter() for bounds reverse-mapping. Subclasses that cannot be expressed as a
   * single ImageFilter (or whose effect depends on input image geometry not yet known) may return
   * a degraded equivalent sufficient for bounds calculation, or nullptr if the filter does not
   * grow or shrink the visible region. The default implementation returns nullptr.
   */
  virtual std::shared_ptr<ImageFilter> getImageFilter(float scale);

  /**
   * Returns the cached ImageFilter for the given scale, creating it via onCreateImageFilter() if
   * needed. Available for subclasses that override onFilterImage() and need the internal ImageFilter.
   */
  std::shared_ptr<ImageFilter> getImageFilter(float scale);

 private:
  std::unique_ptr<Rect> _clipBounds = nullptr;

  friend class Layer;
  friend class Types;
};
}  // namespace tgfx
