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
  auto glyphRun = SimpleTextShaper::Shape(text, font);
  if (glyphRun.empty()) {
    return nullptr;
  }
  return std::make_shared<SimpleTextBlob>(std::move(glyphRun));
}

std::shared_ptr<TextBlob> TextBlob::MakeFrom(const GlyphID glyphIDs[], const Point positions[],
                                             size_t glyphCount, const Font& font) {
  if (glyphCount == 0) {
    return nullptr;
  }
  GlyphRun glyphRun(font, {glyphIDs, glyphIDs + glyphCount}, {positions, positions + glyphCount});
  return std::make_shared<SimpleTextBlob>(std::move(glyphRun));
}

Rect SimpleTextBlob::getBounds() const {
  return glyphRun.getBounds();
}
}  // namespace tgfx
