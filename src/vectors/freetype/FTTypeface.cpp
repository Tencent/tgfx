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
#include "FTLibrary.h"
#include FT_TRUETYPE_TABLES_H
#include "FTScalerContext.h"
#include "SystemFont.h"
#include "tgfx/utils/UTF.h"
#include "utils/UniqueID.h"

namespace tgfx {
class EmptyTypeface : public Typeface {
 public:
  uint32_t uniqueID() const override {
    return _uniqueID;
  }

  std::string fontFamily() const override {
    return "";
  }

  std::string fontStyle() const override {
    return "";
  }

  int glyphsCount() const override {
    return 0;
  }

  int unitsPerEm() const override {
    return 0;
  }

  bool hasColor() const override {
    return false;
  }

  GlyphID getGlyphID(Unichar) const override {
    return 0;
  }

  std::shared_ptr<Data> getBytes() const override {
    return nullptr;
  }

  std::shared_ptr<Data> copyTableData(FontTableTag) const override {
    return nullptr;
  }

 protected:
  FontMetrics getMetrics(float) const override {
    return {};
  }

  Rect getBounds(GlyphID, float, bool, bool) const override {
    return {};
  }

  float getAdvance(GlyphID, float, bool, bool, bool) const override {
    return 0;
  }

  bool getPath(GlyphID, float, bool, bool, Path*) const override {
    return false;
  }

  std::shared_ptr<ImageBuffer> getGlyphImage(GlyphID, float, bool, bool, Matrix*) const override {
    return nullptr;
  }

  Point getVerticalOffset(GlyphID, float, bool, bool) const override {
    return Point::Zero();
  }

 private:
  uint32_t _uniqueID = UniqueID::Next();
};

std::shared_ptr<Typeface> Typeface::MakeFromName(const std::string& fontFamily,
                                                 const std::string& fontStyle) {
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

std::shared_ptr<Typeface> Typeface::MakeDefault() {
  return std::make_shared<EmptyTypeface>();
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

int FTTypeface::GetUnitsPerEm(FT_Face face) {
  auto unitsPerEm = face->units_per_EM;
  // At least some versions of FreeType set face->units_per_EM to 0 for bitmap only fonts.
  if (unitsPerEm == 0) {
    auto* ttHeader = static_cast<TT_Header*>(FT_Get_Sfnt_Table(face, FT_SFNT_HEAD));
    if (ttHeader) {
      unitsPerEm = ttHeader->Units_Per_EM;
    }
  }
  return unitsPerEm;
}

std::string FTTypeface::fontFamily() const {
  std::lock_guard<std::mutex> autoLock(locker);
  return face->family_name ? face->family_name : "";
}

std::string FTTypeface::fontStyle() const {
  std::lock_guard<std::mutex> autoLock(locker);
  return face->style_name ? face->style_name : "";
}

int FTTypeface::glyphsCount() const {
  std::lock_guard<std::mutex> autoLock(locker);
  return static_cast<int>(face->num_glyphs);
}

int FTTypeface::unitsPerEm() const {
  std::lock_guard<std::mutex> autoLock(locker);
  return GetUnitsPerEm(face);
}

bool FTTypeface::hasColor() const {
  std::lock_guard<std::mutex> autoLock(locker);
  return FT_HAS_COLOR(face);
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

FontMetrics FTTypeface::getMetrics(float size) const {
  auto scalerContext = FTScalerContext::Make(weakThis.lock(), size);
  if (scalerContext == nullptr) {
    return {};
  }
  return scalerContext->generateFontMetrics(false, false);
}

Rect FTTypeface::getBounds(GlyphID glyphID, float size, bool fauxBold, bool fauxItalic) const {
  auto scalerContext = FTScalerContext::Make(weakThis.lock(), size);
  if (scalerContext == nullptr) {
    return Rect::MakeEmpty();
  }
  auto glyphMetrics = scalerContext->generateGlyphMetrics(glyphID, fauxBold, fauxItalic);
  auto bounds =
      Rect::MakeXYWH(glyphMetrics.left, glyphMetrics.top, glyphMetrics.width, glyphMetrics.height);
  auto advance = glyphMetrics.advanceX;
  if (bounds.isEmpty() && advance > 0) {
    auto fontMetrics = scalerContext->generateFontMetrics(fauxBold, fauxItalic);
    bounds.setLTRB(0, fontMetrics.ascent, advance, fontMetrics.descent);
  }
  return bounds;
}

float FTTypeface::getAdvance(GlyphID glyphID, float size, bool fauxBold, bool fauxItalic,
                             bool verticalText) const {
  auto scalerContext = FTScalerContext::Make(weakThis.lock(), size);
  if (scalerContext == nullptr) {
    return 0;
  }
  auto glyphMetrics =
      scalerContext->generateGlyphMetrics(glyphID, fauxBold, fauxItalic, verticalText);
  return verticalText ? glyphMetrics.advanceY : glyphMetrics.advanceX;
}

bool FTTypeface::getPath(GlyphID glyphID, float size, bool fauxBold, bool fauxItalic,
                         Path* path) const {
  auto scalerContext = FTScalerContext::Make(weakThis.lock(), size);
  if (scalerContext == nullptr) {
    return false;
  }
  return scalerContext->generatePath(glyphID, fauxBold, fauxItalic, path);
}

std::shared_ptr<ImageBuffer> FTTypeface::getGlyphImage(GlyphID glyphID, float size, bool fauxBold,
                                                       bool fauxItalic, Matrix* matrix) const {
  auto scalerContext = FTScalerContext::Make(weakThis.lock(), size);
  if (scalerContext == nullptr) {
    return nullptr;
  }
  return scalerContext->generateImage(glyphID, fauxBold, fauxItalic, matrix);
}

Point FTTypeface::getVerticalOffset(GlyphID glyphID, float size, bool fauxBold,
                                    bool fauxItalic) const {
  auto scalerContext = FTScalerContext::Make(weakThis.lock(), size);
  if (scalerContext == nullptr) {
    return Point::Zero();
  }
  auto metrics = scalerContext->generateFontMetrics(fauxBold, fauxItalic);
  auto offsetY = metrics.capHeight;
  auto glyphMetrics = scalerContext->generateGlyphMetrics(glyphID, fauxBold, fauxItalic);
  return {-glyphMetrics.advanceX * 0.5f, offsetY};
}
}  // namespace tgfx
