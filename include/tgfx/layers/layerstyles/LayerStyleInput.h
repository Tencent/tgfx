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
#include <vector>
#include "tgfx/core/Image.h"
#include "tgfx/core/Point.h"
#include "tgfx/layers/layerstyles/StyledShape.h"

namespace tgfx {

/**
 * Extra input source that a LayerStyle may need beyond the primary content image.
 */
class StyleInputSource {
 public:
  /**
   * The kind of extra source carried by this object.
   */
  enum class Type {
    /**
     * The background content below the layer.
     */
    Background,
    /**
     * The rasterized layer contour.
     */
    Contour,
    /**
     * The optional vector shape of the layer content.
     */
    Shape
  };

  StyleInputSource(std::shared_ptr<Image> image, Point imageOffset)
      : _type(Type::Background), _image(std::move(image)), _imageOffset(imageOffset) {
  }

  /**
   * The kind of this source.
   */
  Type type() const {
    return _type;
  }

  /**
   * The source image.
   */
  const std::shared_ptr<Image>& image() const {
    return _image;
  }

  /**
   * The offset of the source image relative to the content image.
   */
  Point imageOffset() const {
    return _imageOffset;
  }

  /**
   * Returns the optional vector shape carried by a Shape source. Other source types return nullopt.
   */
  const std::optional<StyledShape>& shape() const {
    return _shape;
  }

 protected:
  StyleInputSource(Type type, std::shared_ptr<Image> image, Point imageOffset,
                   std::optional<StyledShape> shape = std::nullopt)
      : _type(type), _image(std::move(image)), _imageOffset(imageOffset), _shape(std::move(shape)) {
  }

 private:
  Type _type = Type::Background;
  std::shared_ptr<Image> _image;
  Point _imageOffset = {};
  std::optional<StyledShape> _shape = std::nullopt;
};

/**
 * Rasterized contour input source.
 */
class ContourInputSource : public StyleInputSource {
 public:
  /**
   * Creates a contour source with its image and offset relative to the content image.
   */
  ContourInputSource(std::shared_ptr<Image> image, Point imageOffset)
      : StyleInputSource(Type::Contour, std::move(image), imageOffset) {
  }
};

/**
 * Optional vector shape input source. The source does not carry a rasterized image.
 */
class ShapeInputSource : public StyleInputSource {
 public:
  /**
   * Creates a shape source. A nullopt shape means the layer has no extractable vector shape.
   */
  explicit ShapeInputSource(std::optional<StyledShape> shape)
      : StyleInputSource(Type::Shape, nullptr, {}, std::move(shape)) {
  }
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
   * Extra sources requested by the LayerStyle's extraSourceType() flags.
   */
  std::vector<std::shared_ptr<StyleInputSource>> extraSources = {};

  /**
   * Returns the extra source with the specified type, or nullptr when it was not requested or
   * could not be generated.
   */
  const StyleInputSource* findExtraSource(StyleInputSource::Type type) const {
    for (const auto& source : extraSources) {
      if (source != nullptr && source->type() == type) {
        return source.get();
      }
    }
    return nullptr;
  }
};

}  // namespace tgfx
