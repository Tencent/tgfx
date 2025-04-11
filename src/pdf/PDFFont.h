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

#include <memory>
#include <string>
#include <unordered_map>
#include "core/TypefaceMetrics.h"
#include "pdf/PDFGlyphUse.h"
#include "pdf/PDFTypes.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/GlyphFace.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {

class PDFFont;

class PDFStrikeSpec {
 public:
  PDFStrikeSpec(const Font& font, float em) : font(font), unitsPerEM(em){};

  const Font& font;
  const float unitsPerEM;
};

class PDFStrike {
 public:
  static std::shared_ptr<PDFStrike> Make(PDFDocument* doc, const Font& font);

  /** Get the font resource for the glyph.
     *  The returned SkPDFFont is owned by the SkPDFStrike.
     *  @param glyph  The glyph of interest
     */
  PDFFont* getFontResource(GlyphID glyphID);

  // struct Traits {
  //   static const SkDescriptor& GetKey(const std::shared_ptr<SkPDFStrike>& strike);
  //   static uint32_t Hash(const SkDescriptor& descriptor);
  // };

  const PDFStrikeSpec strikeSpec;
  // const SkPDFStrikeSpec fImage;
  // const bool fHasMaskFilter;
  PDFDocument* fDoc;
  std::unordered_map<GlyphID, std::unique_ptr<PDFFont>> fFontMap;

 private:
  PDFStrike(PDFStrikeSpec strikeSpec, PDFDocument* document);
};

class PDFFont {
 public:
  ~PDFFont();
  PDFFont(PDFFont&&) noexcept;
  PDFFont& operator=(PDFFont&&) = delete;
  PDFFont() = delete;
  PDFFont(const PDFFont&) = delete;
  PDFFont& operator=(const PDFFont&) = delete;

  /** Returns the font type represented in this font.  For Type0 fonts,
     *  returns the type of the descendant font.
     */
  TypefaceMetrics::FontType getType() const {
    return fFontType;
  }

  static TypefaceMetrics::FontType FontType(const PDFStrike& pdfStrike,
                                            const TypefaceMetrics& metrics);

  static void GetType1GlyphNames(const Typeface& /*facetype*/, std::string*) {
    // facetype. .getPostScriptGlyphNames(dst);
  }

  static bool IsMultiByte(TypefaceMetrics::FontType type) {
    return type == TypefaceMetrics::FontType::Type1 ||
           type == TypefaceMetrics::FontType::TrueType || type == TypefaceMetrics::FontType::CFF;
  }

  /** Returns true if this font encoding supports glyph IDs above 255.
     */
  bool multiByteGlyphs() const {
    return PDFFont::IsMultiByte(this->getType());
  }

  /** Return true if this font has an encoding for the passed glyph id.
     */
  bool hasGlyph(GlyphID gid) const {
    return (gid >= this->firstGlyphID() && gid <= this->lastGlyphID()) || gid == 0;
  }

  /** Convert the input glyph ID into the font encoding.  */
  GlyphID glyphToPDFFontEncoding(GlyphID gid) const {
    if (this->multiByteGlyphs() || gid == 0) {
      return gid;
    }
    DEBUG_ASSERT((gid >= this->firstGlyphID() && gid <= this->lastGlyphID()));
    DEBUG_ASSERT(this->firstGlyphID() > 0);
    return gid - this->firstGlyphID() + 1;
  }

  void noteGlyphUsage(GlyphID glyph) {
    DEBUG_ASSERT(this->hasGlyph(glyph));
    fGlyphUsage.set(glyph);
  }

  PDFIndirectReference indirectReference() const {
    return fIndirectReference;
  }

  /** Gets SkAdvancedTypefaceMetrics, and caches the result.
     *  @param typeface can not be nullptr.
     *  @return nullptr only when typeface is bad.
     */
  static const TypefaceMetrics* GetMetrics(std::shared_ptr<Typeface> typeface,
                                           PDFDocument* document);

  static const std::vector<Unichar>& GetUnicodeMap(const Typeface& typeface, PDFDocument* document);

  static void PopulateCommonFontDescriptor(PDFDictionary* descriptor,
                                           const TypefaceMetrics& metrics, uint16_t emSize,
                                           int16_t defaultWidth);

  void emitSubset(PDFDocument*) const;

  /** Return false iff the typeface has its NotEmbeddable flag set. */
  static bool CanEmbedTypeface(const Typeface&, PDFDocument*);

  GlyphID firstGlyphID() const {
    return fGlyphUsage.firstNonZero();
  }

  GlyphID lastGlyphID() const {
    return fGlyphUsage.lastGlyph();
  }

  const PDFGlyphUse& glyphUsage() const {
    return fGlyphUsage;
  }

  const PDFStrike& strike() const {
    return *fStrike;
  }

  static std::shared_ptr<Data> GetTypefaceData(const std::shared_ptr<Typeface>& typeface) {
    return typeface->openData();
  }

 private:
  void emitSubsetType0(PDFDocument* document) const;
  void emitSubsetType3(PDFDocument* doc) const;

  const PDFStrike* fStrike;
  PDFGlyphUse fGlyphUsage;
  PDFIndirectReference fIndirectReference;
  TypefaceMetrics::FontType fFontType;

  PDFFont(const PDFStrike*, GlyphID firstGlyphID, GlyphID lastGlyphID,
          TypefaceMetrics::FontType fontType, PDFIndirectReference indirectReference);

  friend class PDFStrike;
};

}  // namespace tgfx