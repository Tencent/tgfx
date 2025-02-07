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

#include "FTTypeface.h"
#include <cstddef>
#include "FTLibrary.h"
#include FT_TRUETYPE_TABLES_H
#include "FTScalerContext.h"
#include "SystemFont.h"
#include "core/utils/UniqueID.h"
#include "tgfx/core/UTF.h"

namespace tgfx {
std::shared_ptr<Typeface> Typeface::MakeFromName(const std::string& fontFamily,
                                                 const std::string& fontStyle) {
  return SystemFont::MakeFromName(fontFamily, fontStyle);
}

std::shared_ptr<Typeface> Typeface::MakeFromName(const std::string& fontFamily,
                                                 const FontStyle& fontStyle) {
  return SystemFont::MakeFromName(fontFamily, fontStyle);
}

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
    auto* ttHeader = static_cast<TT_Header*>(FT_Get_Sfnt_Table(face, FT_SFNT_HEAD));
    if (ttHeader) {
      upem = ttHeader->Units_Per_EM;
    }
  }
  return upem;
}

bool FTTypeface::hasColor() const {
  std::lock_guard<std::mutex> autoLock(locker);
  return FT_HAS_COLOR(face);
}

bool FTTypeface::hasOutlines() const {
  std::lock_guard<std::mutex> autoLock(locker);
  return FT_IS_SCALABLE(face);
}

GlyphID FTTypeface::getGlyphID(Unichar unichar) const {
  std::lock_guard<std::mutex> autoLock(locker);
  return static_cast<GlyphID>(FT_Get_Char_Index(face, static_cast<FT_ULong>(unichar)));
}

std::shared_ptr<Data> FTTypeface::getBytes() const {
  return data.data;
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

#ifdef TGFX_USE_GLYPH_TO_UNICODE
std::vector<Unichar> FTTypeface::getGlyphToUnicodeMap() const {
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
#endif

}  // namespace tgfx