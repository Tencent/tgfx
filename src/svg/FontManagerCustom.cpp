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

#include "tgfx/svg/FontManagerCustom.h"
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include "core/utils/Log.h"
#include "tgfx/core/FontStyle.h"

namespace tgfx {

FontStyleSetCustom::FontStyleSetCustom(std::string familyName) : familyName(std::move(familyName)) {
}

void FontStyleSetCustom::appendTypeface(std::shared_ptr<Typeface> typeface) {
  styles.emplace_back(std::move(typeface));
}

size_t FontStyleSetCustom::count() {
  return styles.size();
}

void FontStyleSetCustom::getStyle(size_t /*index*/, FontStyle* style, std::string* name) {
  if (style) {
    *style = FontStyle::Normal();
  }
  if (name) {
    *name = "";
  }
}

std::shared_ptr<Typeface> FontStyleSetCustom::createTypeface(size_t index) {
  if (styles.size() <= index) {
    return nullptr;
  }
  return styles[index];
}

std::shared_ptr<Typeface> FontStyleSetCustom::matchStyle(const FontStyle& style) {
  std::shared_ptr<Typeface> typeface = nullptr;
  for (const auto& item : styles) {
    if (item->getFontStyle() == style) {
      return typeface = item;
    }
  }
  return typeface;
}

std::string FontStyleSetCustom::getFamilyName() {
  return familyName;
}

FontManagerCustom::FontManagerCustom(const FontLoader& loader) : defaultFamily(nullptr) {

  loader.loadFonts(families);

  // Try to pick a default font.
  static const std::array<std::string, 6> defaultNames = {
      "Arial", "Verdana", "Times New Roman", "Droid Sans", "DejaVu Serif", ""};
  for (const auto& defaultName : defaultNames) {
    auto set = this->onMatchFamily(defaultName);
    if (!set) {
      continue;
    }

    auto style =
        FontStyle(FontStyle::Weight::Normal, FontStyle::Width::Normal, FontStyle::Slant::Upright);
    auto typeface = set->matchStyle(style);
    if (!typeface) {
      continue;
    }

    defaultFamily = set;
    break;
  }
  if (nullptr == defaultFamily) {
    defaultFamily = families[0];
  }
}

size_t FontManagerCustom::onCountFamilies() const {
  return families.size();
}

std::string FontManagerCustom::onGetFamilyName(size_t index) const {
  DEBUG_ASSERT(index < families.size());
  return families[index]->getFamilyName();
}

std::shared_ptr<FontStyleSet> FontManagerCustom::onCreateStyleSet(size_t index) const {
  DEBUG_ASSERT(index < families.size());
  return families[index];
}

std::shared_ptr<FontStyleSet> FontManagerCustom::onMatchFamily(
    const std::string& familyName) const {
  for (const auto& family : families) {
    if (family->getFamilyName() == familyName) {
      return family;
    }
  }
  return nullptr;
}

std::shared_ptr<Typeface> FontManagerCustom::onMatchFamilyStyle(const std::string& familyName,
                                                                FontStyle style) const {
  if (auto set = this->matchFamily(familyName)) {
    return set->matchStyle(style);
  }
  return defaultFamily->matchStyle(style);
}

std::shared_ptr<Typeface> FontManagerCustom::onMatchFamilyStyleCharacter(
    const std::string&, FontStyle, const std::vector<std::string>&, Unichar) const {
  return nullptr;
}

}  // namespace tgfx