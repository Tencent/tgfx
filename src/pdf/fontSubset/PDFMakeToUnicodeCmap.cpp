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

#include "PDFMakeToUnicodeCmap.h"
#include <cstddef>
#include <memory>
#include <string>
#include "pdf/PDFGlyphUse.h"
#include "pdf/PDFUtils.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {

namespace {
void append_tounicode_header(std::shared_ptr<MemoryWriteStream> cmap, bool multibyte) {
  // 12 dict begin: 12 is an Adobe-suggested value. Shall not change.
  // It's there to prevent old version Adobe Readers from malfunctioning.
  const char* kHeader =
      "/CIDInit /ProcSet findresource begin\n"
      "12 dict begin\n"
      "begincmap\n";
  cmap->writeText(kHeader);

  // The /CIDSystemInfo must be consistent to the one in
  // SkPDFFont::populateCIDFont().
  // We can not pass over the system info object here because the format is
  // different. This is not a reference object.
  const char* kSysInfo =
      "/CIDSystemInfo\n"
      "<<  /Registry (Adobe)\n"
      "/Ordering (UCS)\n"
      "/Supplement 0\n"
      ">> def\n";
  cmap->writeText(kSysInfo);

  // The CMapName must be consistent to /CIDSystemInfo above.
  // /CMapType 2 means ToUnicode.
  // Codespace range just tells the PDF processor the valid range.
  const char* kTypeInfoHeader =
      "/CMapName /Adobe-Identity-UCS def\n"
      "/CMapType 2 def\n"
      "1 begincodespacerange\n";
  cmap->writeText(kTypeInfoHeader);
  if (multibyte) {
    cmap->writeText("<0000> <FFFF>\n");
  } else {
    cmap->writeText("<00> <FF>\n");
  }
  cmap->writeText("endcodespacerange\n");
}

static void append_cmap_footer(std::shared_ptr<MemoryWriteStream> cmap) {
  const char kFooter[] =
      "endcmap\n"
      "CMapName currentdict /CMap defineresource pop\n"
      "end\n"
      "end";
  cmap->writeText(kFooter);
}

namespace {
struct BFChar {
  GlyphID fGlyphId;
  Unichar fUnicode;
};

struct BFRange {
  GlyphID fStart;
  GlyphID fEnd;
  Unichar fUnicode;
};
}  // namespace

void write_glyph(const std::shared_ptr<MemoryWriteStream>& cmap, bool multiByte, GlyphID gid) {
  if (multiByte) {
    PDFUtils::WriteUInt16BE(cmap, gid);
  } else {
    PDFUtils::WriteUInt8(cmap, static_cast<uint8_t>(gid));
  }
}

void append_bfchar_section(const std::vector<BFChar>& bfChar, bool multiByte,
                           const std::shared_ptr<MemoryWriteStream>& cmap) {
  // PDF spec defines that every bf* list can have at most 100 entries.
  for (size_t i = 0; i < bfChar.size(); i += 100) {
    int count = static_cast<int>(bfChar.size() - i);
    count = std::min(count, 100);
    cmap->writeText(std::to_string(count));
    cmap->writeText(" beginbfchar\n");
    for (size_t j = 0; j < static_cast<size_t>(count); ++j) {
      cmap->writeText("<");
      write_glyph(cmap, multiByte, bfChar[i + j].fGlyphId);
      cmap->writeText("> <");
      PDFUtils::WriteUTF16beHex(cmap, bfChar[i + j].fUnicode);
      cmap->writeText(">\n");
    }
    cmap->writeText("endbfchar\n");
  }
}

void append_bfrange_section(const std::vector<BFRange>& bfRange, bool multiByte,
                            const std::shared_ptr<MemoryWriteStream>& cmap) {
  // PDF spec defines that every bf* list can have at most 100 entries.
  for (size_t i = 0; i < bfRange.size(); i += 100) {
    int count = static_cast<int>(bfRange.size() - i);
    count = std::min(count, 100);
    cmap->writeText(std::to_string(count));
    cmap->writeText(" beginbfrange\n");
    for (size_t j = 0; j < static_cast<size_t>(count); ++j) {
      cmap->writeText("<");
      write_glyph(cmap, multiByte, bfRange[i + j].fStart);
      cmap->writeText("> <");
      write_glyph(cmap, multiByte, bfRange[i + j].fEnd);
      cmap->writeText("> <");
      PDFUtils::WriteUTF16beHex(cmap, bfRange[i + j].fUnicode);
      cmap->writeText(">\n");
    }
    cmap->writeText("endbfrange\n");
  }
}

// Generate <bfchar> and <bfrange> table according to PDF spec 1.4 and Adobe
// Technote 5014.
// The function is not static so we can test it in unit tests.
//
// Current implementation guarantees bfchar and bfrange entries do not overlap.
//
// Current implementation does not attempt aggressive optimizations against
// following case because the specification is not clear.
//
// 4 beginbfchar          1 beginbfchar
// <0003> <0013>          <0020> <0014>
// <0005> <0015>    to    endbfchar
// <0007> <0017>          1 beginbfrange
// <0020> <0014>          <0003> <0007> <0013>
// endbfchar              endbfrange
//
// Adobe Technote 5014 said: "Code mappings (unlike codespace ranges) may
// overlap, but succeeding maps supersede preceding maps."
//
// In case of searching text in PDF, bfrange will have higher precedence so
// typing char id 0x0014 in search box will get glyph id 0x0004 first.  However,
// the spec does not mention how will this kind of conflict being resolved.
//
// For the worst case (having 65536 continuous unicode and we use every other
// one of them), the possible savings by aggressive optimization is 416KB
// pre-compressed and does not provide enough motivation for implementation.
void append_cmap_sections(const Unichar* glyphToUnicode, const PDFGlyphUse* subset,
                          const std::shared_ptr<MemoryWriteStream>& cmap, bool multiByteGlyphs,
                          GlyphID firstGlyphID, GlyphID lastGlyphID) {
  int glyphOffset = 0;
  if (!multiByteGlyphs) {
    glyphOffset = firstGlyphID - 1;
  }

  std::vector<BFChar> bfcharEntries;
  std::vector<BFRange> bfrangeEntries;

  BFRange currentRangeEntry = {0, 0, 0};
  bool rangeEmpty = true;
  const int limit = static_cast<int>(lastGlyphID) + 1 - glyphOffset;

  for (int i = firstGlyphID - glyphOffset; i < limit + 1; ++i) {
    auto glyphID = static_cast<GlyphID>(i + glyphOffset);
    bool inSubset = i < limit && (subset == nullptr || subset->has(glyphID));
    if (!rangeEmpty) {
      // PDF spec requires bfrange not changing the higher byte,
      // e.g. <1035> <10FF> <2222> is ok, but
      //      <1035> <1100> <2222> is no good
      bool inRange =
          i == currentRangeEntry.fEnd + 1 && i >> 8 == currentRangeEntry.fStart >> 8 && i < limit &&
          glyphToUnicode[glyphID] == currentRangeEntry.fUnicode + i - currentRangeEntry.fStart;
      if (!inSubset || !inRange) {
        if (currentRangeEntry.fEnd > currentRangeEntry.fStart) {
          bfrangeEntries.push_back(currentRangeEntry);
        } else {
          bfcharEntries.push_back({currentRangeEntry.fStart, currentRangeEntry.fUnicode});
        }
        rangeEmpty = true;
      }
    }
    if (inSubset) {
      currentRangeEntry.fEnd = static_cast<GlyphID>(i);
      if (rangeEmpty) {
        currentRangeEntry.fStart = static_cast<GlyphID>(i);
        currentRangeEntry.fUnicode = glyphToUnicode[glyphID];
        rangeEmpty = false;
      }
    }
  }

  // The spec requires all bfchar entries for a font must come before bfrange
  // entries.
  append_bfchar_section(bfcharEntries, multiByteGlyphs, cmap);
  append_bfrange_section(bfrangeEntries, multiByteGlyphs, cmap);
}
}  // namespace

std::unique_ptr<Stream> PDFMakeToUnicodeCmap(const Unichar* glyphToUnicode,
                                             const PDFGlyphUse* subset, bool multiByteGlyphs,
                                             GlyphID firstGlyphID, GlyphID lastGlyphID) {
  auto cmap = MemoryWriteStream::Make();
  append_tounicode_header(cmap, multiByteGlyphs);
  append_cmap_sections(glyphToUnicode, subset, cmap, multiByteGlyphs, firstGlyphID, lastGlyphID);
  append_cmap_footer(cmap);
  return Stream::MakeFromData(cmap->readData());
}

}  // namespace tgfx