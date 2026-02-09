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

#include "core/ScalerContext.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/Point.h"

namespace tgfx {
/**
 * A Rasterizer that rasterizes a give Glyph.
 */
class GlyphRasterizer : public ImageCodec {
 public:
  /**
   * Creates a new GlyphRasterizer instance. Returns nullptr if scalerContext is null or dimensions
   * are invalid.
   */
  static std::shared_ptr<GlyphRasterizer> MakeFrom(int width, int height,
                                                   std::shared_ptr<ScalerContext> scalerContext,
                                                   GlyphID glyphID, bool fauxBold,
                                                   const Stroke* stroke, const Point& glyphOffset);

  GlyphRasterizer(int width, int height, std::shared_ptr<ScalerContext> scalerContext,
                  GlyphID glyphID, bool fauxBold, const Stroke* stroke, const Point& glyphOffset);

  ~GlyphRasterizer() override;

  bool isAlphaOnly() const override {
    return !scalerContext->hasColor();
  }

  bool asyncSupport() const override;

 protected:
  bool onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                    std::shared_ptr<ColorSpace> dstColorSpace, void* dstPixels) const override;

  std::shared_ptr<ScalerContext> scalerContext = nullptr;
  GlyphID glyphID = 0;
  bool fauxBold = false;
  Stroke* stroke = nullptr;
  Point glyphOffset = {};
};
}  // namespace tgfx
