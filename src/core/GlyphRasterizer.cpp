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

#include "GlyphRasterizer.h"

namespace tgfx {
GlyphRasterizer::GlyphRasterizer(int width, int height,
                                 std::shared_ptr<ScalerContext> scalerContext, GlyphID glyphID,
                                 bool fauxBold, const Stroke* stroke, const Point& glyphOffset)
    : ImageCodec(width, height), scalerContext(std::move(scalerContext)), glyphID(glyphID),
      fauxBold(fauxBold), stroke(stroke ? new Stroke(*stroke) : nullptr), glyphOffset(glyphOffset) {
}

GlyphRasterizer::~GlyphRasterizer() {
  if (stroke) {
    delete stroke;
  }
}

bool GlyphRasterizer::onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                                   void* dstPixels) const {
  auto dstInfo =
      ImageInfo::Make(width(), height(), colorType, alphaType, dstRowBytes);
  return scalerContext->readPixels(glyphID, fauxBold, stroke, dstInfo, dstPixels, glyphOffset);
}
}  // namespace tgfx
