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
#include "profileClient/Profile.h"

namespace tgfx {
Rect TextContent::getBounds() const {
  TGFX_PROFILE_ZONE_SCOPPE_NAME("TextContent::getBounds");
  return textBlob->getBounds();
}

void TextContent::draw(Canvas* canvas, const Paint& paint) const {
  TGFX_PROFILE_ZONE_SCOPPE_NAME("TextContent::draw");
  auto textPaint = paint;
  auto color = textColor;
  color.alpha *= paint.getAlpha();
  textPaint.setColor(color);
  canvas->drawTextBlob(textBlob, 0, 0, textPaint);
}

bool TextContent::hitTestPoint(float localX, float localY, bool pixelHitTest) {
  TGFX_PROFILE_ZONE_SCOPPE_NAME("TextContent::hitTestPoint");
  if (pixelHitTest) {
    const auto glyphRunLists = GlyphRunList::Unwrap(textBlob.get());

    if (nullptr == glyphRunLists) {
      return false;
    }

    for (const auto& glyphRunList : *glyphRunLists) {
      if (hitTestPointInternal(localX, localY, glyphRunList)) {
        return true;
      }
    }

    return false;
  }

  const Rect textBounds = textBlob->getBounds();
  return textBounds.contains(localX, localY);
}

bool TextContent::hitTestPointInternal(float localX, float localY,
                                       const std::shared_ptr<GlyphRunList>& glyphRunList) {
  const auto& glyphRuns = glyphRunList->glyphRuns();
  for (const auto& glyphRun : glyphRuns) {
    const auto& font = glyphRun.font;
    const auto& positions = glyphRun.positions;
    size_t index = 0;

    for (const auto& glyphID : glyphRun.glyphs) {
      const auto& position = positions[index];
      if (font.hasColor()) {
        auto bounds = font.getBounds(glyphID);
        bounds.offset(position.x, position.y);
        if (bounds.contains(localX, localY)) {
          return true;
        }
      } else {
        Path glyphPath = {};
        if (font.getPath(glyphID, &glyphPath)) {
          if (glyphPath.contains(localX - position.x, localY - position.y)) {
            return true;
          }
        }
      }
      index++;
    }
  }
  return false;
}
}  // namespace tgfx
