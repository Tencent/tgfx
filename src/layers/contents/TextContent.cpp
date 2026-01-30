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

TextContent::TextContent(std::shared_ptr<TextBlob> textBlob, const Matrix& matrix,
                         const LayerPaint& paint)
    : DrawContent(paint), textBlob(std::move(textBlob)), textMatrix(matrix) {
}

Rect TextContent::onGetBounds() const {
  return textMatrix.mapRect(textBlob->getBounds());
}

Rect TextContent::getTightBounds(const Matrix& matrix) const {
  auto combinedMatrix = textMatrix;
  combinedMatrix.postConcat(matrix);
  return textBlob->getTightBounds(&combinedMatrix);
}

bool TextContent::hitTestPoint(float localX, float localY) const {
  if (color.alpha <= 0) {
    return false;
  }
  Matrix inverse = Matrix::I();
  if (!textMatrix.invert(&inverse)) {
    return false;
  }
  auto localPoint = inverse.mapXY(localX, localY);
  return textBlob->hitTestPoint(localPoint.x, localPoint.y, stroke.get());
}

void TextContent::onDraw(Canvas* canvas, const Paint& paint) const {
  auto oldMatrix = canvas->getMatrix();
  canvas->concat(textMatrix);
  canvas->drawTextBlob(textBlob, 0, 0, paint);
  canvas->setMatrix(oldMatrix);
}

bool TextContent::onHasSameGeometry(const GeometryContent* other) const {
  auto* otherText = static_cast<const TextContent*>(other);
  return textBlob == otherText->textBlob && textMatrix == otherText->textMatrix;
}

}  // namespace tgfx
