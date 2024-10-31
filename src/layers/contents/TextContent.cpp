/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/GlyphRunList.h"

namespace tgfx {
Rect TextContent::getBounds() const {
  return textBlob->getBounds();
}

void TextContent::draw(Canvas* canvas, const Paint& paint) const {
  auto textPaint = paint;
  auto color = textColor;
  color.alpha *= paint.getAlpha();
  textPaint.setColor(color);
  canvas->drawTextBlob(textBlob, 0, 0, textPaint);
}

bool TextContent::hitTestPoint(float localX, float localY, bool pixelHitTest) {
  if (pixelHitTest) {
    return getTextContentPath().contains(localX, localY);
  }

  const Rect textBounds = textBlob->getBounds(Matrix::I());
  return textBounds.contains(localX, localY);
}

Path TextContent::getTextContentPath() const {
  Path totalPath = {};

  for (const auto& runList : textBlob->glyphRunLists) {
    Path glyphRunListPath = {};
    if (runList->hasColor()) {
      const Rect emojiBounds = runList->getBounds(Matrix::I());
      glyphRunListPath.addRect(emojiBounds);
      totalPath.addPath(glyphRunListPath);
    } else {
      if (runList->getPath(&glyphRunListPath, Matrix::I())) {
        totalPath.addPath(glyphRunListPath);
      }
    }
  }

  return totalPath;
}
}  // namespace tgfx
