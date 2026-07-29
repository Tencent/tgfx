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
 * Contour image, offset and optional vector shape carried by a BackgroundAndContour source.
 */
struct ContourData {
  std::shared_ptr<Image> image = nullptr;
  Point imageOffset = {};
  std::optional<StyledShape> shape = std::nullopt;
};

/**
 * Extra input source that a LayerStyle may need beyond the primary content image.
 */
class StyleInputSource {
 public:
  enum class Type {
    /** A plain image source, such as the background content below the layer. */
    Base,
    /** A contour source that additionally carries the layer's vector shape. */
    Contour,
    /** A background image and an optional contour source. */
    BackgroundAndContour
  };

  /**
   * Creates a plain image source.
   * @param image The source image.
   * @param imageOffset The source image offset relative to the content image.
   */
  StyleInputSource(std::shared_ptr<Image> image, Point imageOffset)
      : _type(Type::Base), _image(std::move(image)), _imageOffset(imageOffset) {
  }

  /** Returns the source kind. */
  Type type() const {
    return _type;
  }

  /** Returns the primary source image. */
  const std::shared_ptr<Image>& image() const {
    return _image;
  }

  /** Returns the primary source image offset relative to the content image. */
  Point imageOffset() const {
    return _imageOffset;
  }

  /**
   * Creates a source carrying a background image and optional contour data.
   * @param backgroundImage The captured background image.
   * @param backgroundOffset The background image offset relative to the content image.
   * @param contourImage The rasterized contour image, or nullptr when unavailable.
   * @param contourOffset The contour image offset relative to the content image.
   * @param shape The optional vector shape corresponding to the contour.
   */
  static std::shared_ptr<StyleInputSource> MakeBackgroundAndContour(
      std::shared_ptr<Image> backgroundImage, Point backgroundOffset,
      std::shared_ptr<Image> contourImage, Point contourOffset,
      std::optional<StyledShape> shape = std::nullopt) {
    ContourData data = {std::move(contourImage), contourOffset, std::move(shape)};
    return std::shared_ptr<StyleInputSource>(
        new StyleInputSource(std::move(backgroundImage), backgroundOffset, std::move(data)));
  }

  /**
   * Returns contour data when type() is BackgroundAndContour; otherwise returns nullopt.
   */
  const std::optional<ContourData>& contour() const {
    return _contour;
  }

 protected:
  StyleInputSource(Type type, std::shared_ptr<Image> image, Point imageOffset)
      : _type(type), _image(std::move(image)), _imageOffset(imageOffset) {
  }

 private:
  StyleInputSource(std::shared_ptr<Image> image, Point imageOffset, ContourData contour)
      : _type(Type::BackgroundAndContour), _image(std::move(image)), _imageOffset(imageOffset),
        _contour(std::move(contour)) {
  }

  Type _type = Type::Base;
  std::shared_ptr<Image> _image;
  Point _imageOffset = {};
  std::optional<ContourData> _contour = std::nullopt;
};

/**
 * Contour input source with additional vector shape information.
 */
class ContourInputSource : public StyleInputSource {
 public:
  /**
   * Creates a contour source.
   * @param image The rasterized contour image.
   * @param imageOffset The contour image offset relative to the content image.
   * @param shape The optional vector shape corresponding to the contour.
   */
  ContourInputSource(std::shared_ptr<Image> image, Point imageOffset,
                     std::optional<StyledShape> shape = std::nullopt)
      : StyleInputSource(Type::Contour, std::move(image), imageOffset), _shape(std::move(shape)) {
  }

  /** Returns the optional vector shape corresponding to the contour. */
  const std::optional<StyledShape>& shape() const {
    return _shape;
  }

 private:
  std::optional<StyledShape> _shape = std::nullopt;
};

/**
 * Contains the source data that a LayerStyle needs to perform its drawing.
 */
struct LayerStyleInput {
  /** The opaque layer content image. */
  std::shared_ptr<Image> content = nullptr;

  /** The offset of the content image's top-left corner in scaled pixel coordinates. */
  Point contentOffset = {};

  /** The scale factor of the layer content relative to its original size. */
  float contentScale = 1.0f;

  /**
   * Extra source requested by LayerStyle::extraSourceType(). Contour styles receive a
   * ContourInputSource. BackgroundAndContour styles receive a StyleInputSource whose primary image
   * is the background and whose contour() contains the optional contour data.
   */
  std::shared_ptr<StyleInputSource> extraSource = nullptr;
};

}  // namespace tgfx
