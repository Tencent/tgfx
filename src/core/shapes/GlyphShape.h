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
    // Must agree with the fill type onGetPath() reports, otherwise the GPU stencil pass and the
    // CPU rasterizer disagree about the same geometry. Resolved once in the constructor because
    // this is queried on every fill.
    return evenOddFill ? PathFillType::EvenOdd : PathFillType::Winding;
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
  // The typeface cannot change after construction, so the fill rule is fixed as well. getUniqueKey()
  // already covers the typeface ID, so a cached value can never go stale behind the key.
  bool evenOddFill = false;
};
}  // namespace tgfx
