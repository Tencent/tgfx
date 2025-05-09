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

#include "PDFType1Font.h"
#include "core/ScalerContext.h"
#include "core/utils/Log.h"
#include "pdf/PDFDocument.h"
#include "pdf/PDFFont.h"
#include "pdf/PDFTypes.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {

namespace {
bool parsePFBSection(const uint8_t** src, size_t* len, int sectionType, size_t* size) {
  // PFB sections have a two or six bytes header. 0x80 and a one byte
  // section type followed by a four byte section length.  Type one is
  // an ASCII section (includes a length), type two is a binary section
  // (includes a length) and type three is an EOF marker with no length.
  const uint8_t* buf = *src;
  if (*len < 2 || buf[0] != 0x80 || buf[1] != sectionType) {
    return false;
  }
  if (buf[1] == 3) {
    return true;
  }
  if (*len < 6) {
    return false;
  }

  *size = static_cast<size_t>(buf[2]) | (static_cast<size_t>(buf[3]) << 8) |
          (static_cast<size_t>(buf[4]) << 16) | (static_cast<size_t>(buf[5]) << 24);
  size_t consumed = *size + 6;
  if (consumed > *len) {
    return false;
  }
  *src = *src + consumed;
  *len = *len - consumed;
  return true;
}

bool parsePFB(const uint8_t* src, size_t size, size_t* headerLen, size_t* dataLen,
              size_t* trailerLen) {
  const uint8_t* srcPtr = src;
  size_t remaining = size;

  return parsePFBSection(&srcPtr, &remaining, 1, headerLen) &&
         parsePFBSection(&srcPtr, &remaining, 2, dataLen) &&
         parsePFBSection(&srcPtr, &remaining, 1, trailerLen) &&
         parsePFBSection(&srcPtr, &remaining, 3, nullptr);
}

/* The sections of a PFA file are implicitly defined.  The body starts
 * after the line containing "eexec," and the trailer starts with 512
 * literal 0's followed by "cleartomark" (plus arbitrary white space).
 *
 * This function assumes that src is NUL terminated, but the NUL
 * termination is not included in size.
 *
 */
bool parsePFA(const char* src, size_t size, size_t* headerLen, size_t* hexDataLen, size_t* dataLen,
              size_t* trailerLen) {
  const char* end = src + size;

  const char* dataPos = strstr(src, "eexec");
  if (!dataPos) {
    return false;
  }
  dataPos += strlen("eexec");
  while ((*dataPos == '\n' || *dataPos == '\r' || *dataPos == ' ') && dataPos < end) {
    dataPos++;
  }
  *headerLen = static_cast<size_t>(dataPos - src);

  const char* trailerPos = strstr(dataPos, "cleartomark");
  if (!trailerPos) {
    return false;
  }
  int zeroCount = 0;
  for (trailerPos--; trailerPos > dataPos && zeroCount < 512; trailerPos--) {
    if (*trailerPos == '\n' || *trailerPos == '\r' || *trailerPos == ' ') {
      continue;
    } else if (*trailerPos == '0') {
      zeroCount++;
    } else {
      return false;
    }
  }
  if (zeroCount != 512) {
    return false;
  }

  *hexDataLen = static_cast<size_t>(trailerPos - src) - *headerLen;
  *trailerLen = size - *headerLen - *hexDataLen;

  // Verify that the data section is hex encoded and count the bytes.
  int nibbles = 0;
  for (; dataPos < trailerPos; dataPos++) {
    if (isspace(*dataPos)) {
      continue;
    }
    // isxdigit() is locale-sensitive https://bugs.skia.org/8285
    if (nullptr == strchr("0123456789abcdefABCDEF", *dataPos)) {
      return false;
    }
    nibbles++;
  }
  *dataLen = static_cast<size_t>((nibbles + 1) / 2);

  return true;
}

int8_t hexToBin(uint8_t c) {
  if (!isxdigit(c)) {
    return -1;
  } else if (c <= '9') {
    return static_cast<int8_t>(c - '0');
  } else if (c <= 'F') {
    return static_cast<int8_t>(c - 'A' + 10);
  } else if (c <= 'f') {
    return static_cast<int8_t>(c - 'a' + 10);
  }
  return -1;
}

std::shared_ptr<Data> convert_type1_font_stream(const std::shared_ptr<Data>& srcData,
                                                size_t* headerLen, size_t* dataLen,
                                                size_t* trailerLen) {
  size_t srcLen = srcData ? srcData->size() : 0;
  DEBUG_ASSERT(srcLen);
  if (!srcLen) {
    return nullptr;
  }
  const auto* src = static_cast<const uint8_t*>(srcData->data());

  if (parsePFB(src, srcLen, headerLen, dataLen, trailerLen)) {
    constexpr int PFBSectionHeaderLength = 6;
    const size_t length = *headerLen + *dataLen + *trailerLen;
    DEBUG_ASSERT(length > 0);
    DEBUG_ASSERT(length + (2 * PFBSectionHeaderLength) <= srcLen);

    const uint8_t* const srcHeader = src + PFBSectionHeaderLength;
    // There is a six-byte section header before header and data
    // (but not trailer) that we're not going to copy.
    const uint8_t* const srcData = srcHeader + *headerLen + PFBSectionHeaderLength;
    const uint8_t* const srcTrailer = srcData + *headerLen;

    auto* data = malloc(length);
    auto* const resultHeader = static_cast<uint8_t*>(data);
    uint8_t* const resultData = resultHeader + *headerLen;
    uint8_t* const resultTrailer = resultData + *dataLen;

    DEBUG_ASSERT(resultTrailer + *trailerLen == resultHeader + length);

    memcpy(resultHeader, srcHeader, *headerLen);
    memcpy(resultData, srcData, *dataLen);
    memcpy(resultTrailer, srcTrailer, *trailerLen);

    return Data::MakeAdopted(data, length, Data::FreeProc);
  }

  // A PFA has to be converted for PDF.
  size_t hexDataLen;
  if (!parsePFA(reinterpret_cast<const char*>(src), srcLen, headerLen, &hexDataLen, dataLen,
                trailerLen)) {
    return nullptr;
  }
  const size_t length = *headerLen + *dataLen + *trailerLen;
  DEBUG_ASSERT(length > 0);
  auto* data = malloc(length);
  auto* buffer = static_cast<uint8_t*>(data);

  memcpy(buffer, src, *headerLen);
  uint8_t* const resultData = &(buffer[*headerLen]);

  const uint8_t* hexData = src + *headerLen;
  const uint8_t* trailer = hexData + hexDataLen;
  size_t outputOffset = 0;
  uint8_t dataByte = 0;  // To hush compiler.
  bool highNibble = true;
  for (; hexData < trailer; hexData++) {
    int8_t curNibble = hexToBin(*hexData);
    if (curNibble < 0) {
      continue;
    }
    if (highNibble) {
      dataByte = static_cast<uint8_t>(static_cast<uint8_t>(curNibble) << 4);
      highNibble = false;
    } else {
      dataByte |= curNibble;
      highNibble = true;
      resultData[outputOffset++] = dataByte;
    }
  }
  if (!highNibble) {
    resultData[outputOffset++] = dataByte;
  }
  DEBUG_ASSERT(outputOffset == *dataLen);

  uint8_t* const resultTrailer = &(buffer[*headerLen + outputOffset]);
  memcpy(resultTrailer, src + *headerLen + hexDataLen, *trailerLen);

  return Data::MakeAdopted(data, length, Data::FreeProc);
}

inline bool can_embed(const FontMetrics& metrics) {
  return !(metrics.flags & FontMetrics::FontFlags::NotEmbeddable);
}

inline float from_font_units(float scaled, uint16_t emSize) {
  return emSize == 1000 ? scaled : scaled * 1000 / emSize;
}

PDFIndirectReference make_type1_font_descriptor(PDFDocument* doc,
                                                const PDFStrikeSpec& pdfStrikeSpec,
                                                const FontMetrics* info) {
  auto descriptor = PDFDictionary::Make("FontDescriptor");
  auto emSize = static_cast<uint16_t>(std::round(pdfStrikeSpec.unitsPerEM));
  if (info) {
    PDFFont::PopulateCommonFontDescriptor(descriptor.get(), *info, emSize, 0);
    if (can_embed(*info)) {
      size_t header = 0;
      size_t data = 0;
      size_t trailer = 0;
      auto typeface = pdfStrikeSpec.typeface;
      auto rawFontData = PDFFont::GetTypefaceData(typeface);
      auto fontData = convert_type1_font_stream(std::move(rawFontData), &header, &data, &trailer);
      if (fontData) {
        auto dict = PDFDictionary::Make();
        dict->insertInt("Length1", header);
        dict->insertInt("Length2", data);
        dict->insertInt("Length3", trailer);
        auto fontStream = Stream::MakeFromData(std::move(fontData));
        descriptor->insertRef("FontFile", PDFStreamOut(std::move(dict), std::move(fontStream), doc,
                                                       PDFSteamCompressionEnabled::Yes));
      }
    }
  }
  return doc->emit(*descriptor);
}

const std::vector<std::string>& type_1_glyph_names(PDFDocument* canon,
                                                   const std::shared_ptr<Typeface>& typeface) {
  auto typefaceID = typeface->uniqueID();
  auto iter = canon->type1GlyphNames.find(typefaceID);
  if (iter == canon->type1GlyphNames.end()) {
    std::vector<std::string> names(typeface->glyphsCount());
    PDFFont::GetType1GlyphNames(*typeface, names.data());
    canon->type1GlyphNames[typefaceID] = std::move(names);
  }
  return iter->second;
}

PDFIndirectReference type1_font_descriptor(PDFDocument* doc, const PDFStrikeSpec& pdfStrikeSpec) {
  auto typeface = pdfStrikeSpec.typeface;
  auto textSize = pdfStrikeSpec.textSize;
  auto typefaceID = typeface->uniqueID();
  auto iter = doc->fontDescriptors.find(typefaceID);
  if (iter != doc->fontDescriptors.end()) {
    return iter->second;
  }
  const FontMetrics* info = PDFFont::GetMetrics(typeface, textSize, doc);
  auto fontDescriptor = make_type1_font_descriptor(doc, pdfStrikeSpec, info);
  doc->fontDescriptors[typefaceID] = fontDescriptor;
  return fontDescriptor;
}
}  // namespace

