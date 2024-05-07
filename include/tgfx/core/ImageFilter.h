/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
/**
 * ImageFilter is the base class for all image filters. If one is installed in the Paint, then all
 * drawings occur as usual, but they are as if the drawings happened into an offscreen (before the
 * blend mode is applied). This offscreen image will then be handed to the image filter, which in
 * turn creates a new image which is what will finally be drawn to the device (using the original
 * blend mode).
 */
class ImageFilter {
 public:
  /**
   * Creates a filter that applies the inner filter and then applies the outer filter.
   */
  static std::shared_ptr<ImageFilter> Compose(std::shared_ptr<ImageFilter> inner,
                                              std::shared_ptr<ImageFilter> outer);

  /**
   * Creates a filter that applies the filters in the order they are provided.
   */
  static std::shared_ptr<ImageFilter> Compose(std::vector<std::shared_ptr<ImageFilter>> filters);

  /**
   * Create a filter that blurs its input by the separate X and Y blurriness. The provided tile mode
   * is used when the blur kernel goes outside the input image.
   * @param blurrinessX  The Gaussian sigma value for blurring along the X axis.
   * @param blurrinessY  The Gaussian sigma value for blurring along the Y axis.
   * @param tileMode     The tile mode applied at edges.
   */
  static std::shared_ptr<ImageFilter> Blur(float blurrinessX, float blurrinessY,
                                           TileMode tileMode = TileMode::Decal);

  /**
   * Create a filter that draws a drop shadow under the input content. This filter produces an image
   * that includes the inputs' content.
   * @param dx            The X offset of the shadow.
   * @param dy            The Y offset of the shadow.
   * @param blurrinessX   The blur radius for the shadow, along the X axis.
   * @param blurrinessY   The blur radius for the shadow, along the Y axis.
   * @param color         The color of the drop shadow.
   */
  static std::shared_ptr<ImageFilter> DropShadow(float dx, float dy, float blurrinessX,
                                                 float blurrinessY, const Color& color);

  /**
   * Create a filter that renders a drop shadow, in exactly the same manner as the DropShadow(),
   * except that the resulting image does not include the input content.
   * @param dx            The X offset of the shadow.
   * @param dy            The Y offset of the shadow.
   * @param blurrinessX   The blur radius for the shadow, along the X axis.
   * @param blurrinessY   The blur radius for the shadow, along the Y axis.
   * @param color         The color of the drop shadow.
   */
  static std::shared_ptr<ImageFilter> DropShadowOnly(float dx, float dy, float blurrinessX,
                                                     float blurrinessY, const Color& color);

  virtual ~ImageFilter() = default;

  /**
   * Returns the bounds of the image that will be produced by this filter when it is applied to an
   * image of the given bounds.
   */
  Rect filterBounds(const Rect& rect) const;

 protected:
  /**
   * Returns the bounds of the image that will be produced by this filter when it is applied to an
   * image of the given bounds.
   */
  virtual Rect onFilterBounds(const Rect& srcRect) const;

  /**
   * The returned processor is in the coordinate space of the source image.
   */
  virtual std::unique_ptr<FragmentProcessor> onFilterImage(std::shared_ptr<Image> source,
                                                           const FPArgs& args,
                                                           const SamplingOptions& sampling,
                                                           const Matrix* localMatrix) const = 0;

  /**
   * Returns true if this filter is a ComposeImageFilter.
   */
  virtual bool isComposeFilter() const {
    return false;
  }

  bool applyCropRect(const Rect& srcRect, Rect* dstRect, const Rect* clipBounds = nullptr) const;

  friend class DropShadowImageFilter;
  friend class ComposeImageFilter;
  friend class FilterImage;
};
}  // namespace tgfx
