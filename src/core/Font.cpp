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

#include "tgfx/core/Font.h"
#include "ScalerContext.h"
#include "core/PixelBuffer.h"

namespace tgfx {

class GlyphRasterizer : public ImageCodec {
 public:
  GlyphRasterizer(int width, int height, std::shared_ptr<ScalerContext> scalerContext,
                  GlyphID glyphID, bool fauxBold, const Stroke* s)
      : ImageCodec(width, height, Orientation::LeftTop), scalerContext(std::move(scalerContext)),
        glyphID(glyphID), fauxBold(fauxBold) {
    if (s != nullptr) {
      stroke = new Stroke(*s);
    }
  }

  ~GlyphRasterizer() override {
    delete stroke;
  }

  bool isAlphaOnly() const override {
    return !scalerContext->hasColor();
  }

  bool readPixels(const ImageInfo& dstInfo, void* dstPixels) const override {
    return scalerContext->readPixels(glyphID, fauxBold, stroke, dstInfo, dstPixels);
  }

 private:
  std::shared_ptr<ScalerContext> scalerContext = nullptr;
  GlyphID glyphID = 0;
  bool fauxBold = false;
  Stroke* stroke = nullptr;
};

Font::Font() : scalerContext(ScalerContext::MakeEmpty(0.0f)) {
}

Font::Font(std::shared_ptr<Typeface> tf, float textSize)
    : scalerContext(ScalerContext::Make(std::move(tf), textSize)) {
}

Font Font::makeWithSize(float newSize) const {
  auto newFont = *this;
  newFont.setSize(newSize);
  return newFont;
}

std::shared_ptr<Typeface> Font::getTypeface() const {
  return scalerContext->getTypeface();
}

bool Font::hasColor() const {
  return scalerContext->hasColor();
}

bool Font::hasOutlines() const {
  return scalerContext->hasOutlines();
}

void Font::setTypeface(std::shared_ptr<Typeface> newTypeface) {
  if (newTypeface == scalerContext->getTypeface()) {
    return;
  }
  scalerContext = ScalerContext::Make(std::move(newTypeface), scalerContext->getSize());
}

float Font::getSize() const {
  return scalerContext->getSize();
}

void Font::setSize(float newSize) {
  if (newSize < 0) {
    newSize = 0;
  }
  if (newSize == scalerContext->getSize()) {
    return;
  }
  scalerContext = ScalerContext::Make(scalerContext->getTypeface(), newSize);
}

GlyphID Font::getGlyphID(const std::string& name) const {
  auto typeface = scalerContext->getTypeface();
  return typeface ? typeface->getGlyphID(name) : 0;
}

GlyphID Font::getGlyphID(Unichar unichar) const {
  auto typeface = scalerContext->getTypeface();
  return typeface ? typeface->getGlyphID(unichar) : 0;
}

FontMetrics Font::getMetrics() const {
  return scalerContext->getFontMetrics();
}

Rect Font::getBounds(GlyphID glyphID) const {
  if (glyphID == 0) {
    return {};
  }
  return scalerContext->getBounds(glyphID, fauxBold, fauxItalic);
}

float Font::getAdvance(GlyphID glyphID, bool verticalText) const {
  if (glyphID == 0) {
    return 0;
  }
  return scalerContext->getAdvance(glyphID, verticalText);
}

Point Font::getVerticalOffset(GlyphID glyphID) const {
  if (glyphID == 0) {
    return {};
  }
  return scalerContext->getVerticalOffset(glyphID);
}

bool Font::getPath(GlyphID glyphID, Path* path) const {
  if (glyphID == 0) {
    return false;
  }
  return scalerContext->generatePath(glyphID, fauxBold, fauxItalic, path);
}

std::shared_ptr<ImageCodec> Font::getImage(GlyphID glyphID, const Stroke* stroke,
                                           Matrix* matrix) const {
  if (glyphID == 0) {
    return nullptr;
  }
  auto bounds = scalerContext->getImageTransform(glyphID, fauxBold, stroke, matrix);
  if (bounds.isEmpty()) {
    return nullptr;
  }
  if (matrix && fauxItalic) {
    matrix->postSkew(ITALIC_SKEW, 0);
  }
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  return std::make_shared<GlyphRasterizer>(width, height, scalerContext, glyphID, fauxBold, stroke);
}

bool Font::operator==(const Font& font) const {
  return scalerContext->typeface == font.scalerContext->typeface &&
         scalerContext->textSize == font.scalerContext->textSize && fauxBold == font.fauxBold &&
         fauxItalic == font.fauxItalic;
}
}  // namespace tgfx