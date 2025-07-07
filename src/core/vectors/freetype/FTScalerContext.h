/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <memory>
#include "ft2build.h"
#include FT_FREETYPE_H
#include "FTTypeface.h"
#include "core/PixelBuffer.h"
#include "core/ScalerContext.h"

namespace tgfx {
class FTScalerContext : public ScalerContext {
 public:
  FTScalerContext(std::shared_ptr<Typeface> typeFace, float textSize);

  ~FTScalerContext() override;

  FontMetrics getFontMetrics() const override;

  Rect getBounds(GlyphID glyphID, bool fauxBold, bool fauxItalic) const override;

  float getAdvance(GlyphID glyphID, bool verticalText) const override;

  Point getVerticalOffset(GlyphID glyphID) const override;

  bool generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic, Path* path) const override;

  Rect getImageTransform(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                         Matrix* matrix) const override;

  bool readPixels(GlyphID glyphID, bool fauxBold, const Stroke* stroke, const ImageInfo& dstInfo,
                  void* dstPixels) const override;

 private:
  int setupSize(bool fauxItalic) const;

  void getFontMetricsInternal(FontMetrics* metrics) const;

  float getAdvanceInternal(GlyphID glyphID, bool verticalText = false) const;

  bool getCBoxForLetter(char letter, FT_BBox* bbox) const;

  void getBBoxForCurrentGlyph(FT_BBox* bbox) const;

  bool loadBitmapGlyph(GlyphID glyphID, FT_Int32 glyphFlags) const;

  Matrix getExtraMatrix(bool fauxItalic) const;

  FTTypeface* ftTypeface() const;

  bool loadOutlineGlyph(FT_Face face, GlyphID glyphID, bool fauxBold, bool fauxItalic) const;

  float textScale = 1.0f;
  Point extraScale = Point::Make(1.f, 1.f);
  FT_Size ftSize = nullptr;
  FT_Int strikeIndex = -1;  // The bitmap strike for the face (or -1 if none).
  FT_Int32 loadGlyphFlags = 0;
};
}  // namespace tgfx
