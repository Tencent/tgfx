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

#include "core/ScalerContext.h"
#include "tgfx/core/ImageCodec.h"

namespace tgfx {
/**
 * A Rasterizer that rasterizes a give Glyph.
 */
class GlyphRasterizer : public ImageCodec {
 public:
  GlyphRasterizer(int width, int height, std::shared_ptr<ScalerContext> scalerContext,
                  GlyphID glyphID, bool fauxBold, const Stroke* stroke);

  ~GlyphRasterizer() override;

  bool isAlphaOnly() const override {
    return !scalerContext->hasColor();
  }

  bool readPixels(const ImageInfo& dstInfo, void* dstPixels) const override;

 private:
  std::shared_ptr<ScalerContext> scalerContext = nullptr;
  GlyphID glyphID = 0;
  bool fauxBold = false;
  Stroke* stroke = nullptr;
};
}  // namespace tgfx
