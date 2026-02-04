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
#include "tgfx/core/Shape.h"

namespace tgfx {

TextContent::TextContent(std::shared_ptr<TextBlob> textBlob, const Point& offset,
                         const LayerPaint& paint)
    : DrawContent(paint), textBlob(std::move(textBlob)), offset(offset) {
}

Rect TextContent::onGetBounds() const {
  return textBlob->getBounds().makeOffset(offset.x, offset.y);
}

Rect TextContent::getTightBounds(const Matrix& matrix) const {
  auto combinedMatrix = Matrix::MakeTrans(offset.x, offset.y);
  combinedMatrix.postConcat(matrix);
  return textBlob->getTightBounds(&combinedMatrix);
}

bool TextContent::hitTestPoint(float localX, float localY) const {
  if (color.alpha <= 0) {
    return false;
  }
  auto localPoint = Point::Make(localX - offset.x, localY - offset.y);
  return textBlob->hitTestPoint(localPoint.x, localPoint.y, stroke.get());
}

void TextContent::onDraw(Canvas* canvas, const Paint& paint) const {
  canvas->drawTextBlob(textBlob, offset.x, offset.y, paint);
}

bool TextContent::onHasSameGeometry(const GeometryContent* other) const {
  auto* otherText = static_cast<const TextContent*>(other);
  return textBlob == otherText->textBlob && offset == otherText->offset;
}

}  // namespace tgfx
