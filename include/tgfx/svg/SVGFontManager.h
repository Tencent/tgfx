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
      return std::hash<int>()(style.weight()) ^ std::hash<int>()(style.width()) ^
             std::hash<int>()(style.slant());
    }
  };

  enum Weight {
    kInvisible_Weight = 0,
    kThin_Weight = 100,
    kExtraLight_Weight = 200,
    kLight_Weight = 300,
    kNormal_Weight = 400,
    kMedium_Weight = 500,
    kSemiBold_Weight = 600,
    kBold_Weight = 700,
    kExtraBold_Weight = 800,
    kBlack_Weight = 900,
    kExtraBlack_Weight = 1000,
  };

  enum Width {
    kUltraCondensed_Width = 1,
    kExtraCondensed_Width = 2,
    kCondensed_Width = 3,
    kSemiCondensed_Width = 4,
    kNormal_Width = 5,
    kSemiExpanded_Width = 6,
    kExpanded_Width = 7,
    kExtraExpanded_Width = 8,
    kUltraExpanded_Width = 9,
  };

  enum Slant {
    kUpright_Slant,
    kItalic_Slant,
    kOblique_Slant,
  };

  constexpr FontStyle(Weight weight, Width width, Slant slant)
      : _value(weight + (width << 16) + (slant << 24)) {
  }

  constexpr FontStyle() : FontStyle{kNormal_Weight, kNormal_Width, kUpright_Slant} {
  }

  bool operator==(const FontStyle& rhs) const {
    return _value == rhs._value;
  }

  int weight() const {
    return _value & 0xFFFF;
  }
  int width() const {
    return (_value >> 16) & 0xFF;
  }
  Slant slant() const {
    return static_cast<Slant>((_value >> 24) & 0xFF);
  }

 private:
  int _value;
};

class SVGFontManager {
 public:
  SVGFontManager() = default;
  ~SVGFontManager() = default;

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
      _typefaces;
  std::shared_ptr<Typeface> _defaultTypeface;
};

}  // namespace tgfx