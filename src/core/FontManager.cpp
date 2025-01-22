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