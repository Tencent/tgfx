/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/svg/SVGFontManager.h"

namespace tgfx {

bool SVGFontManager::setDefaultTypeface(const std::shared_ptr<Typeface>& typeface) {
  if (typeface) {
    defaultTypeface = typeface;
    return true;
  }
  return false;
};

void SVGFontManager::addFontStyle(const std::string& fontFamily, FontStyle style) {
  if (typefaceMap.at(fontFamily).find(style) == typefaceMap.at(fontFamily).end()) {
    typefaceMap[fontFamily][style] = nullptr;
  }
}

void SVGFontManager::setTypeface(const std::string& fontFamily, FontStyle style,
                                 const std::shared_ptr<Typeface>& typeface) {
  typefaceMap[fontFamily][style] = typeface;
}

std::vector<std::string> SVGFontManager::getFontFamilies() const {
  std::vector<std::string> families;
  families.reserve(typefaceMap.size());
  for (const auto& [family, _] : typefaceMap) {
    families.push_back(family);
  }
  return families;
}

std::vector<FontStyle> SVGFontManager::getFontStyles(const std::string& fontFamily) const {
  if (auto iter = typefaceMap.find(fontFamily); iter != typefaceMap.end()) {
    std::vector<FontStyle> styles;
    styles.reserve(iter->second.size());
    for (const auto& [style, _] : iter->second) {
      styles.push_back(style);
    }
    return styles;
  } else {
    return {};
  }
}

std::shared_ptr<Typeface> SVGFontManager::getTypefaceForConfig(const std::string& fontFamily,
                                                               FontStyle style) const {
  auto familyIter = typefaceMap.find(fontFamily);
  if (familyIter == typefaceMap.end()) {
    return nullptr;
  }
  auto styleIter = familyIter->second.find(style);
  if (styleIter == familyIter->second.end()) {
    return nullptr;
  } else {
    return styleIter->second;
  }
}

std::shared_ptr<Typeface> SVGFontManager::getTypefaceForRender(const std::string& fontFamily,
                                                               FontStyle style) const {
  auto familyIter = typefaceMap.find(fontFamily);
  if (familyIter == typefaceMap.end()) {
    return defaultTypeface;
  }
  auto styleIter = familyIter->second.find(style);
  if (styleIter == familyIter->second.end()) {
    return defaultTypeface;
  } else {
    return styleIter->second;
  }
}

}  // namespace tgfx