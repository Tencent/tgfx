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

#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>

namespace tgfx {
/**
 * Font weight numeric values ranging from 1 to 1000, corresponding to CSS font-weight values.
 * @see https://developer.mozilla.org/en-US/docs/Web/CSS/font-weight
 */
enum class FontWeight {
  Invisible,   // 0
  Thin,        // 100
  ExtraLight,  // 200
  Light,       // 300
  Normal,      // 400
  Medium,      // 500
  SemiBold,    // 600
  Bold,        // 700
  ExtraBold,   // 800
  Black,       // 900
  ExtraBlack,  // 1000
};

/**
 * Font width values corresponding to CSS font-stretch keyword property
 * @see https://developer.mozilla.org/en-US/docs/Web/CSS/font-stretch
 */
enum class FontWidth {
  UltraCondensed,
  ExtraCondensed,
  Condensed,
  SemiCondensed,
  Normal,
  SemiExpanded,
  Expanded,
  ExtraExpanded,
  UltraExpanded,
};

/**
 * Font slant values corresponding to CSS font-style keyword property
 * @see https://developer.mozilla.org/en-US/docs/Web/CSS/font-style
 */
enum class FontSlant {
  Upright,
  Italic,
  Oblique,
};

/**
 * Font traits for stylistic information.
 */
class FontStyle {
 public:
  constexpr FontStyle(FontWeight weight, FontWidth width, FontSlant slant)
      : value(static_cast<uint32_t>(
                  std::clamp(weight, FontWeight::Invisible, FontWeight::ExtraBlack)) +
              (static_cast<uint32_t>(
                   std::clamp(width, FontWidth::UltraCondensed, FontWidth::UltraExpanded))
               << 16) +
              (static_cast<uint32_t>(std::clamp(slant, FontSlant::Upright, FontSlant::Oblique))
               << 24)) {
  }

  constexpr FontStyle() : FontStyle(FontWeight::Normal, FontWidth::Normal, FontSlant::Upright) {
  }

  bool operator==(const FontStyle& rhs) const {
    return value == rhs.value;
  }

  FontWeight weight() const {
    return static_cast<FontWeight>(value & 0xFFFF);
  }

  FontWidth width() const {
    return static_cast<FontWidth>((value >> 16) & 0xFF);
  }

  FontSlant slant() const {
    return static_cast<FontSlant>((value >> 24) & 0xFF);
  }

  static constexpr FontStyle Normal() {
    return FontStyle(FontWeight::Normal, FontWidth::Normal, FontSlant::Upright);
  }

  static constexpr FontStyle Bold() {
    return FontStyle(FontWeight::Bold, FontWidth::Normal, FontSlant::Upright);
  }

  static constexpr FontStyle Italic() {
    return FontStyle(FontWeight::Normal, FontWidth::Normal, FontSlant::Italic);
  }

  static constexpr FontStyle BoldItalic() {
    return FontStyle(FontWeight::Bold, FontWidth::Normal, FontSlant::Italic);
  }

 private:
  uint32_t value = 0;
};

}  // namespace tgfx
