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
#include "WebTypeface.h"
#include "core/utils/Log.h"
#include "platform/web/WebImageBuffer.h"
#include "tgfx/core/Buffer.h"

using namespace emscripten;

namespace tgfx {

std::shared_ptr<ScalerContext> ScalerContext::CreateNew(std::shared_ptr<Typeface> typeface,
                                                        float size) {
  DEBUG_ASSERT(typeface != nullptr);
  auto scalerContextClass = val::module_property("ScalerContext");
  if (!scalerContextClass.as<bool>()) {
    return nullptr;
  }
  auto scalerContext = scalerContextClass.new_(typeface->fontFamily(), typeface->fontStyle(), size);
  if (!scalerContext.as<bool>()) {
    return nullptr;
  }
  return std::make_shared<WebScalerContext>(std::move(typeface), size, std::move(scalerContext));
}

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

Rect WebScalerContext::getImageTransform(GlyphID glyphID, Matrix* matrix) const {
  auto bounds = scalerContext.call<Rect>("getBounds", getText(glyphID), false, false);
  if (bounds.isEmpty()) {
    return {};
  }
  if (matrix) {
    matrix->setTranslate(bounds.left, bounds.top);
  }
  return bounds;
}

bool WebScalerContext::readPixels(GlyphID glyphID, const ImageInfo& dstInfo,
                                  void* dstPixels) const {
  if (dstInfo.isEmpty() || dstPixels == nullptr) {
    return false;
  }
  auto bounds = scalerContext.call<Rect>("getBounds", getText(glyphID), false, false);
  if (bounds.isEmpty()) {
    return false;
  }
  auto image = scalerContext.call<val>("generateImage", getText(glyphID), bounds);
  if (!image.as<bool>()) {
    return false;
  }

  auto width = static_cast<int>(bounds.width());
  auto height = static_cast<int>(bounds.height());
  auto data = val::module_property("tgfx").call<val>(
      "readImagePixels", val::module_property("module"), image, width, height);
  if (!data.as<bool>()) {
    return false;
  }
  auto length = data["length"].as<size_t>();
  if (length == 0) {
    return false;
  }

  auto memory = val::module_property("HEAPU8")["buffer"];
  auto memoryView =
      val::global("Uint8Array").new_(memory, reinterpret_cast<uintptr_t>(dstPixels), length);
  memoryView.call<void>("set", data);
  return true;
}

std::string WebScalerContext::getText(GlyphID glyphID) const {
  return static_cast<WebTypeface*>(typeface.get())->getText(glyphID);
}
}  // namespace tgfx
