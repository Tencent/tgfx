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

#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>

namespace tgfx {

/**
 * Font style object
 */
class FontStyle {
 public:
  /**
   * Font weight numeric values ranging from 1 to 1000, corresponding to CSS font-weight values.
   * Only two keyword values are available: normal = 400 and bold = 700. Other enums use FreeType's
   * FT_WEIGHT_XXX values.
   * @see https://developer.mozilla.org/en-US/docs/Web/CSS/font-weight
   */
  enum class Weight {
    Invisible,   //0
    Thin,        //100
    ExtraLight,  //200
    Light,       //300
    Normal,      //400 normal
    Medium,      //500
    SemiBold,    //600
    Bold,        //700 bold
    ExtraBold,   //800
    Black,       //900
    ExtraBlack,  //1000
  };

  /**
   * Font width values corresponding to CSS font-stretch keyword property
   * @see https://developer.mozilla.org/en-US/docs/Web/CSS/font-stretch
   */
  enum class Width {
    UltraCondensed,  //ultra-condensed
    ExtraCondensed,  //extra-condensed
    Condensed,       //condensed
    SemiCondensed,   //semi-condensed
    Normal,          //normal
    SemiExpanded,    //semi-expanded
    Expanded,        //expanded
    ExtraExpanded,   //extra-expanded
    UltraExpanded,   //ultra-expanded
  };

  /**
   * Font slant values corresponding to CSS font-style keyword property
   * @see https://developer.mozilla.org/en-US/docs/Web/CSS/font-style
   */
  enum class Slant {
    Upright,  //normal
    Italic,   //italic
    Oblique,  //oblique
  };

  constexpr FontStyle(Weight weight, Width width, Slant slant)
      : value(static_cast<uint32_t>(std::clamp(weight, Weight::Invisible, Weight::ExtraBlack)) +
              (static_cast<uint32_t>(std::clamp(width, Width::UltraCondensed, Width::UltraExpanded))
               << 16) +
              (static_cast<uint32_t>(std::clamp(slant, Slant::Upright, Slant::Oblique)) << 24)) {
  }

  constexpr FontStyle() : FontStyle{Weight::Normal, Width::Normal, Slant::Upright} {
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

  static constexpr FontStyle Normal() {
    return FontStyle(Weight::Normal, Width::Normal, Slant::Upright);
  }

  static constexpr FontStyle Bold() {
    return FontStyle(Weight::Bold, Width::Normal, Slant::Upright);
  }

  static constexpr FontStyle Italic() {
    return FontStyle(Weight::Normal, Width::Normal, Slant::Italic);
  }

  static constexpr FontStyle BoldItalic() {
    return FontStyle(Weight::Bold, Width::Normal, Slant::Italic);
  }

 private:
  uint32_t value;
};

}  // namespace tgfx