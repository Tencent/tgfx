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

namespace tgfx {
static auto EmptyContext = ScalerContext::MakeEmpty(0);

Font::Font() : scalerContext(EmptyContext) {
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
    return Rect::MakeEmpty();
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
    return Point::Zero();
  }
  return scalerContext->getVerticalOffset(glyphID);
}

bool Font::getPath(GlyphID glyphID, Path* path) const {
  if (glyphID == 0) {
    return false;
  }
  return scalerContext->generatePath(glyphID, fauxBold, fauxItalic, path);
}

std::shared_ptr<ImageBuffer> Font::getImage(GlyphID glyphID, Matrix* matrix) const {
  if (glyphID == 0) {
    return nullptr;
  }
  return scalerContext->generateImage(glyphID, fauxItalic, matrix);
}
}  // namespace tgfx