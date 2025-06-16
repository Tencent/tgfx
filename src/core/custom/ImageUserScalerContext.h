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

#include "ImageUserTypeface.h"
#include "UserScalerContext.h"

namespace tgfx {
class ImageUserScalerContext final : public UserScalerContext {
 public:
  ImageUserScalerContext(std::shared_ptr<Typeface> typeface, float size);

  Rect getBounds(GlyphID glyphID, bool fauxBold, bool fauxItalic) const override;

  bool generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic, Path* path) const override;

  Rect getImageTransform(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                         Matrix* matrix) const override;

  bool readPixels(GlyphID glyphID, bool fauxBold, const Stroke* stroke, const ImageInfo& dstInfo,
                  void* dstPixels) const override;

 private:
  ImageUserTypeface* imageTypeface() const;

  Point extraScale = Point::Make(1.f, 1.f);
};
}  // namespace tgfx
