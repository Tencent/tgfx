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

#include "PDFFont.h"
#include "../../include/tgfx/core/Log.h"
#include "core/AdvancedTypefaceInfo.h"
#include "core/ScalerContext.h"
#include "core/utils/FontTableTag.h"
#include "core/utils/MathExtra.h"
#include "pdf/PDFDocumentImpl.h"
#include "pdf/PDFTypes.h"
#include "pdf/PDFUtils.h"
#include "pdf/fontSubset/PDFMakeCIDGlyphWidthsArray.h"
#include "pdf/fontSubset/PDFMakeToUnicodeCmap.h"
#include "pdf/fontSubset/PDFSubsetFont.h"
#include "pdf/fontSubset/PDFType1Font.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {

// PDF's notion of symbolic vs non-symbolic is related to the character set, not
// symbols vs. characters.  Rarely is a font the right character set to call it
// non-symbolic, so always call it symbolic.  (PDF 1.4 spec, section 5.7.1)
constexpr int32_t PDFSymbolic = 4;

namespace {
inline float FromFontUnits(float scaled, uint16_t emSize) {
  return emSize == 1000 ? scaled : scaled * 1000 / emSize;
}

inline float ScaleFromFontUnits(int16_t val, uint16_t emSize) {
  return FromFontUnits(val, emSize);
}

bool CanEmbed(const AdvancedTypefaceInfo& metrics) {
  return !(metrics.flags & AdvancedTypefaceInfo::FontFlags::NotEmbeddable);
}

bool CanSubset(const AdvancedTypefaceInfo& metrics) {
  return !(metrics.flags & AdvancedTypefaceInfo::FontFlags::NotSubsettable);
}

void SetGlyphWidthAndBoundingBox(float width, Rect box,
                                 const std::shared_ptr<MemoryWriteStream>& content) {
  PDFUtils::AppendFloat(width, content);
  content->writeText(" 0 ");
  content->writeText(std::to_string(static_cast<int>(box.left)));
  content->writeText(" ");
  content->writeText(std::to_string(static_cast<int>(box.top)));
  content->writeText(" ");
  content->writeText(std::to_string(static_cast<int>(box.right)));
  content->writeText(" ");
  content->writeText(std::to_string(static_cast<int>(box.bottom)));
  content->writeText(" d1\n");
}

GlyphID FirstNonzeroGlyphForSingleByteEncoding(GlyphID glyphID) {
  return glyphID != 0 ? glyphID - ((glyphID - 1) % 255) : 1;
}

}  // namespace

PDFStrike::PDFStrike(PDFStrikeSpec strikeSpec, PDFDocumentImpl* document)
    : strikeSpec(std::move(strikeSpec)), document(document) {
}

std::shared_ptr<PDFStrike> PDFStrike::Make(PDFDocumentImpl* doc, const Font& font) {
  DEBUG_ASSERT(0 < font.getTypeface()->unitsPerEm());

  const Font& canonFont(font);
  float pathStrikeEM = canonFont.getSize();
  auto typefaceID = font.getTypeface()->uniqueID();
  auto iter = doc->strikes.find(typefaceID);
  if (iter != doc->strikes.end()) {
    return iter->second;
  }

  auto strike = std::shared_ptr<PDFStrike>(
      new PDFStrike(PDFStrikeSpec(font.getTypeface(), font.getSize(), pathStrikeEM), doc));
  doc->strikes[typefaceID] = strike;
  return strike;
}

const AdvancedTypefaceInfo* PDFFont::GetAdvancedInfo(const std::shared_ptr<Typeface>& typeface,
                                                     float textSize, PDFDocumentImpl* document) {
  auto id = typeface->uniqueID();
  auto iter = document->fontAdvancedInfo.find(id);
  if (iter != document->fontAdvancedInfo.end()) {
    return iter->second.get();  // canon retains ownership.
  }

  auto count = typeface->glyphsCount();
  if (count <= 0 || count > 1 + UINT16_MAX) {
    // Cache nullptr to skip this check.  Use SkSafeUnref().
    document->fontAdvancedInfo[id] = nullptr;
    return nullptr;
  }

  auto advancedInfo = typeface->getAdvancedInfo();
  if (advancedInfo.capHeight == 0) {
    Font font;
    font.setTypeface(typeface);
    font.setSize(textSize);

    float capHeight = 0;
    for (char c : {'M', 'X'}) {
      auto glyphID = font.getGlyphID(c);
      auto bounds = font.getBounds(glyphID);
      capHeight += bounds.height();
    }
    advancedInfo.capHeight = static_cast<int16_t>(std::round(capHeight / 2));
  }

  // Fonts are always subset, so always prepend the subset tag.
  advancedInfo.postScriptName = document->nextFontSubsetTag() + advancedInfo.postScriptName;
  document->fontAdvancedInfo[id] = std::make_unique<AdvancedTypefaceInfo>(advancedInfo);
  return document->fontAdvancedInfo[id].get();
}

