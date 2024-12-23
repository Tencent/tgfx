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

#include "WebTypeface.h"
#include <vector>
#include "core/utils/UniqueID.h"
#include "platform/web/WebImageBuffer.h"
#include "tgfx/core/UTF.h"

using namespace emscripten;

namespace tgfx {
std::shared_ptr<Typeface> Typeface::MakeFromName(const std::string& name,
                                                 const std::string& style) {
  return WebTypeface::Make(name, style);
}

std::shared_ptr<Typeface> Typeface::MakeFromPath(const std::string&, int) {
  return nullptr;
}

std::shared_ptr<Typeface> Typeface::MakeFromBytes(const void*, size_t, int) {
  return nullptr;
}

std::shared_ptr<Typeface> Typeface::MakeFromData(std::shared_ptr<Data>, int) {
  return nullptr;
}

std::shared_ptr<WebTypeface> WebTypeface::Make(const std::string& name, const std::string& style) {
  auto scalerContextClass = val::module_property("ScalerContext");
  if (!scalerContextClass.as<bool>()) {
    return nullptr;
  }
  auto webTypeface = std::shared_ptr<WebTypeface>(new WebTypeface(name, style));
  webTypeface->scalerContextClass = scalerContextClass;
  return webTypeface;
}

WebTypeface::WebTypeface(std::string name, std::string style)
    : _uniqueID(UniqueID::Next()), name(std::move(name)), style(std::move(style)) {
  webFontFamily = this->name;
  if (!this->style.empty()) {
    webFontFamily += " " + this->style;
  }
  auto emojiName = this->name;
  std::transform(emojiName.begin(), emojiName.end(), emojiName.begin(), ::tolower);
  _hasColor = emojiName.find("emoji") != std::string::npos;
}

// The web side does not involve multithreading and does not require locking.
static std::unordered_map<std::string, std::vector<Unichar>>& GlyphsMap() {
  static auto& glyphs = *new std::unordered_map<std::string, std::vector<Unichar>>;
  return glyphs;
}

GlyphID WebTypeface::getGlyphID(Unichar unichar) const {
  auto text = UTF::ToUTF8(unichar);
  if (!_hasColor && scalerContextClass.call<bool>("isEmoji", text)) {
    return 0;
  }
  auto& glyphs = GlyphsMap()[webFontFamily];
  auto iter = std::find(glyphs.begin(), glyphs.end(), unichar);
  if (iter != glyphs.end()) {
    return static_cast<GlyphID>(iter - glyphs.begin() + 1);
  }
  if (glyphs.size() >= UINT16_MAX) {
    return 0;
  }
  glyphs.push_back(unichar);
  return static_cast<GlyphID>(glyphs.size());
}

std::shared_ptr<Data> WebTypeface::getBytes() const {
  return nullptr;
}

std::string WebTypeface::getText(GlyphID glyphID) const {
  if (glyphID == 0 || GlyphsMap().find(webFontFamily) == GlyphsMap().end()) {
    return "";
  }
  const auto& glyphs = GlyphsMap()[webFontFamily];
  if (glyphID > glyphs.size()) {
    return "";
  }
  auto unichar = glyphs.at(glyphID - 1);
  return UTF::ToUTF8(unichar);
}

#ifdef TGFX_USE_GLYPH_TO_UNICODE
std::vector<Unichar> WebTypeface::getGlyphToUnicodeMap() const {
  return GlyphsMap()[webFontFamily];
}
#endif

}  // namespace tgfx
