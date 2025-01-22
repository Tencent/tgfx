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
  enum class Weight {
    Invisible = 0,
    Thin = 100,
    ExtraLight = 200,
    Light = 300,
    Normal = 400,
    Medium = 500,
    SemiBold = 600,
    Bold = 700,
    ExtraBold = 800,
    Black = 900,
    ExtraBlack = 1000,
  };

  enum class Width {
    UltraCondensed = 1,
    ExtraCondensed = 2,
    Condensed = 3,
    SemiCondensed = 4,
    Normal = 5,
    SemiExpanded = 6,
    Expanded = 7,
    ExtraExpanded = 8,
    UltraExpanded = 9,
  };

  enum class Slant {
    Upright,
    Italic,
    Oblique,
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