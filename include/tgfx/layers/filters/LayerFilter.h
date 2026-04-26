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
 *
 * Subclasses express their effect via one of two hooks:
 *   1. Override onCreateImageFilter() to return a single ImageFilter. The default onFilterImage()
 *      applies it to the input image directly.
 *   2. Override onFilterImage() for effects that need a custom pixel pipeline or that depend on
 *      the content bounds (for example, to anchor a shader at the content center).
 *
 * The contentBounds parameter passed to filterImage() / onFilterImage() describes the location of
 * the layer content inside the input image, expressed in the input image's pixel coordinates.
 * Filters that need a stable anchor across layer size changes should use contentBounds.center()
 * as the origin of their internal transforms.
 */
class LayerFilter : public LayerProperty {
 public:
  /**
   * Applies this filter to the given input image. The contentBounds describe the region of the
   * input image that represents the original layer content, expressed in the input image's pixel
   * coordinates. The offset, if non-null, receives the (x, y) translation of the filtered image
   * relative to the input image origin.
   * @param input         The source image to filter.
   * @param contentBounds The content region inside the input image, in input image pixels. Used by
   *                      subclasses to anchor internal transforms at the content center.
   * @param scale         The scale factor applied to scale-dependent filter parameters.
   * @param offset        If non-null, receives the translation of the filtered image relative to
   *                      the input image origin.
   * @return The filtered image, or nullptr on failure.
   */
  std::shared_ptr<Image> filterImage(std::shared_ptr<Image> input, const Rect& contentBounds,
                                     float scale, Point* offset = nullptr);

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
   * Creates a new image filter for the given scale factor. Subclasses whose effect can be expressed
   * as a single ImageFilter should override this method. The default implementation returns
   * nullptr, which means the subclass must instead override onFilterImage().
   */
  virtual std::shared_ptr<ImageFilter> onCreateImageFilter(float scale);

  /**
   * Applies this filter to the input image. Subclasses that need a custom pixel pipeline beyond a
   * single ImageFilter, or that need to anchor transforms at the content center, should override
   * this method. The default implementation applies the ImageFilter from onCreateImageFilter() via
   * Image::makeWithFilter(). The base implementation ignores contentBounds because a single
   * ImageFilter is translation-invariant.
   */
  virtual std::shared_ptr<Image> onFilterImage(std::shared_ptr<Image> input,
                                               const Rect& contentBounds, float scale,
                                               Point* offset);

  /**
   * Marks the filter as dirty and invalidates the cached filter.
   */
  void invalidateFilter();

  /**
   * Returns the cached ImageFilter for the given scale, creating it via onCreateImageFilter() if
   * necessary. Available for subclasses that override onFilterImage() and still need the internal
   * ImageFilter.
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
