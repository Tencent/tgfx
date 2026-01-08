/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include "core/ScalerContext.h"
#include "core/utils/AtomicCache.h"
#include "core/utils/FauxBoldScale.h"
#include "core/utils/MathExtra.h"
#include "core/utils/StrokeUtils.h"
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
  return std::shared_ptr<TextBlob>(new TextBlob({std::move(glyphRun)}));
}

std::shared_ptr<TextBlob> TextBlob::MakeFrom(const GlyphID glyphIDs[], const Point positions[],
                                             size_t glyphCount, const Font& font) {
  if (glyphCount == 0 || font.getTypeface() == nullptr) {
    return nullptr;
  }
  GlyphRun glyphRun(font, {glyphIDs, glyphIDs + glyphCount}, {positions, positions + glyphCount});
  return std::shared_ptr<TextBlob>(new TextBlob({std::move(glyphRun)}));
}

std::shared_ptr<TextBlob> TextBlob::MakeFrom(GlyphRun glyphRun) {
  if (glyphRun.glyphs.size() != glyphRun.positions.size()) {
    return nullptr;
  }
  if (glyphRun.glyphs.empty()) {
    return nullptr;
  }
  return std::shared_ptr<TextBlob>(new TextBlob({std::move(glyphRun)}));
}

std::shared_ptr<TextBlob> TextBlob::MakeFrom(std::vector<GlyphRun> glyphRuns) {
  if (glyphRuns.empty()) {
    return nullptr;
  }
  std::vector<GlyphRun*> validRuns = {};
  for (auto& run : glyphRuns) {
    if (run.glyphs.size() != run.positions.size()) {
      return nullptr;
    }
    if (!run.glyphs.empty()) {
      validRuns.push_back(&run);
    }
  }
  if (validRuns.empty()) {
    return nullptr;
  }
  if (validRuns.size() == glyphRuns.size()) {
    return std::shared_ptr<TextBlob>(new TextBlob(std::move(glyphRuns)));
  }
  std::vector<GlyphRun> filteredRuns = {};
  for (auto* run : validRuns) {
    filteredRuns.push_back(std::move(*run));
  }
  return std::shared_ptr<TextBlob>(new TextBlob(std::move(filteredRuns)));
}

TextBlob::~TextBlob() {
  AtomicCacheReset(bounds);
}

Rect TextBlob::getBounds() const {
  if (auto cachedBounds = AtomicCacheGet(bounds)) {
    return *cachedBounds;
  }
  auto totalBounds = computeBounds();
  AtomicCacheSet(bounds, &totalBounds);
  return totalBounds;
}

Rect TextBlob::computeBounds() const {
  Rect finalBounds = {};
  Matrix transformMat = {};
  for (auto& run : _glyphRuns) {
    auto& font = run.font;
    transformMat.reset();
    transformMat.setScale(font.getSize(), font.getSize());
    if (font.isFauxItalic()) {
      transformMat.postSkew(ITALIC_SKEW, 0.0f);
    }
    auto typeface = font.getTypeface();
    if (typeface == nullptr || typeface->getBounds().isEmpty()) {
      finalBounds.setEmpty();
      break;
    }
    auto fontBounds = typeface->getBounds();
    if (font.isFauxBold()) {
      auto fauxBoldScale = FauxBoldScale(font.getSize());
      fontBounds.outset(fauxBoldScale, fauxBoldScale);
    }
    transformMat.mapRect(&fontBounds);
    Rect runBounds = {};
    runBounds.setBounds(run.positions.data(), static_cast<int>(run.positions.size()));
    runBounds.left += fontBounds.left;
    runBounds.top += fontBounds.top;
    runBounds.right += fontBounds.right;
    runBounds.bottom += fontBounds.bottom;
    finalBounds.join(runBounds);
  }
  if (!finalBounds.isEmpty()) {
    return finalBounds;
  }
  return getTightBounds();
}

Rect TextBlob::getTightBounds(const Matrix* matrix) const {
  auto resolutionScale = matrix ? matrix->getMaxScale() : 1.0f;
  if (FloatNearlyZero(resolutionScale)) {
    return {};
  }
  auto hasScale = !FloatNearlyEqual(resolutionScale, 1.0f);
  Rect totalBounds = {};
  for (auto& run : _glyphRuns) {
    auto font = run.font;
    if (hasScale) {
      // Scale the glyphs before measuring to prevent precision loss with small font sizes.
      font = font.makeWithSize(resolutionScale * font.getSize());
    }
    size_t index = 0;
    auto& positions = run.positions;
    for (auto& glyphID : run.glyphs) {
      auto bounds = font.getBounds(glyphID);
      auto& position = positions[index];
      bounds.offset(position.x * resolutionScale, position.y * resolutionScale);
      totalBounds.join(bounds);
      index++;
    }
  }
  if (hasScale) {
    totalBounds.scale(1.0f / resolutionScale, 1.0f / resolutionScale);
  }
  if (matrix) {
    totalBounds = matrix->mapRect(totalBounds);
  }
  return totalBounds;
}

bool TextBlob::hitTestPoint(float localX, float localY, const Stroke* stroke) const {
  for (auto& run : _glyphRuns) {
    auto& font = run.font;
    auto& positions = run.positions;
    auto usePathHitTest = font.hasOutlines();
    size_t index = 0;
    for (auto& glyphID : run.glyphs) {
      auto& position = positions[index];
      auto glyphLocalX = localX - position.x;
      auto glyphLocalY = localY - position.y;
      if (usePathHitTest) {
        Path glyphPath = {};
        if (font.getPath(glyphID, &glyphPath)) {
          if (stroke) {
            stroke->applyToPath(&glyphPath);
          }
          if (glyphPath.contains(glyphLocalX, glyphLocalY)) {
            return true;
          }
        }
      } else {
        auto glyphBounds = font.getBounds(glyphID);
        if (stroke) {
          ApplyStrokeToBounds(*stroke, &glyphBounds);
        }
        if (glyphBounds.contains(glyphLocalX, glyphLocalY)) {
          return true;
        }
      }
      index++;
    }
  }
  return false;
}
}  // namespace tgfx
