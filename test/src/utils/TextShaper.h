/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/Typeface.h"

namespace tgfx {
class PositionedGlyphs {
 public:
  PositionedGlyphs() = default;

  explicit PositionedGlyphs(
      std::vector<std::tuple<std::shared_ptr<Typeface>, GlyphID, uint32_t>> glyphs)
      : glyphs(std::move(glyphs)) {
  }

  std::shared_ptr<Typeface> getTypeface(size_t atIndex) const {
    return std::get<0>(glyphs[atIndex]);
  }

  GlyphID getGlyphID(size_t atIndex) const {
    return std::get<1>(glyphs[atIndex]);
  }

  uint32_t getStringIndex(size_t atIndex) const {
    return std::get<2>(glyphs[atIndex]);
  }

  size_t glyphCount() const {
    return glyphs.size();
  }

 private:
  std::vector<std::tuple<std::shared_ptr<Typeface>, GlyphID, uint32_t>> glyphs;
};

class TextShaper {
 public:
  static PositionedGlyphs Shape(const std::string& text, std::shared_ptr<Typeface> typeface);

  static void PurgeCaches();
};
}  // namespace tgfx