std::shared_ptr<ScalerContext> PDFFont::GetScalerContext(const std::shared_ptr<Typeface>& typeface,
                                                         float textSize) {
  return typeface->getScalerContext(textSize);
}

PDFFont::~PDFFont() = default;

PDFFont::PDFFont(const PDFStrike* strike, GlyphID firstGlyphID, GlyphID lastGlyphID,
                 AdvancedTypefaceInfo::FontType fontType, PDFIndirectReference indirectReference)
    : _strike(strike), _glyphUsage(firstGlyphID, lastGlyphID),
      _indirectReference(indirectReference), fontType(fontType) {
  // Always include glyph 0
  this->noteGlyphUsage(0);
}

void PDFFont::PopulateCommonFontDescriptor(PDFDictionary* descriptor,
                                           const AdvancedTypefaceInfo& advancedInfo,
                                           uint16_t emSize, int16_t defaultWidth) {
  descriptor->insertName("FontName", advancedInfo.postScriptName);
  descriptor->insertInt("Flags", 32);
  descriptor->insertInt("ItalicAngle", 0);
  descriptor->insertObject("FontBBox", MakePDFArray(0.f, 0.f, 0.f, 0.f));
  if (defaultWidth > 0) {
    descriptor->insertScalar("MissingWidth", ScaleFromFontUnits(defaultWidth, emSize));
  }
}

AdvancedTypefaceInfo::FontType PDFFont::FontType(const PDFStrike& /*pdfStrike*/,
                                                 const AdvancedTypefaceInfo& advancedInfo) {
  if (advancedInfo.flags & AdvancedTypefaceInfo::FontFlags::Variable ||
      advancedInfo.flags & AdvancedTypefaceInfo::FontFlags::AltDataFormat ||
      advancedInfo.flags & AdvancedTypefaceInfo::FontFlags::NotEmbeddable) {
    // force Type3 fallback.
    return AdvancedTypefaceInfo::FontType::Other;
  }
  return advancedInfo.type;
}

PDFFont* PDFStrike::getFontResource(GlyphID glyphID) {
  auto typeface = strikeSpec.typeface;
  auto textSize = strikeSpec.textSize;
  const auto fontMetrics = PDFFont::GetAdvancedInfo(typeface, textSize, document);
  DEBUG_ASSERT(fontMetrics);  // SkPDFDevice::internalDrawText ensures the typeface is good.
                              // GetMetrics only returns null to signify a bad typeface.
  const auto& metrics = *fontMetrics;

  // Determine the FontType.
  // 1. Can the "original" font data be used directly
  // (simple OpenType, no non-default variations, not WOFF, etc).
  // 2. Is the glyph to be drawn unmodified from the font data
  // (no path effect, stroking, fake bolding, extra matrix, mask filter).
  // 3. Will PDF viewers draw this glyph the way we want
  // (at the moment this means an unmodified glyph path).
  auto type = PDFFont::FontType(*this, metrics);
  // Keep the type (and original data) if the glyph is empty or the glyph has an unmodified path.
  // Otherwise, fall back to Type3.
  // if (!(glyph.isEmpty() || (glyph->path() && !glyph->pathIsModified()))) {
  //   type = SkAdvancedFontMetrics::kOther_Font;
  // }

  bool multibyte = PDFFont::IsMultiByte(type);
  GlyphID subsetCode = multibyte ? 0 : FirstNonzeroGlyphForSingleByteEncoding(glyphID);
  auto iter = fontMap.find(subsetCode);
  if (iter != fontMap.end()) {
    // DEBUG_ASSERT(multibyte == font->multiByteGlyphs());
    return iter->second.get();
  }

  auto lastGlyph = static_cast<GlyphID>(typeface->glyphsCount() - 1);
  DEBUG_ASSERT(glyphID <= lastGlyph);  // should be caught by SkPDFDevice::internalDrawText

  GlyphID firstNonZeroGlyph;
  if (multibyte) {
    firstNonZeroGlyph = 1;
  } else {
    firstNonZeroGlyph = subsetCode;
    lastGlyph = std::min<GlyphID>(lastGlyph, 254 + subsetCode);
  }
  auto ref = document->reserveRef();
  auto pdfFont =
      std::unique_ptr<PDFFont>(new PDFFont(this, firstNonZeroGlyph, lastGlyph, type, ref));
  fontMap[subsetCode] = std::move(pdfFont);
  return fontMap[subsetCode].get();
}

