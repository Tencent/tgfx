/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/CustomTypefaceBuilder.h"
#include "tgfx/core/ImageCodec.h"

namespace tgfx {
/**
 * ImageTypefaceBuilder is a CustomTypefaceBuilder that lets you add glyphs defined by images.
 * Use it to create typefaces from custom images. The resulting typefaces are render-only and
 * contain just the information needed to display glyphs.
 */
class ImageTypefaceBuilder : public CustomTypefaceBuilder {
 public:
  struct GlyphRecord {  // logical union
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
  std::vector<std::shared_ptr<GlyphRecord>> glyphRecords;
};
}  // namespace tgfx
