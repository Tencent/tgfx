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
    /** A plain image source, such as the background content below the layer. */
    Base,
    /** A contour source that additionally carries the shared contour's vector shape. */
    Contour,
    /** The layer's own geometry, without its descendants. Carries the vector shape only; the
     * matching raster is the content entry. */
    OwnShape
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

 protected:
  StyleInputSource(Type type, std::shared_ptr<Image> image, Point imageOffset)
      : _type(type), _image(std::move(image)), _imageOffset(imageOffset) {
  }

 private:
  Type _type = Type::Base;
  std::shared_ptr<Image> _image;
  Point _imageOffset = {};
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
 * The layer's own geometry. It is not a contour: it holds the layer's own painters without its
 * descendants, including painters with alpha=0 and the painters flagged silhouetteOnly. Only the
 * shape is carried here; the matching raster is the content entry that the style also receives.
 */
class OwnShapeInputSource : public StyleInputSource {
 public:
  /**
   * Creates an own-geometry source.
   * @param shape The optional vector shape of the layer's own geometry. It is absent when the
   * geometry cannot be expressed as a single StyledShape.
   */
  explicit OwnShapeInputSource(std::optional<StyledShape> shape)
      : StyleInputSource(Type::OwnShape, nullptr, Point::Zero()), _shape(std::move(shape)) {
  }

  /** Returns the optional vector shape of the layer's own geometry. */
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
  /**
   * The main raster this style resolves: the own recording when the style declares
   * LayerStyleExtraSourceType::OwnShape and the layer records one, otherwise the shared contour, then
   * the shared content. Opaque, as described by OpaqueContext.
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
   * Extra sources requested by LayerStyle::extraSourceType() flags. Each source is an independent
   * StyleInputSource, a ContourInputSource for contour sources, or an OwnShapeInputSource for the
   * layer's own geometry. Use findExtraSource() to retrieve a source by type.
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