const std::vector<Unichar>& PDFFont::GetUnicodeMap(const Typeface& typeface,
                                                   PDFDocumentImpl* document) {
  DEBUG_ASSERT(document);
  auto id = typeface.uniqueID();
  auto iter = document->toUnicodeMap.find(id);
  if (iter != document->toUnicodeMap.end()) {
    return iter->second;
  }
  std::vector<Unichar> buffer = typeface.getGlyphToUnicodeMap();
  document->toUnicodeMap[id] = buffer;
  return document->toUnicodeMap[id];
}

///////////////////////////////////////////////////////////////////////////////
//  Type0Font
///////////////////////////////////////////////////////////////////////////////

void PDFFont::emitSubsetType0(PDFDocumentImpl* document) const {
  auto typeface = strike().strikeSpec.typeface;
  auto textSize = strike().strikeSpec.textSize;
  const auto metricsPtr = PDFFont::GetAdvancedInfo(typeface, textSize, document);
  if (!metricsPtr) {
    return;
  }
  const auto& metrics = *metricsPtr;
  ASSERT(CanEmbed(metrics));
  auto type = getType();

  auto descriptor = PDFDictionary::Make("FontDescriptor");
  auto emSize = static_cast<uint16_t>(std::round(strike().strikeSpec.unitsPerEM));
  PDFFont::PopulateCommonFontDescriptor(descriptor.get(), metrics, emSize, 0);

  auto fontStream = typeface->openStream();
  size_t fontSize = fontStream ? fontStream->size() : 0;
  if (0 == fontSize) {
    return;
  }

  if (type == AdvancedTypefaceInfo::FontType::TrueType ||
      type == AdvancedTypefaceInfo::FontType::CFF) {
    // Avoid use of FontFile3 OpenType (OpenType with CFF) which is PDF 1.6 (2004).
    // Instead use FontFile3 CIDFontType0C (bare CFF) which is PDF 1.3 (2000).
    // See b/352098914
    std::shared_ptr<Data> subsetFontData = nullptr;
    if (CanSubset(metrics)) {
      DEBUG_ASSERT(firstGlyphID() == 1);
      // If the face has CFF the subsetter will always return just the CFF.
      subsetFontData = PDFSubsetFont(typeface, glyphUsage());
    }
    if (!subsetFontData) {
      // If the data cannot be subset, still ensure bare CFF.
      constexpr FontTableTag CFFTag = SetFourByteTag('C', 'F', 'F', ' ');
      subsetFontData = typeface->copyTableData(CFFTag);
    }
    std::unique_ptr<Stream> subsetFontStream = nullptr;
    if (subsetFontData) {
      subsetFontStream = Stream::MakeFromData(subsetFontData);
    } else {
      // If subsetting fails, fall back to original font data.
      subsetFontStream = std::move(fontStream);
    }
    auto streamDictionary = PDFDictionary::Make();
    streamDictionary->insertInt("Length1", subsetFontStream->size());
    const char* fontFileKey;
    if (type == AdvancedTypefaceInfo::FontType::TrueType) {
      fontFileKey = "FontFile2";
    } else {
      streamDictionary->insertName("Subtype", "CIDFontType0C");
      fontFileKey = "FontFile3";
    }
    auto fontStreamRef = PDFStreamOut(std::move(streamDictionary), std::move(subsetFontStream),
                                      document, PDFSteamCompressionEnabled::Yes);
    descriptor->insertRef(fontFileKey, fontStreamRef);
  } else if (type == AdvancedTypefaceInfo::FontType::Type1CID) {
    auto streamDictionary = PDFDictionary::Make();
    streamDictionary->insertName("Subtype", "CIDFontType0C");
    auto fontStreamRef = PDFStreamOut(std::move(streamDictionary), std::move(fontStream), document,
                                      PDFSteamCompressionEnabled::Yes);
    descriptor->insertRef("FontFile3", fontStreamRef);
  } else {
    DEBUG_ASSERT(false);
  }

  auto newCIDFont = PDFDictionary::Make("Font");
  // TODO(YGaurora): Add FontDescriptor to CIDFont.
  newCIDFont->insertRef("FontDescriptor", document->emit(*descriptor));
  newCIDFont->insertName("BaseFont", metrics.postScriptName);

  switch (type) {
    case AdvancedTypefaceInfo::FontType::Type1CID:
      newCIDFont->insertName("Subtype", "CIDFontType0");
      break;
    case AdvancedTypefaceInfo::FontType::CFF:
      newCIDFont->insertName("Subtype", "CIDFontType0");
      newCIDFont->insertName("CIDToGIDMap", "Identity");
      break;
    case AdvancedTypefaceInfo::FontType::TrueType:
      newCIDFont->insertName("Subtype", "CIDFontType2");
      newCIDFont->insertName("CIDToGIDMap", "Identity");
      break;
    default:
      DEBUG_ASSERT(false);
  }
  auto sysInfo = PDFDictionary::Make();
  // These are actually ASCII strings.
  sysInfo->insertByteString("Registry", "Adobe");
  sysInfo->insertByteString("Ordering", "Identity");
  sysInfo->insertInt("Supplement", 0);
  newCIDFont->insertObject("CIDSystemInfo", std::move(sysInfo));

  // Unfortunately, poppler enforces DW (default width) must be an integer.
  int32_t defaultWidth = 0;
  {
    auto widths = PDFMakeCIDGlyphWidthsArray(strike().strikeSpec, glyphUsage(), &defaultWidth);
    if (widths && widths->size() > 0) {
      newCIDFont->insertObject("W", std::move(widths));
    }
    newCIDFont->insertInt("DW", defaultWidth);
  }

  ////////////////////////////////////////////////////////////////////////////

  PDFDictionary fontDictionary("Font");
  fontDictionary.insertName("Subtype", "Type0");
  fontDictionary.insertName("BaseFont", metrics.postScriptName);
  fontDictionary.insertName("Encoding", "Identity-H");
  auto descendantFonts = MakePDFArray();
  descendantFonts->appendRef(document->emit(*newCIDFont));
  fontDictionary.insertObject("DescendantFonts", std::move(descendantFonts));

  const std::vector<Unichar>& glyphToUnicode = PDFFont::GetUnicodeMap(*typeface, document);
  DEBUG_ASSERT(static_cast<size_t>(typeface->glyphsCount()) == glyphToUnicode.size());
  auto toUnicode = PDFMakeToUnicodeCmap(glyphToUnicode.data(), &_glyphUsage, multiByteGlyphs(),
                                        firstGlyphID(), lastGlyphID());
  fontDictionary.insertRef("ToUnicode", PDFStreamOut(nullptr, std::move(toUnicode), document));

  document->emit(fontDictionary, indirectReference());
}

