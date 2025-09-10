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

#include "tgfx/svg/TextShaper.h"
#include "tgfx/core/UTF.h"

namespace tgfx {

class TextShaperPrimitive : public TextShaper {
 public:
  explicit TextShaperPrimitive(std::vector<std::shared_ptr<Typeface>> fallbackTypefaceList)
      : fallbackTypefaces(std::move(fallbackTypefaceList)) {
  }

  std::shared_ptr<TextBlob> shape(const std::string& text, std::shared_ptr<Typeface> typeface,
                                  float fontSize) override {
    std::vector<GlyphRun> glyphRuns = {};
    std::vector<GlyphID> glyphIDs = {};
    std::vector<Point> positions = {};

    Font previousFont = Font();
    auto emptyAdvance = fontSize / 2.0f;
    float XOffset = 0.0f;

    const char* textStart = text.data();
    const char* textStop = textStart + text.size();
    while (textStart < textStop) {
      const auto oldPosition = textStart;
      UTF::NextUTF8(&textStart, textStop);
      auto length = textStart - oldPosition;
      auto str = std::string(oldPosition, static_cast<size_t>(length));

      GlyphID currentGlyphID = 0;
      std::shared_ptr<Typeface> currentTypeface = nullptr;

      // Match the string with SVG-parsed typeface, and try fallback typefaces if no match
      // is found
      GlyphID glyphID = typeface ? typeface->getGlyphID(str) : 0;
      if (glyphID != 0) {
        currentGlyphID = glyphID;
        currentTypeface = typeface;
      } else {
        for (const auto& typefaceItem : fallbackTypefaces) {
          if (typefaceItem == nullptr) {
            continue;
          }
          glyphID = typefaceItem->getGlyphID(str);
          if (glyphID != 0) {
            currentGlyphID = glyphID;
            currentTypeface = typefaceItem;
            break;
          }
        }
      }

      // Skip with empty advance if no matching typeface is found
      if (!currentTypeface) {
        XOffset += emptyAdvance;
        continue;
      }

      if (!previousFont.getTypeface()) {
        previousFont = Font(currentTypeface, fontSize);
      } else if (currentTypeface->uniqueID() != previousFont.getTypeface()->uniqueID()) {
        // If the current typeface differs from the previous one, create a glyph run from current
        // glyphs and start a new font
        glyphRuns.emplace_back(previousFont, glyphIDs, positions);
        glyphIDs.clear();
        positions.clear();
        previousFont = Font(currentTypeface, fontSize);
      }

      glyphIDs.emplace_back(currentGlyphID);
      positions.emplace_back(Point::Make(XOffset, 0.0f));
      XOffset += previousFont.getAdvance(currentGlyphID);
    }

    if (!glyphIDs.empty()) {
      glyphRuns.emplace_back(previousFont, glyphIDs, positions);
    }
    return glyphRuns.empty() ? nullptr : TextBlob::MakeFrom(glyphRuns);
  }

 private:
  std::vector<std::shared_ptr<Typeface>> fallbackTypefaces;
};

std::shared_ptr<TextShaper> TextShaper::Make(
    std::vector<std::shared_ptr<Typeface>> fallbackTypefaceList) {
  if (fallbackTypefaceList.empty()) {
    return nullptr;
  }
  return std::make_shared<TextShaperPrimitive>(std::move(fallbackTypefaceList));
}

}  // namespace tgfx