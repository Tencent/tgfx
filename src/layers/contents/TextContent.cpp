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

namespace tgfx {
Rect TextContent::getBounds() const {
  return glyphRun.getBounds(Matrix::I());
}

void TextContent::draw(Canvas* canvas, const Paint& paint) const {
  auto textPaint = paint;
  auto color = textColor;
  color.alpha *= paint.getAlpha();
  textPaint.setColor(color);
  canvas->drawGlyphs(glyphRun.glyphIDs().data(), glyphRun.positions().data(), glyphRun.runSize(),
                     glyphRun.font(), textPaint);
}
}  // namespace tgfx
