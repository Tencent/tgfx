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

#include "tgfx/core/FontManager.h"
#include <cstddef>
#include <memory>
#include "core/utils/Log.h"

namespace tgfx {

/**
* Width has the greatest priority.
* If the value of pattern.width is 5 (normal) or less,
*    narrower width values are checked first, then wider values.
* If the value of pattern.width is greater than 5 (normal),
*    wider values are checked first, followed by narrower values.
*
* Italic/Oblique has the next highest priority.
* If italic requested and there is some italic font, use it.
* If oblique requested and there is some oblique font, use it.
* If italic requested and there is some oblique font, use it.
* If oblique requested and there is some italic font, use it.
*
* Exact match.
* If pattern.weight < 400, weights below pattern.weight are checked
*   in descending order followed by weights above pattern.weight
*   in ascending order until a match is found.
* If pattern.weight > 500, weights above pattern.weight are checked
*   in ascending order followed by weights below pattern.weight
*   in descending order until a match is found.
* If pattern.weight is 400, 500 is checked first
*   and then the rule for pattern.weight < 400 is used.
* If pattern.weight is 500, 400 is checked first
*   and then the rule for pattern.weight < 400 is used.
*/
std::shared_ptr<Typeface> FontStyleSet::matchStyleCSS3(const FontStyle& pattern) {
  auto count = this->count();
  if (0 == count) {
    return nullptr;
  }

  struct Score {
    int score;
    int index;
    Score& operator+=(int rhs) {
      this->score += rhs;
      return *this;
    }
    Score& operator<<=(int rhs) {
      this->score <<= rhs;
      return *this;
    }
    bool operator<(const Score& that) const {
      return this->score < that.score;
    }
  };

  int patternWeight = static_cast<int>(pattern.weight());
  int patternWidth = static_cast<int>(pattern.width());
  int patternSlant = static_cast<int>(pattern.slant());

  Score maxScore = {0, 0};
  for (size_t i = 0; i < count; ++i) {
    FontStyle current;
    this->getStyle(i, &current, nullptr);
    int currentWeight = static_cast<int>(current.weight());
    int currentWidth = static_cast<int>(current.width());
    int currentSlant = static_cast<int>(current.slant());

    Score currentScore = {0, static_cast<int>(i)};

    // CSS stretch / FontStyle::Width
    // Takes priority over everything else.
    if (pattern.width() <= FontStyle::Width::Normal) {
      if (current.width() <= pattern.width()) {
        currentScore += 10 - patternWidth + static_cast<int>(current.width());
      } else {
        currentScore += 10 - static_cast<int>(current.width());
      }
    } else {
      if (current.width() > pattern.width()) {
        currentScore += 10 + patternWidth - currentWidth;
      } else {
        currentScore += currentWidth;
      }
    }
    currentScore <<= 8;

    // CSS style (normal, italic, oblique) / FontStyle::Slant (upright, italic, oblique)
    // Takes priority over all valid weights.
    DEBUG_ASSERT(0 <= patternSlant && patternSlant <= 2 && 0 <= currentSlant && currentSlant <= 2);
    static const int score[3][3] = {
        /*               Upright Italic Oblique  [current]*/
        /*   Upright */ {3, 1, 2},
        /*   Italic  */ {1, 3, 2},
        /*   Oblique */ {1, 2, 3},
        /* [pattern] */
    };
    currentScore += score[patternSlant][currentSlant];
    currentScore <<= 8;

    // CSS weight / FontStyle::Weight
    // The 'closer' to the target weight, the higher the score.
    // 1000 is the 'heaviest' recognized weight
    if (patternWeight == currentWeight) {
      currentScore += 1000;
      // less than 400 prefer lighter weights
    } else if (patternWeight < 400) {
      if (currentWeight <= patternWeight) {
        currentScore += 1000 - patternWeight + currentWeight;
      } else {
        currentScore += 1000 - currentWeight;
      }
      // between 400 and 500 prefer heavier up to 500, then lighter weights
    } else if (patternWeight <= 500) {
      if (currentWeight >= patternWeight && currentWeight <= 500) {
        currentScore += 1000 + patternWeight - currentWeight;
      } else if (current.weight() <= pattern.weight()) {
        currentScore += 500 + currentWeight;
      } else {
        currentScore += 1000 - currentWeight;
      }
      // greater than 500 prefer heavier weights
    } else if (patternWeight > 500) {
      if (currentWeight > patternWeight) {
        currentScore += 1000 + patternWeight - currentWeight;
      } else {
        currentScore += currentWeight;
      }
    }

    if (maxScore < currentScore) {
      maxScore = currentScore;
    }
  }

  return this->createTypeface(static_cast<size_t>(maxScore.index));
}

class EmptyFontStyleSet : public FontStyleSet {
 public:
  size_t count() override {
    return 0;
  }

  void getStyle(size_t, FontStyle*, std::string*) override {
  }

  std::shared_ptr<Typeface> createTypeface(size_t) override {
    return nullptr;
  }

  std::shared_ptr<Typeface> matchStyle(const FontStyle&) override {
    return nullptr;
  }
};

std::shared_ptr<FontStyleSet> FontStyleSet::CreateEmpty() {
  return std::shared_ptr<FontStyleSet>(new EmptyFontStyleSet);
}

namespace {
std::shared_ptr<FontStyleSet> emptyOnNull(std::shared_ptr<FontStyleSet>&& styleSet) {
  if (!styleSet) {
    styleSet = FontStyleSet::CreateEmpty();
  }
  return std::move(styleSet);
}
}  // anonymous namespace

size_t FontManager::countFamilies() const {
  return onCountFamilies();
}

std::string FontManager::getFamilyName(size_t index) const {
  return onGetFamilyName(index);
}

std::shared_ptr<FontStyleSet> FontManager::createStyleSet(size_t index) const {
  return emptyOnNull(onCreateStyleSet(index));
}

std::shared_ptr<FontStyleSet> FontManager::matchFamily(const std::string& familyName) const {
  return emptyOnNull(onMatchFamily(familyName));
}

std::shared_ptr<Typeface> FontManager::matchFamilyStyle(const std::string& familyName,
                                                        FontStyle style) const {
  return onMatchFamilyStyle(familyName, style);
}

std::shared_ptr<Typeface> FontManager::matchFamilyStyleCharacter(
    const std::string& familyName, FontStyle style, const std::vector<std::string>& bcp47s,
    Unichar character) const {
  return onMatchFamilyStyleCharacter(familyName, style, bcp47s, character);
}

}  // namespace tgfx