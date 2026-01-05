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

#include "PDFType1Font.h"
#include <memory>
#include "core/AdvancedTypefaceInfo.h"
#include "core/ScalerContext.h"
#include "core/utils/Log.h"
#include "pdf/PDFDocumentImpl.h"
#include "pdf/PDFFont.h"
#include "pdf/PDFTypes.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {

namespace {
bool ParsePFBSection(const uint8_t** src, size_t* len, int sectionType, size_t* size) {
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

bool ParsePFB(const uint8_t* src, size_t size, size_t* headerLen, size_t* dataLen,
              size_t* trailerLen) {
  const uint8_t* srcPtr = src;
  size_t remaining = size;

  return ParsePFBSection(&srcPtr, &remaining, 1, headerLen) &&
         ParsePFBSection(&srcPtr, &remaining, 2, dataLen) &&
         ParsePFBSection(&srcPtr, &remaining, 1, trailerLen) &&
         ParsePFBSection(&srcPtr, &remaining, 3, nullptr);
}

bool ParsePFA(const char* src, size_t size, size_t* headerLen, size_t* hexDataLen, size_t* dataLen,
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
    if (nullptr == strchr("0123456789abcdefABCDEF", *dataPos)) {
      return false;
    }
    nibbles++;
  }
  *dataLen = static_cast<size_t>((nibbles + 1) / 2);

  return true;
}

int8_t HexToBin(uint8_t c) {
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

std::shared_ptr<Data> ConvertType1FontStream(const std::unique_ptr<Stream>& sourceStream,
                                             size_t* headerLength, size_t* dataLength,
                                             size_t* trailerLength) {
  size_t sourceLength = sourceStream ? sourceStream->size() : 0;
  DEBUG_ASSERT(sourceLength);
  if (!sourceLength) {
    return nullptr;
  }
  Buffer sourceBuffer(sourceLength);
  if (!sourceStream->read(sourceBuffer.data(), sourceLength)) {
    return nullptr;
  }
  const auto source = sourceBuffer.bytes();
  if (ParsePFB(source, sourceLength, headerLength, dataLength, trailerLength)) {
    constexpr int PFBSectionHeaderLength = 6;
    const size_t length = *headerLength + *dataLength + *trailerLength;
    DEBUG_ASSERT(length > 0);
    DEBUG_ASSERT(length + (2 * PFBSectionHeaderLength) <= sourceLength);

    const uint8_t* const sourceHeader = source + PFBSectionHeaderLength;
    // There is a six-byte section header before header and data (but not trailer) that we're not
    // going to copy.
    const uint8_t* const sourceData = sourceHeader + *headerLength + PFBSectionHeaderLength;
    const uint8_t* const srcTrailer = sourceData + *headerLength;

    auto data = malloc(length);
    auto const resultHeader = static_cast<uint8_t*>(data);
    uint8_t* const resultData = resultHeader + *headerLength;
    uint8_t* const resultTrailer = resultData + *dataLength;

    DEBUG_ASSERT(resultTrailer + *trailerLength == resultHeader + length);

    memcpy(resultHeader, sourceHeader, *headerLength);
    memcpy(resultData, sourceData, *dataLength);
    memcpy(resultTrailer, srcTrailer, *trailerLength);

    return Data::MakeAdopted(data, length, Data::FreeProc);
  }

  // A PFA has to be converted for PDF.
  size_t hexDataLen;
  if (!ParsePFA(reinterpret_cast<const char*>(source), sourceLength, headerLength, &hexDataLen,
                dataLength, trailerLength)) {
    return nullptr;
  }
  const size_t length = *headerLength + *dataLength + *trailerLength;
  DEBUG_ASSERT(length > 0);
  auto data = malloc(length);
  auto buffer = static_cast<uint8_t*>(data);

  memcpy(buffer, source, *headerLength);
  uint8_t* const resultData = &(buffer[*headerLength]);

  const uint8_t* hexData = source + *headerLength;
  const uint8_t* trailer = hexData + hexDataLen;
  size_t outputOffset = 0;
  uint8_t dataByte = 0;  // To hush compiler.
  bool highNibble = true;
  for (; hexData < trailer; hexData++) {
    int8_t curNibble = HexToBin(*hexData);
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
  DEBUG_ASSERT(outputOffset == *dataLength);

  uint8_t* const resultTrailer = &(buffer[*headerLength + outputOffset]);
  memcpy(resultTrailer, source + *headerLength + hexDataLen, *trailerLength);

  return Data::MakeAdopted(data, length, Data::FreeProc);
}

inline bool CanEmbed(const AdvancedTypefaceInfo& advancedInfo) {
  return !(advancedInfo.flags & AdvancedTypefaceInfo::FontFlags::NotEmbeddable);
}

inline float FromFontUnits(float scaled, uint16_t emSize) {
  return emSize == 1000 ? scaled : scaled * 1000 / emSize;
}

PDFIndirectReference MakeType1FontDescriptor(PDFDocumentImpl* document,
                                             const PDFStrikeSpec& pdfStrikeSpec,
                                             const AdvancedTypefaceInfo* info) {
  auto descriptor = PDFDictionary::Make("FontDescriptor");
  auto emSize = static_cast<uint16_t>(std::round(pdfStrikeSpec.unitsPerEM));
  if (info) {
    PDFFont::PopulateCommonFontDescriptor(descriptor.get(), *info, emSize, 0);
    if (CanEmbed(*info)) {
      size_t header = 0;
      size_t data = 0;
      size_t trailer = 0;
      auto typeface = pdfStrikeSpec.typeface;
      auto rawFontData = PDFFont::GetTypefaceStream(typeface);
      auto fontData = ConvertType1FontStream(std::move(rawFontData), &header, &data, &trailer);
      if (fontData) {
        auto dict = PDFDictionary::Make();
        dict->insertInt("Length1", header);
        dict->insertInt("Length2", data);
        dict->insertInt("Length3", trailer);
        auto fontStream = Stream::MakeFromData(std::move(fontData));
        descriptor->insertRef("FontFile", PDFStreamOut(std::move(dict), std::move(fontStream),
                                                       document, PDFSteamCompressionEnabled::Yes));
      }
    }
  }
  return document->emit(*descriptor);
}

const std::vector<std::string>& Type1GlyphNames(PDFDocumentImpl* canon,
                                                const std::shared_ptr<Typeface>& typeface) {
  auto typefaceID = typeface->uniqueID();
  auto iter = canon->type1GlyphNames.find(typefaceID);
  if (iter == canon->type1GlyphNames.end()) {
    // TODO (YGaurora)
    std::vector<std::string> names(typeface->glyphsCount());
    canon->type1GlyphNames[typefaceID] = names;
  }
  return iter->second;
}

PDFIndirectReference Type1FontDescriptor(PDFDocumentImpl* doc, const PDFStrikeSpec& pdfStrikeSpec) {
  auto typeface = pdfStrikeSpec.typeface;
  auto textSize = pdfStrikeSpec.textSize;
  auto typefaceID = typeface->uniqueID();
  auto iter = doc->fontDescriptors.find(typefaceID);
  if (iter != doc->fontDescriptors.end()) {
    return iter->second;
  }
  const auto info = PDFFont::GetAdvancedInfo(typeface, textSize, doc);
  auto fontDescriptor = MakeType1FontDescriptor(doc, pdfStrikeSpec, info);
  doc->fontDescriptors[typefaceID] = fontDescriptor;
  return fontDescriptor;
}
}  // namespace

void EmitSubsetType1(const PDFFont& pdfFont, PDFDocumentImpl* document) {
  auto typeface = pdfFont.strike().strikeSpec.typeface;
  auto textSize = pdfFont.strike().strikeSpec.textSize;
  auto glyphNames = Type1GlyphNames(document, typeface);
  auto scaleContext = PDFFont::GetScalerContext(typeface, pdfFont.strike().strikeSpec.textSize);

  GlyphID firstGlyphID = pdfFont.firstGlyphID();
  GlyphID lastGlyphID = pdfFont.lastGlyphID();

  auto fontDictionary = PDFDictionary::Make("Font");
  fontDictionary->insertRef("FontDescriptor",
                            Type1FontDescriptor(document, pdfFont.strike().strikeSpec));
  fontDictionary->insertName("Subtype", "Type1");
  if (const auto info = PDFFont::GetAdvancedInfo(typeface, textSize, document)) {
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
    for (size_t i = 0; i < glyphRangeSize; ++i) {
      widths->appendScalar(FromFontUnits(scaleContext->getAdvance(glyphIDs[i], false),
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
