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

#include <memory>
#include "ft2build.h"
#include FT_FREETYPE_H
#include "FTTypeface.h"
#include "core/PixelBuffer.h"

namespace tgfx {
class FTScalerContext {
 public:
  static std::unique_ptr<FTScalerContext> Make(std::shared_ptr<FTTypeface> typeface, float size);

  ~FTScalerContext();

  FontMetrics generateFontMetrics(bool fauxBold, bool fauxItalic);

  GlyphMetrics generateGlyphMetrics(GlyphID glyphID, bool fauxBold, bool fauxItalic,
                                    bool verticalText = false);

  bool generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic, Path* path);

  std::shared_ptr<ImageBuffer> generateImage(GlyphID glyphID, bool fauxBold, bool fauxItalic,
                                             Matrix* matrix);

 private:
  FTScalerContext(std::shared_ptr<FTTypeface> typeFace, float textSize);

  bool valid() const {
    return typeface != nullptr && ftSize != nullptr;
  }

  int setupSize(bool fauxItalic);

  bool getCBoxForLetter(char letter, bool fauxBold, FT_BBox* bbox);

  void getBBoxForCurrentGlyph(FT_BBox* bbox);

  Matrix getExtraMatrix(bool fauxItalic);

  std::shared_ptr<FTTypeface> typeface = nullptr;
  float textSize = 1.0f;
  Point extraScale = Point::Make(1.f, 1.f);
  FT_Size ftSize = nullptr;
  FT_Int strikeIndex = -1;  // The bitmap strike for the face (or -1 if none).
  FT_Int32 loadGlyphFlags = 0;
};
}  // namespace tgfx
