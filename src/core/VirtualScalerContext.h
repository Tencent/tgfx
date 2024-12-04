/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "ScalerContext.h"
#include "tgfx/core/VirtualTypeface.h"

namespace tgfx {
class VirtualScalerContext : public ScalerContext {
 public:
  explicit VirtualScalerContext(std::shared_ptr<Typeface> typeface, float size);

  FontMetrics getFontMetrics() const override;

  Rect getBounds(GlyphID glyphID, bool fauxBold, bool fauxItalic) const override;

  float getAdvance(GlyphID glyphID, bool verticalText) const override;

  Point getVerticalOffset(GlyphID glyphID) const override;

  bool generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic, Path* path) const override;

  Size getImageTransform(GlyphID glyphID, Matrix* matrix) const override;

  std::shared_ptr<ImageBuffer> generateImage(GlyphID glyphID, bool tryHardware) const override;

 private:
  static std::shared_ptr<VirtualTypeface> ConvertTypeface(
      const std::shared_ptr<Typeface>& typeface);
};
}  // namespace tgfx
