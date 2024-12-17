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

#pragma once

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>
#include "tgfx/core/Typeface.h"
namespace tgfx {

class FontStyle {
 public:
  struct Hash {
    std::size_t operator()(const FontStyle& style) const {
      return std::hash<int>()(static_cast<int>(style.weight())) ^
             std::hash<int>()(static_cast<int>(style.width())) ^
             std::hash<int>()(static_cast<int>(style.slant()));
    }
  };

  enum class Weight {
    Invisible_Weight = 0,
    Thin_Weight = 100,
    ExtraLight_Weight = 200,
    Light_Weight = 300,
    Normal_Weight = 400,
    Medium_Weight = 500,
    SemiBold_Weight = 600,
    Bold_Weight = 700,
    ExtraBold_Weight = 800,
    Black_Weight = 900,
    ExtraBlack_Weight = 1000,
  };

  enum class Width {
    UltraCondensed_Width = 1,
    ExtraCondensed_Width = 2,
    Condensed_Width = 3,
    SemiCondensed_Width = 4,
    Normal_Width = 5,
    SemiExpanded_Width = 6,
    Expanded_Width = 7,
    ExtraExpanded_Width = 8,
    UltraExpanded_Width = 9,
  };

  enum class Slant {
    Upright_Slant,
    Italic_Slant,
    Oblique_Slant,
  };

  constexpr FontStyle(Weight weight, Width width, Slant slant)
      : value(static_cast<int>(weight) + (static_cast<int>(width) << 16) +
              (static_cast<int>(slant) << 24)) {
  }

  constexpr FontStyle()
      : FontStyle{Weight::Normal_Weight, Width::Normal_Width, Slant::Upright_Slant} {
  }

  bool operator==(const FontStyle& rhs) const {
    return value == rhs.value;
  }

  Weight weight() const {
    return static_cast<Weight>(value & 0xFFFF);
  }
  Width width() const {
    return static_cast<Width>((value >> 16) & 0xFF);
  }
  Slant slant() const {
    return static_cast<Slant>((value >> 24) & 0xFF);
  }

 private:
  int value;
};

class SVGFontManager {
 public:
  SVGFontManager() = default;
  ~SVGFontManager() = default;

  /**
   * Set the Default Typeface object
   * @param typeface 
   * @return true 
   * @return false 
   */
  bool setDefaultTypeface(const std::shared_ptr<Typeface>& typeface);

  void addFontStyle(const std::string& fontFamily, FontStyle style);

  void setTypeface(const std::string& fontFamily, FontStyle style,
                   const std::shared_ptr<Typeface>& typeface);

  std::vector<std::string> getFontFamilies() const;

  std::vector<FontStyle> getFontStyles(const std::string& fontFamily) const;

  std::shared_ptr<Typeface> getTypefaceForConfig(const std::string& fontFamily,
                                                 FontStyle style) const;

  std::shared_ptr<Typeface> getTypefaceForRender(const std::string& fontFamily,
                                                 FontStyle style) const;

 private:
  std::unordered_map<std::string,
                     std::unordered_map<FontStyle, std::shared_ptr<Typeface>, FontStyle::Hash>>
      typefaceMap;
  std::shared_ptr<Typeface> defaultTypeface;
};

}  // namespace tgfx