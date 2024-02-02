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

#include "CGTypeface.h"
#include "core/PixelBuffer.h"
#include "tgfx/core/ImageInfo.h"

namespace tgfx {
class CGScalerContext {
 public:
  static std::unique_ptr<CGScalerContext> Make(std::shared_ptr<Typeface> typeface, float size);

  ~CGScalerContext();

  FontMetrics generateFontMetrics();

  GlyphMetrics generateGlyphMetrics(GlyphID glyphID, bool fauxBold, bool fauxItalic,
                                    bool verticalText = false);

  Point getVerticalOffset(GlyphID glyphID, bool fauxBold, bool fauxItalic) const;

  bool generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic, Path* path);

  std::shared_ptr<ImageBuffer> generateImage(GlyphID glyphID, bool fauxItalic, Matrix* matrix);

 private:
  CGScalerContext(std::shared_ptr<Typeface> typeface, float size);

  std::shared_ptr<Typeface> typeface = nullptr;
  float textSize = 1.0f;
  float fauxBoldScale = 1.0f;
  CTFontRef ctFont = nullptr;
};
}  // namespace tgfx
