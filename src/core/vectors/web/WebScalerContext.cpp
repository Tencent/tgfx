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

#include "WebScalerContext.h"
#include "ReadPixelsFromCanvasImage.h"
#include "WebTypeface.h"
#include "core/utils/ApplyStrokeToBounds.h"
#include "core/utils/Log.h"
#include "platform/web/WebImageBuffer.h"

using namespace emscripten;

namespace tgfx {
WebScalerContext::WebScalerContext(std::shared_ptr<Typeface> typeface, float size,
                                   val scalerContext)
    : ScalerContext(std::move(typeface), size), scalerContext(std::move(scalerContext)) {
}

FontMetrics WebScalerContext::getFontMetrics() const {
  return scalerContext.call<FontMetrics>("getFontMetrics");
}

Rect WebScalerContext::getBounds(GlyphID glyphID, bool fauxBold, bool fauxItalic) const {
  return scalerContext.call<Rect>("getBounds", getText(glyphID), fauxBold, fauxItalic);
}

float WebScalerContext::getAdvance(GlyphID glyphID, bool) const {
  return scalerContext.call<float>("getAdvance", getText(glyphID));
}

Point WebScalerContext::getVerticalOffset(GlyphID glyphID) const {
  FontMetrics metrics = getFontMetrics();
  auto advanceX = getAdvance(glyphID, false);
  return {-advanceX * 0.5f, metrics.capHeight};
}

bool WebScalerContext::generatePath(GlyphID, bool, bool, Path*) const {
  return false;
}

Rect WebScalerContext::getImageTransform(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                                         Matrix* matrix) const {
  auto bounds =
      scalerContext.call<Rect>("getBounds", getText(glyphID), !hasColor() && fauxBold, false);
  if (bounds.isEmpty()) {
    return {};
  }
  if (!hasColor() && stroke != nullptr) {
    ApplyStrokeToBounds(*stroke, &bounds, true);
  }
  if (matrix) {
    matrix->setTranslate(bounds.left, bounds.top);
  }
  return bounds;
}

bool WebScalerContext::readPixels(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                                  const ImageInfo& dstInfo, void* dstPixels) const {
  if (dstInfo.isEmpty() || dstPixels == nullptr) {
    return false;
  }
  auto properFauxBold = !hasColor() && fauxBold;
  auto bounds = scalerContext.call<Rect>("getBounds", getText(glyphID), properFauxBold, false);
  if (bounds.isEmpty()) {
    return false;
  }
  emscripten::val imageData = emscripten::val::null();
  if (!hasColor() && stroke != nullptr) {
    ApplyStrokeToBounds(*stroke, &bounds, true);
    imageData =
        scalerContext.call<val>("readPixels", getText(glyphID), bounds, properFauxBold, *stroke);
  } else {
    imageData = scalerContext.call<val>("readPixels", getText(glyphID), bounds, properFauxBold);
  }
  if (!imageData.as<bool>()) {
    return false;
  }
  return ReadPixelsFromCanvasImage(imageData, dstInfo, dstPixels);
}

std::string WebScalerContext::getText(GlyphID glyphID) const {
  return static_cast<WebTypeface*>(typeface.get())->getText(glyphID);
}
}  // namespace tgfx
