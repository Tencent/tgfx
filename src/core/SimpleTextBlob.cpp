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

#include "core/SimpleTextBlob.h"
#include "tgfx/core/PathEffect.h"
#include "utils/SimpleTextShaper.h"

namespace tgfx {
std::shared_ptr<TextBlob> TextBlob::MakeFrom(const std::string& text, const tgfx::Font& font) {
  auto [glyphIDs, positions] = SimpleTextShaper::Shape(text, font);
  if (glyphIDs.empty()) {
    return nullptr;
  }
  auto textBlob = std::make_shared<SimpleTextBlob>();
  textBlob->glyphIDs = std::move(glyphIDs);
  textBlob->positions = std::move(positions);
  textBlob->font = font;
  return textBlob;
}

std::shared_ptr<TextBlob> TextBlob::MakeFrom(const GlyphID glyphIDs[], const Point positions[],
                                             size_t glyphCount, const Font& font) {
  if (glyphCount == 0) {
    return nullptr;
  }
  auto textBlob = std::make_shared<SimpleTextBlob>();
  textBlob->glyphIDs = {glyphIDs, glyphIDs + glyphCount};
  textBlob->positions = {positions, positions + glyphCount};
  textBlob->font = font;
  return textBlob;
}

bool SimpleTextBlob::hasColor() const {
  return font.getTypeface()->hasColor();
}

Rect SimpleTextBlob::getBounds(const Stroke* stroke) const {
  auto totalBounds = Rect::MakeEmpty();
  int index = 0;
  for (auto& glyphID : glyphIDs) {
    auto bounds = font.getBounds(glyphID);
    bounds.offset(positions[index]);
    if (stroke) {
      bounds.outset(stroke->width, stroke->width);
    }
    totalBounds.join(bounds);
    index++;
  }
  return totalBounds;
}

bool SimpleTextBlob::getPath(Path* path, const Stroke* stroke) const {
  if (hasColor()) {
    return false;
  }
  auto pathEffect = PathEffect::MakeStroke(stroke);
  Path totalPath = {};
  auto glyphCount = glyphIDs.size();
  for (size_t i = 0; i < glyphCount; ++i) {
    const auto& glyphID = glyphIDs[i];
    Path glyphPath = {};
    if (font.getPath(glyphID, &glyphPath)) {
      const auto& position = positions[i];
      if (pathEffect) {
        pathEffect->applyTo(&glyphPath);
      }
      glyphPath.transform(Matrix::MakeTrans(position.x, position.y));
      totalPath.addPath(glyphPath);
    } else {
      return false;
    }
  }
  *path = totalPath;
  return true;
}
}  // namespace tgfx
