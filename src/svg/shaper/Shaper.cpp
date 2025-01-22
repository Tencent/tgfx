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

#include "tgfx/svg/shaper/Shaper.h"
#include <locale>
#include <memory>
#include <string>
#include "core/utils/Log.h"
#include "svg/shaper/ShaperPrimitive.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/FontManager.h"
#include "tgfx/core/UTF.h"
#include "tgfx/svg/shaper/Shaper.h"

namespace tgfx {
class FontManagerRunIterator final : public FontRunIterator {
 public:
  FontManagerRunIterator(const char* utf8, size_t utf8Bytes, const Font& inputfont,
                         std::shared_ptr<FontManager> fallbackMgr, const char* requestName,
                         FontStyle requestStyle, const LanguageRunIterator* lang)
      : current(utf8), begin(utf8), end(current + utf8Bytes),
        fallbackManager(std::move(fallbackMgr)), font(inputfont), fallbackFont(inputfont),
        requestName(requestName), requestStyle(requestStyle), languageIter(lang) {
    // If fallback is not wanted, clients should use TrivialFontRunIterator.
    fallbackFont.setTypeface(nullptr);
  }

  void consume() override {
    ASSERT(current < end);
    Unichar unichar = UTF::NextUTF8(&current, end);
    // If the starting typeface can handle this character, use it.
    if (font.getGlyphID(unichar)) {
      _currentFont = &font;
      // If the current fallback can handle this character, use it.
    } else if (fallbackFont.getTypeface() && fallbackFont.getGlyphID(unichar)) {
      _currentFont = &fallbackFont;
      // If not, try to find a fallback typeface
    } else {
      std::vector<std::string> languages;
      if (languageIter) {
        languages.push_back(languageIter->currentLanguage());
      }
      auto candidate =
          fallbackManager->matchFamilyStyleCharacter(requestName, requestStyle, languages, unichar);
      if (candidate) {
        fallbackFont.setTypeface(std::move(candidate));
        _currentFont = &fallbackFont;
      } else {
        _currentFont = &font;
      }
    }

    while (current < end) {
      const char* prev = current;
      unichar = UTF::NextUTF8(&current, end);

      // End run if not using initial typeface and initial typeface has this character.
      if (_currentFont->getTypeface() != font.getTypeface() && font.getGlyphID(unichar)) {
        current = prev;
        return;
      }

      // End run if current typeface does not have this character and some other font does.
      if (!_currentFont->getGlyphID(unichar)) {
        std::vector<std::string> languages;
        if (languageIter) {
          languages.push_back(languageIter->currentLanguage());
        }
        auto candidate = fallbackManager->matchFamilyStyleCharacter(requestName, requestStyle,
                                                                    languages, unichar);
        if (candidate) {
          current = prev;
          return;
        }
      }
    }
  }

  size_t endOfCurrentRun() const override {
    return static_cast<size_t>(current - begin);
  }

  bool atEnd() const override {
    return current == end;
  }

  const Font& currentFont() const override {
    return *_currentFont;
  }

 private:
  const char* current = nullptr;
  const char* const begin = nullptr;
  const char* const end = nullptr;
  std::shared_ptr<const FontManager> fallbackManager;
  Font font;
  Font fallbackFont;
  Font* _currentFont = nullptr;
  const char* const requestName;
  const FontStyle requestStyle;
  LanguageRunIterator const* const languageIter;
};

std::unique_ptr<FontRunIterator> Shaper::MakeFontMgrRunIterator(
    const char* utf8, size_t utf8Bytes, const Font& font, std::shared_ptr<FontManager> fallback) {
  return std::make_unique<FontManagerRunIterator>(utf8, utf8Bytes, font, std::move(fallback),
                                                  nullptr, font.getTypeface()->getFontStyle(),
                                                  nullptr);
}

std::unique_ptr<LanguageRunIterator> Shaper::MakeStdLanguageRunIterator(const char* /*utf8*/,
                                                                        size_t utf8Bytes) {
  return std::make_unique<TrivialLanguageRunIterator>(std::locale().name().c_str(), utf8Bytes);
}

}  // namespace tgfx