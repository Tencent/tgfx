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

#include "WebGlyphDrawer.h"
#include <emscripten/val.h>
#include "WebMask.h"
#include "WebTypeface.h"
#include "tgfx/core/Font.h"

using namespace emscripten;

namespace tgfx {
std::shared_ptr<GlyphDrawer> GlyphDrawer::Make(float resolutionScale, bool antiAlias,
                                               bool needsGammaCorrection) {
  return std::make_shared<WebGlyphDrawer>(resolutionScale, antiAlias, needsGammaCorrection);
}

bool WebGlyphDrawer::onFillGlyph(const GlyphFace* glyphFace, GlyphID glyphID, const Stroke* stroke,
                                 const Rect& glyphBounds, const ImageInfo& dstInfo,
                                 void* dstPixels) {
  if (glyphFace == nullptr || glyphFace == 0) {
    return false;
  }
  Font font = {};
  if (!glyphFace->asFont(&font)) {
    return false;
  }
  auto typeface = std::static_pointer_cast<WebTypeface>(font.getTypeface());
  auto scalerContextClass = val::module_property("ScalerContext");
  if (!scalerContextClass.as<bool>()) {
    return false;
  }
  auto scalerContext =
      scalerContextClass.new_(typeface->fontFamily(), typeface->fontStyle(), font.getSize());
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

  auto texts = std::vector<std::string>{typeface->getText(glyphID)};
  auto points = std::vector<Point>{Point::Zero()};

  auto webFont = val::object();
  webFont.set("name", typeface->fontFamily());
  webFont.set("style", typeface->fontStyle());
  webFont.set("size", font.getSize() / resolutionScale);
  webFont.set("bold", font.isFauxBold());
  webFont.set("italic", font.isFauxItalic());
  auto fontMatrix = Matrix::MakeScale(resolutionScale, resolutionScale);
  fontMatrix.postTranslate(-glyphBounds.x(), -glyphBounds.y());
  if (stroke) {
    webMask.call<void>("strokeText", webFont, *stroke, texts, points, fontMatrix);
  } else {
    webMask.call<void>("fillText", webFont, texts, points, fontMatrix);
  }
  auto imageData = scalerContext.call<val>("getImageData", canvas);
  if (!imageData.as<bool>()) {
    return false;
  }
  auto length = static_cast<size_t>(dstInfo.width() * dstInfo.height() * 4);
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
