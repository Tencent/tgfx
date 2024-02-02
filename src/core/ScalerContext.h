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
static constexpr float ITALIC_SKEW = -0.20f;

class ScalerContext {
 public:
  static std::shared_ptr<ScalerContext> MakeEmpty(float size);

  static std::shared_ptr<ScalerContext> Make(std::shared_ptr<Typeface> typeface, float size);

  virtual ~ScalerContext() = default;

  std::shared_ptr<Typeface> getTypeface() const {
    return typeface;
  }

  float getSize() const {
    return textSize;
  }

  virtual FontMetrics getFontMetrics() const = 0;

  virtual Rect getBounds(GlyphID glyphID, bool fauxBold, bool fauxItalic) const = 0;

  virtual float getAdvance(GlyphID glyphID, bool verticalText) const = 0;

  virtual Point getVerticalOffset(GlyphID glyphID) const = 0;

  virtual bool generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic, Path* path) const = 0;

  virtual std::shared_ptr<ImageBuffer> generateImage(GlyphID glyphID, bool fauxItalic,
                                                     Matrix* matrix) const = 0;

 protected:
  std::shared_ptr<Typeface> typeface = nullptr;
  float textSize = 0.0f;

  ScalerContext(std::shared_ptr<Typeface> typeface, float size);

 private:
  static std::shared_ptr<ScalerContext> CreateNew(std::shared_ptr<Typeface> typeface, float size);
};
}  // namespace tgfx
