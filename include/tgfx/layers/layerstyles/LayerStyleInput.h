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
 * A recording a LayerStyle may consume, as its primary content or as an extra source.
 */
class StyleInputSource {
 public:
  /**
   * The kind of recording carried by this object.
   */
  enum class Type {
    /** A plain image recording, such as the shared content or the background below the layer. */
    Base,
    /** The layer's geometry, recorded on its own, without its descendants. */
    Geometry,
    /** The shared subtree contour, used when the layer records no geometry of its own. */
    Contour
  };

  /**
   * Creates a plain image source.
   * @param image The source image.
   * @param imageOffset The top-left corner of the source image, in the frame its role implies. See
   *                    imageOffset().
   */
  StyleInputSource(std::shared_ptr<Image> image, Point imageOffset)
      : _type(Type::Base), _image(std::move(image)), _imageOffset(imageOffset) {
  }

  virtual ~StyleInputSource() = default;

  /** Returns the recording kind. */
  Type type() const {
    return _type;
  }

  /** Returns the primary source image. */
  const std::shared_ptr<Image>& image() const {
    return _image;
  }

  /**
   * Returns the top-left corner of the source image: the layer-local origin times contentScale as
   * the primary content, relative to that content image as an extra source, both in scaled pixels.
   */
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
 * Input source for the layer's geometry: the raster and the exact shape read back from the very
 * same recording, so the shape always describes the pixels the style samples.
 */
class GeometryInputSource : public StyleInputSource {
 public:
  /**
   * @param image The rasterized geometry; geometry is only ever the primary content.
   * @param imageOffset Its top-left corner in layer-local coordinates times contentScale.
   * @param shape The vector shape of the same recording, or empty when it has none.
   */
  GeometryInputSource(std::shared_ptr<Image> image, Point imageOffset,
                      std::optional<StyledShape> shape)
      : StyleInputSource(Type::Geometry, std::move(image), imageOffset), _shape(std::move(shape)) {
  }

  /** Returns the vector shape of the same recording, empty when the recording has none. */
  const std::optional<StyledShape>& shape() const {
    return _shape;
  }

 private:
  std::optional<StyledShape> _shape = std::nullopt;
};

/**
 * Contour input source that additionally carries the content shape used by the generic spread
 * fallback. That shape is derived from the shared content, so it does not describe this contour
 * raster; it has no shape(), and only contentShape() reports it (a legacy pairing kept separate).
 */
class ContourInputSource : public StyleInputSource {
 public:
  /**
   * @param image The rasterized contour image.
   * @param imageOffset The contour image's top-left corner; see imageOffset() for its frame.
   * @param contentShape The content shape this contour's spread fallback was derived from.
   */
  ContourInputSource(std::shared_ptr<Image> image, Point imageOffset,
                     std::optional<StyledShape> contentShape = std::nullopt)
      : StyleInputSource(Type::Contour, std::move(image), imageOffset),
        _contentShape(std::move(contentShape)) {
  }

  /** Returns the content shape of the shared content, not the shape of this contour raster. */
  const std::optional<StyledShape>& contentShape() const {
    return _contentShape;
  }

 private:
  std::optional<StyledShape> _contentShape = std::nullopt;
};

/**
 * Contains the source data that a LayerStyle needs to perform its drawing.
 */
struct LayerStyleInput {
  /**
   * The recording this style resolves: the layer's geometry, the shared contour standing in for a
   * missing geometry, or the shared content. type() reports which one was recorded, and shape()
   * exists only on GeometryInputSource, so a consumer that wants the vector outline tests for
   * Type::Geometry and casts. nullptr means nothing to draw, so skip the style.
   */
  std::shared_ptr<StyleInputSource> content = nullptr;

  /**
   * The scale factor of the layer content relative to its original size. Some layer styles have
   * size-related parameters that must be adjusted with this scale factor.
   */
  float contentScale = 1.0f;

  /**
   * Extra sources requested by LayerStyle::extraSourceType() flags. Each source is an independent
   * StyleInputSource, or a ContourInputSource for the shared contour. Use findExtraSource() to
   * retrieve a source by type.
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