namespace {
struct SingleByteGlyphIdIterator {
  SingleByteGlyphIdIterator(GlyphID first, GlyphID last) : first(first), last(last) {
    DEBUG_ASSERT(first > 0);
    DEBUG_ASSERT(last >= first);
  }
  struct Iter {
    void operator++() {
      current = (0 == current) ? first : current + 1;
    }
    // This is an input_iterator
    GlyphID operator*() const {
      return static_cast<GlyphID>(current);
    }
    bool operator!=(const Iter& rhs) const {
      return current != rhs.current;
    }
    Iter(GlyphID f, int c) : first(f), current(c) {
    }

   private:
    const GlyphID first;
    int current;  // must be int to make fLast+1 to fit
  };
  Iter begin() const {
    return Iter(first, 0);
  }
  Iter end() const {
    return Iter(first, static_cast<int>(last) + 1);
  }

 private:
  const GlyphID first;
  const GlyphID last;
};

PDFIndirectReference type3_descriptor(PDFDocumentImpl* doc,
                                      const std::shared_ptr<Typeface>& typeface, float xHeight) {
  auto iter = doc->type3FontDescriptors.find(typeface->uniqueID());
  if (iter != doc->type3FontDescriptors.end()) {
    return iter->second;
  }

  PDFDictionary descriptor("FontDescriptor");
  int32_t fontDescriptorFlags = PDFSymbolic;

  /** PDF32000_2008: FontFamily should be used for Type3 fonts in Tagged PDF documents. */
  std::string familyName = typeface->fontFamily();
  if (!familyName.empty()) {
    descriptor.insertByteString("FontFamily", familyName);
  }

  /** PDF32000_2008: FontStretch should be used for Type3 fonts in Tagged PDF documents. */
  descriptor.insertName("FontStretch", "Normal");

  /** PDF32000_2008: FontWeight should be used for Type3 fonts in Tagged PDF documents. */
  descriptor.insertInt("FontWeight", 400);

  if (const auto metrics = PDFFont::GetAdvancedInfo(typeface, 1000.f, doc)) {
    // Type3 FontDescriptor does not require all the same fields.
    descriptor.insertName("FontName", metrics->postScriptName);
    // descriptor.insertInt("ItalicAngle", metrics->fItalicAngle);
    fontDescriptorFlags |= static_cast<int32_t>(metrics->style);
    // Adobe requests CapHeight, XHeight, and StemV be added
    // to "greatly help our workflow downstream".
    if (!FloatNearlyZero(metrics->capHeight)) {
      descriptor.insertInt("CapHeight", static_cast<int>(metrics->capHeight));
    }

    if (xHeight != 0) {
      descriptor.insertScalar("XHeight", xHeight);
    }
  }
  descriptor.insertInt("Flags", fontDescriptorFlags);
  auto ref = doc->emit(descriptor);
  doc->type3FontDescriptors[typeface->uniqueID()] = ref;
  return ref;
}

}  // namespace

