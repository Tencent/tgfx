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

#include "TextContent.h"

namespace tgfx {

TextContent::TextContent(std::shared_ptr<TextBlob> textBlob, const LayerPaint& paint)
    : GeometryContent(paint), textBlob(std::move(textBlob)) {
}

Rect TextContent::onGetBounds() const {
  return textBlob->getBounds();
}

Rect TextContent::getTightBounds(const Matrix& matrix) const {
  return textBlob->getTightBounds(&matrix);
}

bool TextContent::hitTestPoint(float localX, float localY) const {
  if (color.alpha <= 0) {
    return false;
  }
  return textBlob->hitTestPoint(localX, localY, stroke.get());
}

void TextContent::onDraw(Canvas* canvas, const Paint& paint) const {
  canvas->drawTextBlob(textBlob, 0, 0, paint);
}

bool TextContent::onHasSameGeometry(const GeometryContent* other) const {
  return textBlob == static_cast<const TextContent*>(other)->textBlob;
}

std::optional<Path> TextContent::onAsClipPath() const {
  // Returns nullopt because GlyphRunList::getPath() may have high computational cost.
  return std::nullopt;
}

}  // namespace tgfx
