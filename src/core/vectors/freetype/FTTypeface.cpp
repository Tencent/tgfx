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

#if defined(__ANDROID__) || defined(ANDROID)
#include "platform/android/GlyphRenderer.h"
#include "tgfx/platform/android/JNIEnvironment.h"
#endif
#include "FTLibrary.h"
#include "FTTypeface.h"
#include "core/AdvancedTypefaceInfo.h"
#include "core/utils/FontTableTag.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/Typeface.h"
#include FT_TRUETYPE_TABLES_H
#include FT_FONT_FORMATS_H
#include FT_TYPE1_TABLES_H
#include <array>
#include "FTScalerContext.h"
#include "SystemFont.h"
#include "core/utils/UniqueID.h"
#include "tgfx/core/UTF.h"

namespace tgfx {
#ifndef TGFX_BUILD_FOR_WEB
std::shared_ptr<Typeface> Typeface::MakeFromName(const std::string& fontFamily,
                                                 const std::string& fontStyle) {
  return SystemFont::MakeFromName(fontFamily, fontStyle);
}

std::shared_ptr<Typeface> Typeface::MakeFromName(const std::string& fontFamily,
                                                 const FontStyle& fontStyle) {
  return SystemFont::MakeFromName(fontFamily, fontStyle);
}
#endif

std::shared_ptr<Typeface> Typeface::MakeFromPath(const std::string& fontPath, int ttcIndex) {
  return FTTypeface::Make(FTFontData(fontPath, ttcIndex));
}

std::shared_ptr<Typeface> Typeface::MakeFromBytes(const void* bytes, size_t length, int ttcIndex) {
  auto data = Data::MakeWithCopy(bytes, length);
  return MakeFromData(std::move(data), ttcIndex);
}

std::shared_ptr<Typeface> Typeface::MakeFromData(std::shared_ptr<Data> data, int ttcIndex) {
  if (data == nullptr || data->empty()) {
    return nullptr;
  }
  return FTTypeface::Make(FTFontData(std::move(data), ttcIndex));
}

static std::mutex& FTMutex() {
  static std::mutex& mutex = *new std::mutex;
  return mutex;
}

static FT_Face CreateFTFace(const FTFontData& data) {
  std::lock_guard<std::mutex> autoLock(FTMutex());
  FT_Open_Args args;
  memset(&args, 0, sizeof(args));
  if (data.data) {
    args.flags = FT_OPEN_MEMORY;
    args.memory_base = static_cast<const FT_Byte*>(data.data->data());
    args.memory_size = static_cast<FT_Long>(data.data->size());
  } else if (!data.path.empty()) {
    args.flags = FT_OPEN_PATHNAME;
    args.pathname = const_cast<FT_String*>(data.path.c_str());
  } else {
    return nullptr;
  }
  FT_Face face = nullptr;
  auto ftLibrary = FTLibrary::Get();
  auto err = FT_Open_Face(ftLibrary, &args, data.ttcIndex, &face);
  if (err || !face->family_name) {
    return nullptr;
  }
  if (!face->charmap) {
    FT_Select_Charmap(face, FT_ENCODING_MS_SYMBOL);
  }
  return face;
}

std::shared_ptr<FTTypeface> FTTypeface::Make(FTFontData data) {
  auto face = CreateFTFace(data);
  if (face == nullptr) {
    return nullptr;
  }
  auto typeface = std::shared_ptr<FTTypeface>(new FTTypeface(std::move(data), face));
  typeface->weakThis = typeface;
  return typeface;
}

FTTypeface::FTTypeface(FTFontData data, FT_Face face)
    : _uniqueID(UniqueID::Next()), data(std::move(data)), face(std::move(face)) {
  _hasColor = FT_HAS_COLOR(this->face);
  _hasOutlines = FT_IS_SCALABLE(this->face);
#if defined(__ANDROID__) || defined(ANDROID)
  if (hasColor() && hasOutlines() && GlyphRenderer::IsAvailable()) {
    JNIEnvironment environment;
    auto env = environment.current();
    if (env != nullptr) {
      typeface = GlyphRenderer::CreateTypeface(env, this->data.path);
    }
  }
#endif
}

FTTypeface::~FTTypeface() {
  std::lock_guard<std::mutex> autoLock(FTMutex());
  FT_Done_Face(face);
}

std::string FTTypeface::fontFamily() const {
  std::lock_guard<std::mutex> autoLock(locker);
  return face->family_name ? face->family_name : "";
}

std::string FTTypeface::fontStyle() const {
  std::lock_guard<std::mutex> autoLock(locker);
  return face->style_name ? face->style_name : "";
}

size_t FTTypeface::glyphsCount() const {
  std::lock_guard<std::mutex> autoLock(locker);
  return static_cast<size_t>(face->num_glyphs);
}

int FTTypeface::unitsPerEm() const {
  std::lock_guard<std::mutex> autoLock(locker);
  return unitsPerEmInternal();
}

int FTTypeface::unitsPerEmInternal() const {
  auto upem = face->units_per_EM;
  // At least some versions of FreeType set face->units_per_EM to 0 for bitmap only fonts.
  if (upem == 0) {
    auto ttHeader = static_cast<TT_Header*>(FT_Get_Sfnt_Table(face, FT_SFNT_HEAD));
    if (ttHeader) {
      upem = ttHeader->Units_Per_EM;
    }
  }
  return upem;
}

GlyphID FTTypeface::getGlyphID(Unichar unichar) const {
  std::lock_guard<std::mutex> autoLock(locker);
  return static_cast<GlyphID>(FT_Get_Char_Index(face, static_cast<FT_ULong>(unichar)));
}

std::unique_ptr<Stream> FTTypeface::openStream() const {
  if (data.data) {
    return Stream::MakeFromData(data.data);
  }
  return Stream::MakeFromFile(data.path);
}

std::shared_ptr<Data> FTTypeface::copyTableData(FontTableTag tag) const {
  std::lock_guard<std::mutex> autoLock(locker);
  FT_ULong tableLength = 0;
  auto error = FT_Load_Sfnt_Table(face, tag, 0, nullptr, &tableLength);
  if (error) {
    return nullptr;
  }
  auto tableData = new (std::nothrow) uint8_t[tableLength];
  if (tableData == nullptr) {
    return nullptr;
  }
  error = FT_Load_Sfnt_Table(face, tag, 0, reinterpret_cast<FT_Byte*>(tableData), &tableLength);
  if (error) {
    delete[] tableData;
    return nullptr;
  }
  return Data::MakeAdopted(tableData, tableLength);
}

#ifdef TGFX_USE_ADVANCED_TYPEFACE_PROPERTY
namespace {
bool CanEmbed(FT_Face face) {
  FT_UShort fsType = FT_Get_FSType_Flags(face);
  return (fsType & (FT_FSTYPE_RESTRICTED_LICENSE_EMBEDDING | FT_FSTYPE_BITMAP_EMBEDDING_ONLY)) == 0;
}

bool CanSubset(FT_Face face) {
  FT_UShort fsType = FT_Get_FSType_Flags(face);
  return (fsType & FT_FSTYPE_NO_SUBSETTING) == 0;
}

AdvancedTypefaceInfo::FontType GetFontType(FT_Face face) {
  const char* fontType = FT_Get_X11_Font_Format(face);
  struct TextToFontType {
    const char* text;
    AdvancedTypefaceInfo::FontType type;
  };

  static TextToFontType values[] = {
      {"Type 1", AdvancedTypefaceInfo::FontType::Type1},
      {"CID Type 1", AdvancedTypefaceInfo::FontType::Type1CID},
      {"CFF", AdvancedTypefaceInfo::FontType::CFF},
      {"TrueType", AdvancedTypefaceInfo::FontType::TrueType},
  };
  for (const auto& value : values) {
    if (strcmp(fontType, value.text) == 0) {
      return value.type;
    }
  }
  return AdvancedTypefaceInfo::FontType::Other;
}
}  // namespace

bool FTTypeface::isOpentypeFontDataStandardFormat() const {
  // FreeType reports TrueType for any data that can be decoded to TrueType or OpenType.
  // However, there are alternate data formats for OpenType, like wOFF and wOF2.
  auto stream = openStream();
  std::array<char, 4> buffer;
  if (stream->read(buffer.data(), 4) < 4) {
    return false;
  }
  auto fontTag = SetFourByteTag(buffer[0], buffer[1], buffer[2], buffer[3]);

  constexpr auto windowsTrueTypeTag = SetFourByteTag(0, 1, 0, 0);
  constexpr auto macTrueTypeTag = SetFourByteTag('t', 'r', 'u', 'e');
  constexpr auto postScriptTag = SetFourByteTag('t', 'y', 'p', '1');
  constexpr auto opentypeCFFTag = SetFourByteTag('O', 'T', 'T', 'O');
  constexpr auto ttcTag = SetFourByteTag('t', 't', 'c', 'f');

  return fontTag == windowsTrueTypeTag || fontTag == macTrueTypeTag || fontTag == postScriptTag ||
         fontTag == opentypeCFFTag || fontTag == ttcTag;
}

AdvancedTypefaceInfo FTTypeface::getAdvancedInfo() const {
  AdvancedTypefaceInfo advancedProperty;
  advancedProperty.postScriptName = FT_Get_Postscript_Name(face);

  if (FT_HAS_MULTIPLE_MASTERS(face)) {
    advancedProperty.flags = static_cast<AdvancedTypefaceInfo::FontFlags>(
        advancedProperty.flags | AdvancedTypefaceInfo::FontFlags::Variable);
  }
  if (!CanEmbed(face)) {
    advancedProperty.flags = static_cast<AdvancedTypefaceInfo::FontFlags>(
        advancedProperty.flags | AdvancedTypefaceInfo::FontFlags::NotEmbeddable);
  }
  if (!CanSubset(face)) {
    advancedProperty.flags = static_cast<AdvancedTypefaceInfo::FontFlags>(
        advancedProperty.flags | AdvancedTypefaceInfo::FontFlags::NotSubsettable);
  }

  advancedProperty.type = GetFontType(face);
  if ((advancedProperty.type == AdvancedTypefaceInfo::FontType::TrueType ||
       advancedProperty.type == AdvancedTypefaceInfo::FontType::CFF) &&
      !isOpentypeFontDataStandardFormat()) {
    advancedProperty.flags = static_cast<AdvancedTypefaceInfo::FontFlags>(
        advancedProperty.flags | AdvancedTypefaceInfo::FontFlags::AltDataFormat);
  }

  advancedProperty.style = static_cast<AdvancedTypefaceInfo::StyleFlags>(0);
  if (FT_IS_FIXED_WIDTH(face)) {
    advancedProperty.style = static_cast<AdvancedTypefaceInfo::StyleFlags>(
        advancedProperty.style | AdvancedTypefaceInfo::StyleFlags::FixedPitch);
  }
  if (face->style_flags & FT_STYLE_FLAG_ITALIC) {
    advancedProperty.style = static_cast<AdvancedTypefaceInfo::StyleFlags>(
        advancedProperty.style | AdvancedTypefaceInfo::StyleFlags::Italic);
  }
  return advancedProperty;
}
#endif

#ifdef TGFX_USE_GLYPH_TO_UNICODE
std::vector<Unichar> FTTypeface::onCreateGlyphToUnicodeMap() const {
  auto numGlyphs = static_cast<size_t>(face->num_glyphs);
  std::vector<Unichar> returnMap(numGlyphs, 0);

  FT_UInt glyphIndex = 0;
  auto charCode = FT_Get_First_Char(face, &glyphIndex);
  while (glyphIndex) {
    if (0 == returnMap[glyphIndex]) {
      returnMap[glyphIndex] = static_cast<Unichar>(charCode);
    }
    charCode = FT_Get_Next_Char(face, charCode, &glyphIndex);
  }
  return returnMap;
}

std::string FTTypeface::getGlyphUTF8(GlyphID glyphID) const {
  auto& map = getGlyphToUnicodeMap();
  Unichar unichar = glyphID < map.size() ? map[glyphID] : 0;
  return UTF::ToUTF8(unichar);
}
#endif

std::shared_ptr<ScalerContext> FTTypeface::onCreateScalerContext(float size) const {
  return std::make_shared<FTScalerContext>(weakThis.lock(), size);
}

}  // namespace tgfx
