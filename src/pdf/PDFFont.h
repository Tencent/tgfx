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
#include "core/AdvancedTypefaceInfo.h"
#include "pdf/PDFGlyphUse.h"
#include "pdf/PDFTypes.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/FontMetrics.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {

class PDFFont;

class PDFStrikeSpec {
 public:
  PDFStrikeSpec(std::shared_ptr<Typeface> typeface, float size, float em)
      : typeface(std::move(typeface)), textSize(size), unitsPerEM(em) {
  }

  std::shared_ptr<Typeface> typeface;
  const float textSize;
  const float unitsPerEM;
};

class PDFStrike {
 public:
  static std::shared_ptr<PDFStrike> Make(PDFDocumentImpl* doc, const Font& font);

  /** Get the font resource for the glyph.
   *  The returned SkPDFFont is owned by the SkPDFStrike.
   *  @param glyph  The glyph of interest
   */
  PDFFont* getFontResource(GlyphID glyphID);

  const PDFStrikeSpec strikeSpec;
  PDFDocumentImpl* document;
  std::unordered_map<GlyphID, std::unique_ptr<PDFFont>> fontMap;

 private:
  PDFStrike(PDFStrikeSpec strikeSpec, PDFDocumentImpl* document);
};

class PDFFont {
 public:
  ~PDFFont();
  PDFFont(PDFFont&&) noexcept;
  PDFFont& operator=(PDFFont&&) = delete;
  PDFFont() = delete;
  PDFFont(const PDFFont&) = delete;
  PDFFont& operator=(const PDFFont&) = delete;

  /** 
   * Returns the font type represented in this font. For Type0 fonts, returns the type of the
   * descendant font. 
   */
  AdvancedTypefaceInfo::FontType getType() const {
    return fontType;
  }

  static AdvancedTypefaceInfo::FontType FontType(const PDFStrike& pdfStrike,
                                                 const AdvancedTypefaceInfo& advancedInfo);

  static bool IsMultiByte(AdvancedTypefaceInfo::FontType type) {
    return type == AdvancedTypefaceInfo::FontType::Type1 ||
           type == AdvancedTypefaceInfo::FontType::TrueType ||
           type == AdvancedTypefaceInfo::FontType::CFF;
  }

  /** 
   * Returns true if this font encoding supports glyph IDs above 255.
   */
  bool multiByteGlyphs() const {
    return PDFFont::IsMultiByte(this->getType());
  }

  /** 
   * Return true if this font has an encoding for the passed glyph id.
   */
  bool hasGlyph(GlyphID gid) const {
    return (gid >= this->firstGlyphID() && gid <= this->lastGlyphID()) || gid == 0;
  }

  /** 
   * Convert the input glyph ID into the font encoding.
   */
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
    _glyphUsage.set(glyph);
  }

  PDFIndirectReference indirectReference() const {
    return _indirectReference;
  }

  /** 
   * Gets SkAdvancedTypefaceMetrics, and caches the result.
   */
  static const AdvancedTypefaceInfo* GetAdvancedInfo(const std::shared_ptr<Typeface>& typeface,
                                                     float textSize, PDFDocumentImpl* document);

  static std::shared_ptr<ScalerContext> GetScalerContext(const std::shared_ptr<Typeface>& typeface,
                                                         float textSize);

  static const std::vector<Unichar>& GetUnicodeMap(const Typeface& typeface,
                                                   PDFDocumentImpl* document);

  static void PopulateCommonFontDescriptor(PDFDictionary* descriptor,
                                           const AdvancedTypefaceInfo& advancedInfo,
                                           uint16_t emSize, int16_t defaultWidth);

  void emitSubset(PDFDocumentImpl* document) const;

  /** 
   * Return false iff the typeface has its NotEmbeddable flag set.
   */
  static bool CanEmbedTypeface(const Typeface& typeface, PDFDocumentImpl* document);

  GlyphID firstGlyphID() const {
    return _glyphUsage.firstNonZero();
  }

  GlyphID lastGlyphID() const {
    return _glyphUsage.lastGlyph();
  }

  const PDFGlyphUse& glyphUsage() const {
    return _glyphUsage;
  }

  const PDFStrike& strike() const {
    return *_strike;
  }

  static std::unique_ptr<Stream> GetTypefaceStream(const std::shared_ptr<Typeface>& typeface) {
    return typeface->openStream();
  }

 private:
  void emitSubsetType0(PDFDocumentImpl* document) const;
  void emitSubsetType3(PDFDocumentImpl* document) const;

  const PDFStrike* _strike;
  PDFGlyphUse _glyphUsage;
  PDFIndirectReference _indirectReference;
  AdvancedTypefaceInfo::FontType fontType;

  PDFFont(const PDFStrike*, GlyphID firstGlyphID, GlyphID lastGlyphID,
          AdvancedTypefaceInfo::FontType fontType, PDFIndirectReference indirectReference);

  friend class PDFStrike;
};

}  // namespace tgfx
