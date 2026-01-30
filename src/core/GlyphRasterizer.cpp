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
#ifdef TGFX_BUILD_FOR_WEB
#include "platform/web/WebImageBuffer.h"
#include "platform/web/WebScalerContext.h"
#endif

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

bool GlyphRasterizer::asyncSupport() const {
  return scalerContext->asyncSupport();
}

std::shared_ptr<ImageBuffer> GlyphRasterizer::onMakeBuffer(bool tryHardware) const {
#ifdef TGFX_BUILD_FOR_WEB
  if (!scalerContext->asyncSupport()) {
    auto webScalerContext = static_cast<WebScalerContext*>(scalerContext.get());
    auto canvas = webScalerContext->getGlyphCanvas(glyphID, fauxBold, stroke);
    if (!canvas.isNull()) {
      // WebTypeface: always use RGBA to avoid getImageData extracting alpha channel.
      // The forceAsMask flag in shader will handle alpha-only rendering.
      return WebImageBuffer::MakeAdopted(canvas, false);
    }
  }
#endif
  return ImageCodec::onMakeBuffer(tryHardware);
}

bool GlyphRasterizer::onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                                   std::shared_ptr<ColorSpace> dstColorSpace,
                                   void* dstPixels) const {
  auto dstInfo =
      ImageInfo::Make(width(), height(), colorType, alphaType, dstRowBytes, dstColorSpace);
  return scalerContext->readPixels(glyphID, fauxBold, stroke, dstInfo, dstPixels, glyphOffset);
}
}  // namespace tgfx
