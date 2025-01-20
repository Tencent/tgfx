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

SVGFontManager::SVGFontManager(const std::shared_ptr<Typeface>& defaultTypeface)
    : defaultTypeface(defaultTypeface) {
}

std::shared_ptr<SVGFontManager> SVGFontManager::Make(const std::shared_ptr<Typeface>& typeface) {
  if (!typeface) {
    return nullptr;
  }
  return std::shared_ptr<SVGFontManager>(new SVGFontManager(typeface));
}

void SVGFontManager::addFontStyle(const std::string& fontFamily, SVGFontInfo info) {
  if (typefaceMap.find(fontFamily) != typefaceMap.end()) {
    return;
  }
  if (typefaceMap[fontFamily].find(info) != typefaceMap[fontFamily].end()) {
    return;
  }
  typefaceMap[fontFamily][info] = nullptr;
}

bool SVGFontManager::setTypeface(const std::string& fontFamily, SVGFontInfo info,
                                 const std::shared_ptr<Typeface>& typeface) {
  if (typefaceMap.find(fontFamily) != typefaceMap.end()) {
    return false;
  }
  if (typefaceMap[fontFamily].find(info) != typefaceMap[fontFamily].end()) {
    return false;
  }
  typefaceMap[fontFamily][info] = typeface;
  return true;
}

std::vector<std::string> SVGFontManager::getFontFamilies() const {
  std::vector<std::string> families;
  families.reserve(typefaceMap.size());
  for (const auto& [family, _] : typefaceMap) {
    families.push_back(family);
  }
  return families;
}

std::vector<SVGFontInfo> SVGFontManager::getFontInfos(const std::string& fontFamily) const {
  if (auto iter = typefaceMap.find(fontFamily); iter != typefaceMap.end()) {
    std::vector<SVGFontInfo> styles;
    styles.reserve(iter->second.size());
    for (const auto& [style, _] : iter->second) {
      styles.push_back(style);
    }
    return styles;
  } else {
    return {};
  }
}

std::shared_ptr<Typeface> SVGFontManager::getTypefaceForRendering(const std::string& fontFamily,
                                                                  SVGFontInfo info) const {
  auto familyIter = typefaceMap.find(fontFamily);
  if (familyIter == typefaceMap.end()) {
    return defaultTypeface;
  }
  auto styleIter = familyIter->second.find(info);
  if (styleIter == familyIter->second.end()) {
    return defaultTypeface;
  } else {
    return styleIter->second;
  }
}

}  // namespace tgfx