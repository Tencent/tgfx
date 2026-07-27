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

#include <memory>
#include <optional>
#include "tgfx/core/Image.h"
#include "tgfx/core/Point.h"
#include "tgfx/layers/layerstyles/StyledShape.h"

namespace tgfx {

/**
 * A background image source with its offset relative to the content image.
 */
struct StyleInputImage {
  std::shared_ptr<Image> image = nullptr;
  Point imageOffset = {};
};

/**
 * A contour image source with its offset and optional vector shape information.
 */
struct StyleInputContour {
  std::shared_ptr<Image> image = nullptr;
  Point imageOffset = {};
  std::optional<StyledShape> shape = std::nullopt;
};

/**
 * Extra input source that a LayerStyle may need beyond the primary content image.
 * May carry a background image, a contour image, or both, depending on the
 * LayerStyle's extraSourceType().
 */
class StyleInputSource {
 public:
  /**
   * Creates a source carrying only a background image.
   */
  static std::shared_ptr<StyleInputSource> MakeBackground(std::shared_ptr<Image> image,
                                                          Point imageOffset) {
    StyleInputImage bg = {std::move(image), imageOffset};
    return std::shared_ptr<StyleInputSource>(new StyleInputSource(std::move(bg), std::nullopt));
  }

  /**
   * Creates a source carrying only a contour image with optional vector shape.
   */
  static std::shared_ptr<StyleInputSource> MakeContour(
      std::shared_ptr<Image> image, Point imageOffset,
      std::optional<StyledShape> shape = std::nullopt) {
    StyleInputContour ct = {std::move(image), imageOffset, std::move(shape)};
    return std::shared_ptr<StyleInputSource>(new StyleInputSource(std::nullopt, std::move(ct)));
  }

  /**
   * Creates a source carrying both a background image and a contour image.
   */
  static std::shared_ptr<StyleInputSource> MakeBackgroundAndContour(
      std::shared_ptr<Image> backgroundImage, Point backgroundOffset,
      std::shared_ptr<Image> contourImage, Point contourOffset,
      std::optional<StyledShape> shape = std::nullopt) {
    StyleInputImage bg = {std::move(backgroundImage), backgroundOffset};
    StyleInputContour ct = {std::move(contourImage), contourOffset, std::move(shape)};
    return std::shared_ptr<StyleInputSource>(new StyleInputSource(std::move(bg), std::move(ct)));
  }

  /**
   * Returns the optional background image source.
   */
  const std::optional<StyleInputImage>& background() const {
    return _background;
  }

  /**
   * Returns the optional contour image source with vector shape.
   */
  const std::optional<StyleInputContour>& contour() const {
    return _contour;
  }

 private:
  StyleInputSource(std::optional<StyleInputImage> background,
                   std::optional<StyleInputContour> contour)
      : _background(std::move(background)), _contour(std::move(contour)) {
  }

  std::optional<StyleInputImage> _background = std::nullopt;
  std::optional<StyleInputContour> _contour = std::nullopt;
};

/**
 * Contains the source data that a LayerStyle needs to perform its drawing.
 */
struct LayerStyleInput {
  /**
   * The opaque layer content image. Rendered with normal fills, then all semi-transparent pixels
   * are converted to fully opaque (fully transparent pixels are preserved).
   */
  std::shared_ptr<Image> content = nullptr;

  /**
   * The offset of the content image's top-left corner in pixel coordinates (already scaled by
   * contentScale). Styles that need a stable sampling origin use this to anchor their pattern.
   */
  Point contentOffset = {};

  /**
   * The scale factor of the layer content relative to its original size. Some layer styles have
   * size-related parameters that must be adjusted with this scale factor.
   */
  float contentScale = 1.0f;

  /**
   * Optional extra source carrying background and/or contour images depending on the LayerStyle's
   * extraSourceType(). nullptr when the style requires no extra source. For Contour the image is
   * similar to content, but includes geometries from alpha=0 painters and replaces gradient fills
   * with solid colors. For Background the image is the normally rendered content below the current
   * layer. For BackgroundAndContour both background() and contour() are populated.
   */
  std::shared_ptr<StyleInputSource> extraSource = nullptr;
};

}  // namespace tgfx
