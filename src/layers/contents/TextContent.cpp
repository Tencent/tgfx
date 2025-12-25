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

TextContent::TextContent(std::shared_ptr<TextBlob> textBlob, float x, float y,
                         const LayerPaint& paint)
    : GeometryContent(paint), textBlob(std::move(textBlob)), x(x), y(y) {
}

Rect TextContent::onGetBounds() const {
  auto bounds = textBlob->getBounds();
  bounds.offset(x, y);
  return bounds;
}

Rect TextContent::getTightBounds(const Matrix& matrix) const {
  auto offsetMatrix = Matrix::MakeTrans(x, y);
  offsetMatrix.postConcat(matrix);
  return textBlob->getTightBounds(&offsetMatrix);
}

bool TextContent::hitTestPoint(float localX, float localY) const {
  if (color.alpha <= 0) {
    return false;
  }
  return textBlob->hitTestPoint(localX - x, localY - y, stroke.get());
}

void TextContent::onDraw(Canvas* canvas, const Paint& paint) const {
  canvas->drawTextBlob(textBlob, x, y, paint);
}

bool TextContent::onHasSameGeometry(const GeometryContent* other) const {
  auto* otherText = static_cast<const TextContent*>(other);
  return textBlob == otherText->textBlob && x == otherText->x && y == otherText->y;
}

}  // namespace tgfx
