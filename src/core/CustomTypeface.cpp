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

#include "tgfx/core/CustomTypeface.h"
#include "ImageUserTypeface.h"
#include "PathUserTypeface.h"
#include "tgfx/core/PathProvider.h"

namespace tgfx {
void CustomTypefaceBuilder::setFontName(const std::string& fontFamily,
                                        const std::string& fontStyle) {
  _fontFamily = fontFamily;
  _fontStyle = fontStyle;
}

void CustomTypefaceBuilder::setMetrics(const FontMetrics& metrics) {
  _fontMetrics = metrics;
}

GlyphID PathTypefaceBuilder::addGlyph(const Path& path) {
  if (glyphRecords.size() >= std::numeric_limits<GlyphID>::max()) {
    // Reached the maximum number of glyphs. Return an invalid GlyphID
    return 0;
  }
  // GlyphID starts from 1
  auto glyphID = static_cast<GlyphID>(glyphRecords.size() + 1);
  glyphRecords.emplace_back(PathProvider::Wrap(path));
  return glyphID;
}

GlyphID PathTypefaceBuilder::addGlyph(std::shared_ptr<PathProvider> provider) {
  if (glyphRecords.size() >= std::numeric_limits<GlyphID>::max()) {
    // Reached the maximum number of glyphs. Return an invalid GlyphID
    return 0;
  }
  // GlyphID starts from 1
  auto glyphID = static_cast<GlyphID>(glyphRecords.size() + 1);
  glyphRecords.emplace_back(std::move(provider));
  return glyphID;
}

std::shared_ptr<Typeface> PathTypefaceBuilder::detach() const {
  if (glyphRecords.empty()) {
    return nullptr;
  }
  return PathUserTypeface::Make(uniqueID, _fontFamily, _fontStyle, _fontMetrics, glyphRecords);
}

GlyphID ImageTypefaceBuilder::addGlyph(std::shared_ptr<ImageCodec> image, const Point& offset) {
  if (glyphRecords.size() >= std::numeric_limits<GlyphID>::max()) {
    // Reached the maximum number of glyphs.Return an invalid GlyphID
    return 0;
  }
  auto glyphID = static_cast<GlyphID>(glyphRecords.size() + 1);
  glyphRecords.emplace_back(std::make_shared<GlyphRecord>(std::move(image), offset));
  return glyphID;
}

std::shared_ptr<Typeface> ImageTypefaceBuilder::detach() const {
  if (glyphRecords.empty()) {
    return nullptr;
  }
  return ImageUserTypeface::Make(uniqueID, _fontFamily, _fontStyle, _fontMetrics, glyphRecords);
}

}  // namespace tgfx
