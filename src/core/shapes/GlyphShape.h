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

#include "tgfx/core/Shape.h"

namespace tgfx {
/**
 * Shape that contains a Font and GlyphID.
 */
class GlyphShape : public Shape {
 public:
  explicit GlyphShape(Font font, GlyphID glyphID);

  PathFillType fillType() const override {
    // Reports the rule the glyph outline itself carries. A typeface cannot state one rule for all
    // of its glyphs: the path operations behind faux bold can change it, so the rule is read from
    // the outline. onGetPath() records it, so a caller that only asks about the rule - OpsCompositor,
    // while it is recording - reads it back instead of computing the path again. A cold call still
    // has to build the path once; there is no cheaper source for the rule.
    auto cached = cachedFillType.load(std::memory_order_acquire);
    if (cached >= 0) {
      return static_cast<PathFillType>(cached);
    }
    return getPath().getFillType();
  }

  Rect onGetBounds() const override;

 protected:
  Type type() const override {
    return Type::Glyph;
  }

  UniqueKey getUniqueKey() const override;

  Path onGetPath(float resolutionScale) const override;

 private:
  Font font;
  GlyphID glyphID = 0;
  // The rule onGetPath() produced, or -1 before it has run. Shapes are immutable, so the rule
  // cannot change once known. Mirrors the way Shape caches its path and bounds.
  mutable std::atomic<int> cachedFillType = {-1};
};
}  // namespace tgfx
