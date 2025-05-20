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

Rect WebScalerContext::getImageTransform(const GlyphStyle& glyphStyle, Matrix* matrix) const {
  auto fauxBold = !hasColor() && glyphStyle.fauxBold;
  auto fauxItalic = !hasColor() && glyphStyle.fauxItalic;
  auto bounds =
      scalerContext.call<Rect>("getBounds", getText(glyphStyle.glyphID), fauxBold, fauxItalic);
  if (bounds.isEmpty()) {
    return {};
  }
  if (!hasColor() && glyphStyle.stroke != nullptr) {
    glyphStyle.stroke->applyToBounds(&bounds);
  }

  if (matrix) {
    matrix->setTranslate(bounds.left, bounds.top);
    if (fauxItalic && !hasColor()) {
      //Since the web platform natively supports faux italic styling,
      //we apply the inverse skew transformation matrix to neutralize
      //the font's built-in italic matrix during rendering.
      matrix->postSkew(-ITALIC_SKEW, 0);
    }
  }
  return bounds;
}

bool WebScalerContext::readPixels(const GlyphStyle& glyphStyle, const ImageInfo& dstInfo,
                                  void* dstPixels) const {
  if (dstInfo.isEmpty() || dstPixels == nullptr) {
    return false;
  }

  auto fauxBold = !hasColor() && glyphStyle.fauxBold;
  auto fauxItalic = !hasColor() && glyphStyle.fauxItalic;

  auto bounds =
      scalerContext.call<Rect>("getBounds", getText(glyphStyle.glyphID), fauxBold, fauxItalic);
  if (bounds.isEmpty()) {
    return false;
  }

  if (!hasColor() && glyphStyle.stroke) {
    glyphStyle.stroke->applyToBounds(&bounds);
    return drawGlyph(glyphStyle, bounds, dstInfo, dstPixels);
  }

  auto imageData = scalerContext.call<val>("readPixels", getText(glyphStyle.glyphID), bounds,
                                           fauxBold, fauxItalic);
  if (!imageData.as<bool>()) {
    return false;
  }
  if (dstInfo.isAlphaOnly()) {
    if (!readAlphaOnlyPixels(imageData, dstInfo, dstPixels)) {
      return false;
    }
  } else {
    auto length = imageData["length"].as<size_t>();
    if (length == 0) {
      return false;
    }
    auto memory = val::module_property("HEAPU8")["buffer"];
    auto memoryView =
        val::global("Uint8Array").new_(memory, reinterpret_cast<uintptr_t>(dstPixels), length);
    memoryView.call<void>("set", imageData);
  };
  return true;
}

std::string WebScalerContext::getText(GlyphID glyphID) const {
  return static_cast<WebTypeface*>(typeface.get())->getText(glyphID);
}

bool WebScalerContext::drawGlyph(const GlyphStyle& glyphStyle, const Rect& glyphBounds,
                                 const ImageInfo& dstInfo, void* dstPixels) const {
  auto typeface = std::static_pointer_cast<WebTypeface>(getTypeface());
  if (typeface == nullptr) {
    return false;
  }
  auto scalerContextClass = val::module_property("ScalerContext");
  if (!scalerContextClass.as<bool>()) {
    return false;
  }
  auto scalerContext =
      scalerContextClass.new_(typeface->fontFamily(), typeface->fontStyle(), getSize());
  if (!scalerContext.as<bool>()) {
    return false;
  }
  auto canvas =
      val::module_property("tgfx").call<val>("createCanvas2D", dstInfo.width(), dstInfo.height());
  if (!canvas.as<bool>()) {
    return false;
  }
  auto webMaskClass = val::module_property("WebMask");
  if (!webMaskClass.as<bool>()) {
    return false;
  }
  auto webMask = webMaskClass.call<val>("create", canvas);
  if (!webMask.as<bool>()) {
    return false;
  }
  auto texts = std::vector<std::string>{typeface->getText(glyphStyle.glyphID)};
  auto points = std::vector<Point>{Point::Zero()};
  auto webFont = val::object();
  webFont.set("name", typeface->fontFamily());
  webFont.set("style", typeface->fontStyle());
  webFont.set("size", getSize());
  webFont.set("bold", glyphStyle.fauxBold);
  webFont.set("italic", glyphStyle.fauxItalic);
  auto fontMatrix = Matrix::MakeTrans(-glyphBounds.x(), -glyphBounds.y());
  if (glyphStyle.stroke) {
    webMask.call<void>("strokeText", webFont, *glyphStyle.stroke, texts, points, fontMatrix);
  } else {
    webMask.call<void>("fillText", webFont, texts, points, fontMatrix);
  }
  auto imageData = scalerContext.call<val>("getImageData", canvas);
  if (!imageData.as<bool>()) {
    return false;
  }
  auto memory = val::module_property("HEAPU8")["buffer"];
  if (dstInfo.isAlphaOnly()) {
    if (!readAlphaOnlyPixels(imageData, dstInfo, dstPixels)) {
      return false;
    }
  } else {
    auto length = imageData["length"].as<size_t>();
    auto memoryView =
        val::global("Uint8Array").new_(memory, reinterpret_cast<uintptr_t>(dstPixels), length);
    memoryView.call<void>("set", imageData);
  }
  return true;
}

bool WebScalerContext::readAlphaOnlyPixels(emscripten::val imageData, const ImageInfo& dstInfo,
                                           void* dstPixels) const {
  auto length = imageData["length"].as<size_t>();
  if (length == 0 || length % 4 != 0) {
    return false;
  }
  auto buffer = new (std::nothrow) int8_t[length];
  if (buffer == nullptr) {
    return false;
  }
  auto memory = val::module_property("HEAPU8")["buffer"];
  auto memoryView =
      val::global("Uint8Array").new_(memory, reinterpret_cast<uintptr_t>(buffer), length);
  memoryView.call<void>("set", imageData);
  auto RGBAInfo = ImageInfo::Make(dstInfo.width(), dstInfo.height(), ColorType::RGBA_8888,
                                  AlphaType::Premultiplied);
  Pixmap RGBAMap(RGBAInfo, buffer);
  RGBAMap.readPixels(dstInfo, dstPixels);
  delete[] buffer;
  return true;
}

}  // namespace tgfx
