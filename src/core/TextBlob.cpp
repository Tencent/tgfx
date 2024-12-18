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

#include "tgfx/core/TextBlob.h"
#include "core/FontGlyphFace.h"
#include "core/GlyphRunList.h"
#include "tgfx/core/UTF.h"

namespace tgfx {
std::shared_ptr<TextBlob> TextBlob::MakeFrom(const std::string& text, const Font& font) {
  if (font.getTypeface() == nullptr) {
    return nullptr;
  }

  const char* textStart = text.data();
  const char* textStop = textStart + text.size();
  GlyphRun glyphRun = GlyphRun(font, {}, {});
  // Use half the font size as width for spaces
  auto emptyAdvance = font.getSize() / 2.0f;
  float xOffset = 0;
  while (textStart < textStop) {
    auto unichar = UTF::NextUTF8(&textStart, textStop);
    auto glyphID = font.getGlyphID(unichar);
    if (glyphID > 0) {
      glyphRun.glyphs.push_back(glyphID);
      glyphRun.positions.push_back(Point::Make(xOffset, 0.0f));
      auto advance = font.getAdvance(glyphID);
      xOffset += advance;
    } else {
      xOffset += emptyAdvance;
    }
  }
  if (glyphRun.glyphs.empty()) {
    return nullptr;
  }
  auto glyphRunList = std::make_shared<GlyphRunList>(std::move(glyphRun));
  return std::shared_ptr<TextBlob>(new TextBlob({glyphRunList}));
}

std::shared_ptr<TextBlob> TextBlob::MakeFrom(const GlyphID glyphIDs[], const Point positions[],
                                             size_t glyphCount, const Font& font) {
  if (glyphCount == 0 || font.getTypeface() == nullptr) {
    return nullptr;
  }
  GlyphRun glyphRun(font, {glyphIDs, glyphIDs + glyphCount}, {positions, positions + glyphCount});
  auto glyphRunList = std::make_shared<GlyphRunList>(std::move(glyphRun));
  return std::shared_ptr<TextBlob>(new TextBlob({glyphRunList}));
}

std::shared_ptr<TextBlob> TextBlob::MakeFrom(GlyphRun glyphRun) {
  if (glyphRun.glyphs.size() != glyphRun.positions.size()) {
    return nullptr;
  }
  if (glyphRun.glyphs.empty()) {
    return nullptr;
  }
  if (glyphRun.glyphFace == nullptr) {
    return nullptr;
  }
  auto glyphRunList = std::make_shared<GlyphRunList>(std::move(glyphRun));
  return std::shared_ptr<TextBlob>(new TextBlob({glyphRunList}));
}

enum class GlyphFaceType { Path, Color, Other };

static GlyphFaceType GetGlyphFaceType(const std::shared_ptr<GlyphFace>& glyphFace) {
  if (glyphFace == nullptr) {
    return GlyphFaceType::Other;
  }

  if (glyphFace->hasColor()) {
    return GlyphFaceType::Color;
  }
  if (glyphFace->hasOutlines()) {
    return GlyphFaceType::Path;
  }

  return GlyphFaceType::Other;
}

std::shared_ptr<TextBlob> TextBlob::MakeFrom(std::vector<GlyphRun> glyphRuns) {
  if (glyphRuns.empty()) {
    return nullptr;
  }
  if (glyphRuns.size() == 1) {
    return MakeFrom(std::move(glyphRuns[0]));
  }
  std::vector<std::shared_ptr<GlyphRunList>> runLists;
  std::vector<GlyphRun> currentRuns;
  GlyphFaceType glyphFaceType = GetGlyphFaceType(glyphRuns[0].glyphFace);
  for (auto& run : glyphRuns) {
    if (run.glyphs.size() != run.positions.size()) {
      return nullptr;
    }
    if (run.glyphFace == nullptr) {
      return nullptr;
    }
    if (run.glyphs.empty()) {
      continue;
    }
    auto currentFontType = GetGlyphFaceType(run.glyphFace);
    if (currentFontType != glyphFaceType) {
      if (!currentRuns.empty()) {
        runLists.push_back(std::make_shared<GlyphRunList>(std::move(currentRuns)));
        currentRuns = {};
      }
      glyphFaceType = currentFontType;
    }
    currentRuns.push_back(std::move(run));
  }
  if (!currentRuns.empty()) {
    runLists.push_back(std::make_shared<GlyphRunList>(std::move(currentRuns)));
  }
  return std::shared_ptr<TextBlob>(new TextBlob(std::move(runLists)));
}

Rect TextBlob::getBounds(float resolutionScale) const {
  auto bounds = Rect::MakeEmpty();
  for (auto& runList : glyphRunLists) {
    bounds.join(runList->getBounds(resolutionScale));
  }
  return bounds;
}
}  // namespace tgfx