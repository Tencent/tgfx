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
#include "tgfx/core/TextBlobBuilder.h"
#include "tgfx/core/UTF.h"

namespace tgfx {

class TextShaperPrimitive : public TextShaper {
 public:
  explicit TextShaperPrimitive(std::vector<std::shared_ptr<Typeface>> fallbackTypefaceList)
      : fallbackTypefaces(std::move(fallbackTypefaceList)) {
  }

  std::shared_ptr<TextBlob> shape(const std::string& text, std::shared_ptr<Typeface> typeface,
                                  float fontSize) override {
    std::vector<GlyphID> glyphs;
    std::vector<Point> positions;
    Font currentFont = Font();
    TextBlobBuilder builder;

    auto emptyAdvance = fontSize / 2.0f;
    float xOffset = 0.0f;

    const char* textStart = text.data();
    const char* textStop = textStart + text.size();
    while (textStart < textStop) {
      const auto oldPosition = textStart;
      UTF::NextUTF8(&textStart, textStop);
      auto length = textStart - oldPosition;
      auto str = std::string(oldPosition, static_cast<size_t>(length));

      GlyphID glyphID = 0;
      std::shared_ptr<Typeface> matchedTypeface = nullptr;

      // Match the string with SVG-parsed typeface, and try fallback typefaces if no match is found
      GlyphID id = typeface ? typeface->getGlyphID(str) : 0;
      if (id != 0) {
        glyphID = id;
        matchedTypeface = typeface;
      } else {
        for (const auto& fallback : fallbackTypefaces) {
          if (fallback == nullptr) {
            continue;
          }
          id = fallback->getGlyphID(str);
          if (id != 0) {
            glyphID = id;
            matchedTypeface = fallback;
            break;
          }
        }
      }

      // Skip with empty advance if no matching typeface is found
      if (!matchedTypeface) {
        xOffset += emptyAdvance;
        continue;
      }

      auto newFont = Font(matchedTypeface, fontSize);
      if (!currentFont.getTypeface()) {
        currentFont = newFont;
      } else if (matchedTypeface->uniqueID() != currentFont.getTypeface()->uniqueID()) {
        // Flush current run and start a new one
        if (!glyphs.empty()) {
          const auto& buffer = builder.allocRunPos(currentFont, glyphs.size());
          memcpy(buffer.glyphs, glyphs.data(), glyphs.size() * sizeof(GlyphID));
          memcpy(buffer.positions, positions.data(), positions.size() * sizeof(Point));
          glyphs.clear();
          positions.clear();
        }
        currentFont = newFont;
      }

      glyphs.push_back(glyphID);
      positions.push_back(Point::Make(xOffset, 0.0f));
      xOffset += currentFont.getAdvance(glyphID);
    }

    // Flush remaining glyphs
    if (!glyphs.empty()) {
      const auto& buffer = builder.allocRunPos(currentFont, glyphs.size());
      memcpy(buffer.glyphs, glyphs.data(), glyphs.size() * sizeof(GlyphID));
      memcpy(buffer.positions, positions.data(), positions.size() * sizeof(Point));
    }

    return builder.build();
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
