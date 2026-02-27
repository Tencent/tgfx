/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "WebGlyphRasterizer.h"
#include "WebImageBuffer.h"
#include "WebScalerContext.h"
#include "core/AtlasTypes.h"

namespace tgfx {
std::shared_ptr<GlyphRasterizer> GlyphRasterizer::MakeFrom(
    int width, int height, std::shared_ptr<ScalerContext> scalerContext, GlyphID glyphID,
    bool fauxBold, const Stroke* stroke, const Point& glyphOffset) {
  if (scalerContext == nullptr || width <= 0 || height <= 0) {
    return nullptr;
  }
  if (scalerContext->asyncSupport()) {
    return std::make_shared<GlyphRasterizer>(width, height, std::move(scalerContext), glyphID,
                                             fauxBold, stroke, glyphOffset);
  }
  return std::make_shared<WebGlyphRasterizer>(width, height, std::move(scalerContext), glyphID,
                                              fauxBold, stroke, glyphOffset);
}

std::shared_ptr<ImageBuffer> WebGlyphRasterizer::onMakeBuffer(bool) const {
  auto webScalerContext = static_cast<WebScalerContext*>(scalerContext.get());
  auto canvas = webScalerContext->getGlyphCanvas(glyphID, fauxBold, stroke, Plot::CellPadding);
  if (!canvas.as<bool>()) {
    return nullptr;
  }
  return WebImageBuffer::MakeAdopted(canvas, isAlphaOnly());
}
}  // namespace tgfx