void PDFFont::emitSubsetType3(PDFDocumentImpl* doc) const {
  const PDFStrike& pdfStrike = *_strike;
  GlyphID firstGlyphID = this->firstGlyphID();
  GlyphID lastGlyphID = this->lastGlyphID();
  const PDFGlyphUse& subset = glyphUsage();
  DEBUG_ASSERT(lastGlyphID >= firstGlyphID);
  // Remove unused glyphs at the end of the range.
  // Keep the lastGlyphID >= firstGlyphID invariant true.
  while (lastGlyphID > firstGlyphID && !subset.has(lastGlyphID)) {
    --lastGlyphID;
  }

  float emSize = pdfStrike.strikeSpec.unitsPerEM;
  auto typeface = pdfStrike.strikeSpec.typeface;
  auto textSize = pdfStrike.strikeSpec.textSize;

  GlyphID xGlyphID = typeface->getGlyphID('X');
  auto scaleContext = PDFFont::GetScalerContext(typeface, textSize);
  float xHeight = scaleContext->getBounds(xGlyphID, false, false).height();

  PDFDictionary font("Font");
  font.insertName("Subtype", "Type3");
  // Flip about the x-axis and scale by 1/emSize.
  Matrix fontMatrix;
  fontMatrix.setScale(1.f / emSize, -1.f / emSize);
  font.insertObject("FontMatrix", PDFUtils::MatrixToArray(fontMatrix));

  auto charProcs = PDFDictionary::Make();
  auto encoding = PDFDictionary::Make("Encoding");

  auto encDiffs = MakePDFArray();
  // length(firstGlyphID .. lastGlyphID) ==  lastGlyphID - firstGlyphID + 1
  // plus 1 for glyph 0;
  DEBUG_ASSERT(firstGlyphID > 0);
  DEBUG_ASSERT(lastGlyphID >= firstGlyphID);
  size_t glyphCount = lastGlyphID - firstGlyphID + 2;
  // one other entry for the index of first glyph.
  encDiffs->reserve(glyphCount + 1);
  encDiffs->appendInt(0);  // index of first glyph

  auto widthArray = MakePDFArray();
  widthArray->reserve(glyphCount);

  Rect bbox = Rect::MakeEmpty();
  auto xObjects = PDFDictionary::Make();
  auto graphicStates = PDFDictionary::Make();

  for (GlyphID glyphID : SingleByteGlyphIdIterator(firstGlyphID, lastGlyphID)) {
    std::string characterName;
    float glyphWidth = 0.0f;

    if (glyphID != 0 && !subset.has(glyphID)) {
      characterName.append("g0");
      glyphWidth = 0.0f;
      encDiffs->appendName(std::move(characterName));
      widthArray->appendScalar(glyphWidth);
      continue;
    }

    {
      char buffer[16];
      std::snprintf(buffer, sizeof(buffer), "g%X", glyphID);
      characterName = buffer;
    }
    glyphWidth = scaleContext->getAdvance(glyphID, false);
    encDiffs->appendName(characterName);
    widthArray->appendScalar(glyphWidth);

    auto glyphBBox = scaleContext->getBounds(glyphID, false, false);
    bbox.join(glyphBBox);

    auto content = MemoryWriteStream::Make();
    Path glyphPath;
    if (!typeface->hasColor() && scaleContext->generatePath(glyphID, false, false, &glyphPath)) {
      SetGlyphWidthAndBoundingBox(glyphWidth, glyphBBox, content);
      PDFUtils::EmitPath(glyphPath, true, content);
      PDFUtils::PaintPath(PathFillType::Winding, content);
    } else {
      SetGlyphWidthAndBoundingBox(glyphWidth, glyphBBox, content);
    }

    auto stream = Stream::MakeFromData(content->readData());
    charProcs->insertRef(std::move(characterName), PDFStreamOut(nullptr, std::move(stream), doc));
  }

  if (xObjects->size() || graphicStates->size()) {
    auto resources = PDFDictionary::Make();
    if (xObjects->size()) {
      resources->insertObject("XObject", std::move(xObjects));
    }
    if (graphicStates->size()) {
      resources->insertObject("ExtGState", std::move(graphicStates));
    }
    font.insertObject("Resources", std::move(resources));
  }

  encoding->insertObject("Differences", std::move(encDiffs));
  font.insertInt("FirstChar", 0);
  font.insertInt("LastChar", lastGlyphID - firstGlyphID + 1);
  /* FontBBox: "A rectangle expressed in the glyph coordinate
      system, specifying the font bounding box. This is the smallest
      rectangle enclosing the shape that would result if all of the
      glyphs of the font were placed with their origins coincident and
      then filled." */
  font.insertObject("FontBBox", MakePDFArray(bbox.left, bbox.bottom, bbox.right, bbox.top));

  font.insertName("CIDToGIDMap", "Identity");

  auto pathTypeface = pdfStrike.strikeSpec.typeface;
  const std::vector<Unichar>& glyphToUnicode = PDFFont::GetUnicodeMap(*pathTypeface, doc);
  DEBUG_ASSERT(glyphToUnicode.size() == static_cast<size_t>(pathTypeface->glyphsCount()));
  auto toUnicodeCmap =
      PDFMakeToUnicodeCmap(glyphToUnicode.data(), &subset, false, firstGlyphID, lastGlyphID);
  font.insertRef("ToUnicode", PDFStreamOut(nullptr, std::move(toUnicodeCmap), doc));
  font.insertRef("FontDescriptor", type3_descriptor(doc, pathTypeface, xHeight));
  font.insertObject("Widths", std::move(widthArray));
  font.insertObject("Encoding", std::move(encoding));
  font.insertObject("CharProcs", std::move(charProcs));

  doc->emit(font, indirectReference());
}

void PDFFont::emitSubset(PDFDocumentImpl* document) const {
  switch (fontType) {
    case AdvancedTypefaceInfo::FontType::Type1CID:
    case AdvancedTypefaceInfo::FontType::TrueType:
    case AdvancedTypefaceInfo::FontType::CFF:
      return emitSubsetType0(document);
    case AdvancedTypefaceInfo::FontType::Type1:
      return EmitSubsetType1(*this, document);
    default:
      return emitSubsetType3(document);
  }
}

}  // namespace tgfx