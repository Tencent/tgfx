/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include <string>
#include "tgfx/core/FontMetrics.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/PathProvider.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
/**
 * CustomTypefaceBuilder is the base class for creating custom typefaces.
 */
class CustomTypefaceBuilder {
 public:
  virtual ~CustomTypefaceBuilder() = default;

  /**
   * Sets the font name and style for the typeface.
   */
  void setFontName(const std::string& fontFamily, const std::string& fontStyle);

  /**
   * Sets the font metrics for the typeface.
   */
  void setMetrics(const FontMetrics& metrics);

  /**
   * Detaches the typeface being built. After this call, the builder remains valid and can be used
   * to add more glyphs, but the returned typeface is no longer linked to this builder. Any later
   * detached typeface will include glyphs from previous detachments. You can safely release the
   * previously detached typeface and use the new one for rendering. All glyphs added to the same
   * typeface builder share internal caches during rendering.
   */
  virtual std::shared_ptr<Typeface> detach() const = 0;

 protected:
  std::string _fontFamily;
  std::string _fontStyle;
  FontMetrics _fontMetrics = {};
  uint32_t uniqueID = 0;
  Rect fontBounds = {};
};

/**
 * PathTypefaceBuilder is a CustomTypefaceBuilder that lets you add glyphs defined by vector paths.
 * Use it to create typefaces from custom vector shapes. The resulting typefaces are render-only and
 * contain just the information needed to display glyphs.
 */
class PathTypefaceBuilder : public CustomTypefaceBuilder {
 public:
  /**
   * Adds a glyph to the typeface using a vector path. Returns the GlyphID of the new glyph, which
   * is a unique identifier within the typeface, starting from 1. Returns 0 if the glyph cannot be
   * added because the typeface builder is full.
   */
  GlyphID addGlyph(const Path& path);

  /**
   * Adds a glyph to the typeface using a PathProvider. The PathProvider is expected to provide the
   * path for the glyph when requested. It may be called from any thread, so it must be thread-safe
   * and immutable after creation. Returns the GlyphID of the new glyph, which is a unique
   * identifier within the typeface, starting from 1. Returns 0 if the glyph cannot be added because
   * the typeface builder is full.
   */
  GlyphID addGlyph(std::shared_ptr<PathProvider> provider);

  /**
   * Detaches the typeface being built. After this call, the builder remains valid and can be used
   * to add more glyphs, but the returned typeface is no longer linked to this builder. Any later
   * detached typeface will include glyphs from previous detachments. You can safely release the
   * previously detached typeface and use the new one for rendering. All glyphs added to the same
   * typeface builder share internal caches during rendering.
   */
  std::shared_ptr<Typeface> detach() const override;

 private:
  std::vector<std::shared_ptr<PathProvider>> glyphRecords = {};
};

/**
 * ImageTypefaceBuilder is a CustomTypefaceBuilder that lets you add glyphs defined by images.
 * Use it to create typefaces from custom images. The resulting typefaces are render-only and
 * contain just the information needed to display glyphs.
 */
class ImageTypefaceBuilder : public CustomTypefaceBuilder {
 public:
  struct GlyphRecord {
    std::shared_ptr<ImageCodec> image = nullptr;
    Point offset = {};
    GlyphRecord(std::shared_ptr<ImageCodec> image, const Point& offset)
        : image(std::move(image)), offset(offset) {
    }
  };

  /**
   * Adds a glyph to the typeface using an ImageCodec. The ImageCodec is expected to provide the
   * image for the glyph when requested. It may be called from any thread, so it must be thread-safe
   * and immutable after creation. Returns the GlyphID of the new glyph, which is a unique
   * identifier within the typeface, starting from 1. Returns 0 if the glyph cannot be added because
   * the typeface builder is full.
   */
  GlyphID addGlyph(std::shared_ptr<ImageCodec> image, const Point& offset);

  std::shared_ptr<Typeface> detach() const override;

 private:
  std::vector<std::shared_ptr<GlyphRecord>> glyphRecords = {};
};
}  // namespace tgfx
