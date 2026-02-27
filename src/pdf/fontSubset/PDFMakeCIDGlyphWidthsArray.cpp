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

#include "PDFMakeCIDGlyphWidthsArray.h"
#include <cstdint>
#include <type_traits>
#include "core/ScalerContext.h"
#include "pdf/PDFFont.h"
#include "pdf/PDFGlyphUse.h"
#include "pdf/PDFTypes.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
namespace {

// Scale from em-units to 1000-units.
float FromFontUnits(float scaled, uint16_t emSize) {
  if (emSize == 1000) {
    return scaled;
  } else {
    return scaled * 1000 / emSize;
  }
}

float FindModeOrZero(const std::vector<float>& advances) {
  if (advances.empty()) {
    return 0;
  }

  float currentAdvance = advances[0];
  float currentModeAdvance = advances[0];
  size_t currentCount = 1;
  size_t currentModeCount = 1;

  for (size_t i = 1; i < advances.size(); ++i) {
    if (advances[i] == currentAdvance) {
      ++currentCount;
    } else {
      if (currentCount > currentModeCount) {
        currentModeAdvance = currentAdvance;
        currentModeCount = currentCount;
      }
      currentAdvance = advances[i];
      currentCount = 1;
    }
  }
  return currentCount > currentModeCount ? currentAdvance : currentModeAdvance;
}

}  // namespace

std::unique_ptr<PDFArray> PDFMakeCIDGlyphWidthsArray(const PDFStrikeSpec& pdfStrikeSpec,
                                                     const PDFGlyphUse& subset,
                                                     int32_t* defaultAdvance) {
  // There are two ways of expressing advances
  //
  // range: " gfid [adv.ances adv.ances ... adv.ances]"
  //   run: " gfid gfid adv.ances"
  //
  // Assuming that on average
  // the ASCII representation of an advance plus a space is 10 characters
  // the ASCII representation of a glyph id plus a space is 4 characters
  // the ASCII representation of unused gid plus a space in a range is 2 characters
  //
  // When not in a range or run
  //  a. Skipping don't cares or defaults is a win (trivial)
  //  b. Run wins for 2+ repeats " gid gid adv.ances"
  //                             " gid [adv.ances adv.ances]"
  //     rule: 2+ repeats create run as long as possible, else start range
  //
  // When in a range
  // Cost of stopping and starting a range is 8 characters  "] gid ["
  //  c. Skipping defaults is always a win                  " adv.ances"
  //     rule: end range if default seen
  //  d. Skipping 4+ don't cares is a win                   " 0 0 0 0"
  //     rule: end range if 4+ don't cares
  // Cost of stop and start range plus run is 28 characters "] gid gid adv.ances gid ["
  //  e. Switching for 2+ repeats and 4+ don't cares wins   " 0 0 adv.ances 0 0 adv.ances"
  //     rule: end range for 2+ repeats with 4+ don't cares
  //  f. Switching for 3+ repeats wins                      " adv.ances adv.ances adv.ances"
  //     rule: end range for 3+ repeats

  auto emSize = static_cast<uint16_t>(pdfStrikeSpec.unitsPerEM);
  auto scaleContext = PDFFont::GetScalerContext(pdfStrikeSpec.typeface, pdfStrikeSpec.textSize);

  auto result = MakePDFArray();

  std::vector<GlyphID> glyphIDs;
  subset.getSetValues([&](size_t index) { glyphIDs.push_back(static_cast<GlyphID>(index)); });

  // Find the pdf integer mode (most common pdf integer advance).
  // Unfortunately, poppler enforces DW (default width) must be an integer,
  // so only consider integer pdf advances when finding the mode.
  size_t numIntAdvances = 0;
  std::vector<float> advances(glyphIDs.size());
  for (auto glyphID : glyphIDs) {
    float currentAdvance = FromFontUnits(scaleContext->getAdvance(glyphID, false), emSize);
    // 判断是否为整数
    if (std::floor(currentAdvance) == currentAdvance) {
      advances[numIntAdvances++] = currentAdvance;
    }
  }
  std::sort(advances.begin(), advances.end());
  auto modeAdvance = static_cast<int32_t>(FindModeOrZero(advances));
  *defaultAdvance = modeAdvance;

  // Pre-convert to pdf advances.
  for (size_t i = 0; i < glyphIDs.size(); ++i) {
    advances[i] = FromFontUnits(scaleContext->getAdvance(glyphIDs[i], false), emSize);
  }

  for (size_t i = 0; i < glyphIDs.size(); ++i) {
    float advance = advances[i];

    // a. Skipping don't cares or defaults is a win (trivial)
    if (static_cast<int32_t>(advance) == modeAdvance) {
      continue;
    }

    // b. 2+ repeats create run as long as possible, else start range
    {
      size_t j = i + 1;  // j is always one past the last known repeat
      for (; j < glyphIDs.size(); ++j) {
        float next_advance = advances[j];
        if (advance != next_advance) {
          break;
        }
      }
      if (j - i >= 2) {
        result->appendInt(glyphIDs[i]);
        result->appendInt(glyphIDs[j - 1]);
        result->appendScalar(advance);
        i = j - 1;
        continue;
      }
    }

    {
      result->appendInt(glyphIDs[i]);
      auto advanceArray = MakePDFArray();
      advanceArray->appendScalar(advance);
      size_t j = i + 1;  // j is always one past the last output
      for (; j < glyphIDs.size(); ++j) {
        advance = advances[j];

        // c. end range if default seen
        if (static_cast<int32_t>(advance) == modeAdvance) {
          break;
        }

        int dontCares = glyphIDs[j] - glyphIDs[j - 1] - 1;
        // d. end range if 4+ don't cares
        if (dontCares >= 4) {
          break;
        }

        float next_advance = 0;
        // e. end range for 2+ repeats with 4+ don't cares
        if (j + 1 < glyphIDs.size()) {
          next_advance = advances[j + 1];
          int next_dontCares = glyphIDs[j + 1] - glyphIDs[j] - 1;
          if (advance == next_advance && dontCares + next_dontCares >= 4) {
            break;
          }
        }

        // f. end range for 3+ repeats
        if (j + 2 < glyphIDs.size() && advance == next_advance) {
          next_advance = advances[j + 2];
          if (advance == next_advance) {
            break;
          }
        }

        while (dontCares-- > 0) {
          advanceArray->appendScalar(0);
        }
        advanceArray->appendScalar(advance);
      }
      result->appendObject(std::move(advanceArray));
      i = j - 1;
    }
  }

  return result;
};

}  // namespace tgfx