void EmitSubsetType1(const PDFFont& pdfFont, PDFDocument* document) {
  auto typeface = pdfFont.strike().strikeSpec.typeface;
  auto textSize = pdfFont.strike().strikeSpec.textSize;
  auto glyphNames = type_1_glyph_names(document, typeface);
  auto scaleContext = ScalerContext::Make(typeface, pdfFont.strike().strikeSpec.textSize);

  GlyphID firstGlyphID = pdfFont.firstGlyphID();
  GlyphID lastGlyphID = pdfFont.lastGlyphID();

  auto fontDictionary = PDFDictionary::Make("Font");
  fontDictionary->insertRef("FontDescriptor",
                            type1_font_descriptor(document, pdfFont.strike().strikeSpec));
  fontDictionary->insertName("Subtype", "Type1");
  if (const auto* info = PDFFont::GetMetrics(typeface, textSize, document)) {
    fontDictionary->insertName("BaseFont", info->postScriptName);
  }

  // glyphCount not including glyph 0
  unsigned glyphCount = 1 + lastGlyphID - firstGlyphID;
  DEBUG_ASSERT((glyphCount > 0 && glyphCount <= 255));
  fontDictionary->insertInt("FirstChar", static_cast<size_t>(0));
  fontDictionary->insertInt("LastChar", static_cast<size_t>(glyphCount));
  {
    int emSize = static_cast<int>(pdfFont.strike().strikeSpec.unitsPerEM);
    auto widths = MakePDFArray();

    size_t glyphRangeSize = lastGlyphID - firstGlyphID + 2;
    std::vector<GlyphID> glyphIDs(glyphRangeSize);
    glyphIDs[0] = 0;
    for (GlyphID glyphID = firstGlyphID; glyphID <= lastGlyphID; glyphID++) {
      glyphIDs[glyphID - firstGlyphID + 1] = glyphID;
    }
    // SkBulkGlyphMetrics metrics{pdfFont.strike().fPath.fStrikeSpec};
    // auto glyphs = metrics.glyphs(SkSpan(glyphIDs.get(), glyphRangeSize));
    for (size_t i = 0; i < glyphRangeSize; ++i) {
      widths->appendScalar(from_font_units(scaleContext->getAdvance(glyphIDs[i], false),
                                           static_cast<uint16_t>(emSize)));
    }
    fontDictionary->insertObject("Widths", std::move(widths));
  }
  auto encDiffs = MakePDFArray();
  encDiffs->reserve(lastGlyphID - firstGlyphID + 3);
  encDiffs->appendInt(0);

  DEBUG_ASSERT(glyphNames.size() > lastGlyphID);
  const char* unknown = "UNKNOWN";
  encDiffs->appendName(glyphNames[0].empty() ? unknown : glyphNames[0]);
  for (GlyphID glyphID = firstGlyphID; glyphID <= lastGlyphID; glyphID++) {
    encDiffs->appendName(glyphNames[glyphID].empty() ? unknown : glyphNames[glyphID]);
  }

  auto encoding = PDFDictionary::Make("Encoding");
  encoding->insertObject("Differences", std::move(encDiffs));
  fontDictionary->insertObject("Encoding", std::move(encoding));

  document->emit(*fontDictionary, pdfFont.indirectReference());
}

}  // namespace tgfx