/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/MapDirection.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/RuntimeEffect.h"

namespace tgfx {
class TextureProxy;
enum class SrcRectConstraint;

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
   * @param color         The color of the drop shadow can overflow 0-1.
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
   * @param color         The color of the drop shadow can overflow 0-1.
   */
  static std::shared_ptr<ImageFilter> DropShadowOnly(float dx, float dy, float blurrinessX,
                                                     float blurrinessY, const Color& color);

  /**
   * Create a filter that draws an inner shadow over the input content. This filter produces an image
   * that includes the inputs' content.
   * @param dx            The X offset of the shadow.
   * @param dy            The Y offset of the shadow.
   * @param blurrinessX   The blur radius for the shadow, along the X axis.
   * @param blurrinessY   The blur radius for the shadow, along the Y axis.
   * @param color         The color of the inner shadow can overflow 0-1.
   */
  static std::shared_ptr<ImageFilter> InnerShadow(float dx, float dy, float blurrinessX,
                                                  float blurrinessY, const Color& color);

  /**
   * Create a filter that renders an inner shadow, in exactly the same manner as the InnerShadow(),
   * except that the resulting image does not include the input content.
   * @param dx            The X offset of the shadow.
   * @param dy            The Y offset of the shadow.
   * @param blurrinessX   The blur radius for the shadow, along the X axis.
   * @param blurrinessY   The blur radius for the shadow, along the Y axis.
   * @param color         The color of the inner shadow can overflow 0-1.
   */
  static std::shared_ptr<ImageFilter> InnerShadowOnly(float dx, float dy, float blurrinessX,
                                                      float blurrinessY, const Color& color);

  /**
   * Create a filter that applies the given color filter to the input image.
   */
  static std::shared_ptr<ImageFilter> ColorFilter(std::shared_ptr<ColorFilter> colorFilter);

  /**
   * Creates a filter that applies the given RuntimeEffect object to the input image.
   * You can use the shading language of the current GPU backend to create RuntimeEffect objects.
   */
  static std::shared_ptr<ImageFilter> Runtime(std::shared_ptr<RuntimeEffect> effect);

  /**
   * Creates a filter that applies a perspective transformation to the input image.
   * @param matrix The transformation matrix that maps vertices from the local coordinate system to
   * the destination coordinate system during 3D perspective transformation. The result of
   * multiplying this matrix with the vertex coordinates will undergo perspective division; the
   * resulting x and y components are the final projected coordinates. The valid range for the z
   * component is consistent with OpenGL's definition for the clipping space, which is [-1, 1]. Any
   * content with a z component outside this range will be clipped.
   * The default transformation anchor is at the top-left origin (0,0) of the source image,
   * user-defined anchors are included in the matrix.
   */
  static std::shared_ptr<ImageFilter> Transform3D(const Matrix3D& matrix);

  virtual ~ImageFilter() = default;

  /**
   * Returns the bounds of the image that will be produced by this filter when it is applied to an
   * image of the given bounds. MapDirection::Forward is used to determine which pixels of the
   * destination canvas a source image rect would touch after filtering. MapDirection::Reverse
   * is used to determine which rect of the source image would be required to fill the given
   * rect (typically, clip bounds).
   */
  Rect filterBounds(const Rect& rect, MapDirection mapDirection = MapDirection::Forward) const;

 protected:
  enum class Type { Blur, DropShadow, InnerShadow, Color, Compose, Runtime, Transform3D };

  /**
   * Returns the type of this image filter.
   */
  virtual Type type() const = 0;

  /**
   * Returns the bounds of the image that will be produced by this filter when it is applied to an
   * image of the given bounds.
   * MapDirection::Forward is used to determine which pixels of the destination canvas a source
   * image rect would touch after filtering. MapDirection::Reverse is used to determine which rect
   * of the source image would be required to fill the given rect.
   */
  virtual Rect onFilterBounds(const Rect& rect, MapDirection mapDirection) const;

  /**
   * Returns a texture proxy that applies this filter to the source image.
   * @param source The source image.
   * @param renderBounds Render bounds of the filtered image, relative to the source image.
   * @param args The arguments for creating the texture proxy.
   */
  virtual std::shared_ptr<TextureProxy> lockTextureProxy(std::shared_ptr<Image> source,
                                                         const Rect& renderBounds,
                                                         const TPArgs& args) const;

  /**
   * Returns a FragmentProcessor that applies this filter to the source image. The returned
   * processor is in the coordinate space of the source image.
   */
  virtual PlacementPtr<FragmentProcessor> asFragmentProcessor(std::shared_ptr<Image> source,
                                                              const FPArgs& args,
                                                              const SamplingOptions& sampling,
                                                              SrcRectConstraint constraint,
                                                              const Matrix* uvMatrix) const = 0;

  bool applyCropRect(const Rect& srcRect, Rect* dstRect, const Rect* clipBounds = nullptr) const;

  PlacementPtr<FragmentProcessor> makeFPFromTextureProxy(std::shared_ptr<Image> source,
                                                         const FPArgs& args,
                                                         const SamplingOptions& sampling,
                                                         SrcRectConstraint constraint,
                                                         const Matrix* uvMatrix) const;

  friend class DropShadowImageFilter;
  friend class InnerShadowImageFilter;
  friend class ComposeImageFilter;
  friend class FilterImage;
  friend class Types;
};

}  // namespace tgfx
