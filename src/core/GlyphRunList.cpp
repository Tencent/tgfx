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

#include "GlyphRunList.h"
#include "ScalerContext.h"
#include "core/utils/FauxBoldScale.h"
#include "../../include/tgfx/core/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/TextBlob.h"

namespace tgfx {
const std::vector<std::shared_ptr<GlyphRunList>>* GlyphRunList::Unwrap(const TextBlob* textBlob) {
  if (textBlob == nullptr) {
    return nullptr;
  }
  return &textBlob->glyphRunLists;
}

GlyphRunList::GlyphRunList(GlyphRun glyphRun) {
  DEBUG_ASSERT(!glyphRun.glyphs.empty());
  DEBUG_ASSERT(glyphRun.glyphs.size() == glyphRun.positions.size());
  _glyphRuns.push_back(std::move(glyphRun));
}

GlyphRunList::GlyphRunList(std::vector<GlyphRun> glyphRuns) : _glyphRuns(std::move(glyphRuns)) {
  DEBUG_ASSERT(!_glyphRuns.empty());
  DEBUG_ASSERT(std::all_of(_glyphRuns.begin(), _glyphRuns.end(),
                           [hasColor = _glyphRuns[0].font.hasColor()](const GlyphRun& glyphRun) {
                             return !glyphRun.glyphs.empty() &&
                                    glyphRun.glyphs.size() == glyphRun.positions.size() &&
                                    glyphRun.font.hasColor() == hasColor;
                           }));
}

Rect GlyphRunList::getBounds() const {
  if (auto cachedBounds = bounds.get()) {
    return *cachedBounds;
  }
  Rect totalBounds = computeConservativeBounds();
  bounds.update(totalBounds);
  return totalBounds;
}

Rect GlyphRunList::getTightBounds(const Matrix* matrix) const {
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

Rect GlyphRunList::computeConservativeBounds() const {
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

bool GlyphRunList::getPath(Path* path, const Matrix* matrix) const {
  if (!hasOutlines()) {
    return false;
  }
  auto resolutionScale = matrix ? matrix->getMaxScale() : 1.0f;
  if (FloatNearlyZero(resolutionScale)) {
    return false;
  }
  auto hasScale = !FloatNearlyEqual(resolutionScale, 1.0f);
  Path totalPath = {};
  for (auto& run : _glyphRuns) {
    auto font = run.font;
    if (hasScale) {
      // Scale the glyphs before measuring to prevent precision loss with small font sizes.
      font = font.makeWithSize(resolutionScale * font.getSize());
    }
    size_t index = 0;
    auto& positions = run.positions;
    for (auto& glyphID : run.glyphs) {
      Path glyphPath = {};
      if (font.getPath(glyphID, &glyphPath)) {
        auto& position = positions[index];
        auto glyphMatrix = Matrix::MakeScale(1.0f / resolutionScale, 1.0f / resolutionScale);
        glyphMatrix.postTranslate(position.x, position.y);
        if (matrix) {
          glyphMatrix.postConcat(*matrix);
        }
        glyphPath.transform(glyphMatrix);
        totalPath.addPath(glyphPath);
      } else {
        return false;
      }
      index++;
    }
  }
  *path = std::move(totalPath);
  return true;
}
}  // namespace tgfx
