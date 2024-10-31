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

#include "GlyphRunList.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/PathEffect.h"
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
  DEBUG_ASSERT(std::all_of(glyphRuns.begin(), glyphRuns.end(),
                           [hasColor = glyphRuns[0].font.hasColor()](const GlyphRun& glyphRun) {
                             return !glyphRun.glyphs.empty() &&
                                    glyphRun.glyphs.size() == glyphRun.positions.size() &&
                                    glyphRun.font.hasColor() == hasColor;
                           }));
}

Rect GlyphRunList::getBounds(const Matrix& matrix, const Stroke* stroke) const {
  auto maxScale = matrix.getMaxScale();
  if (maxScale <= 0.0f) {
    return Rect::MakeEmpty();
  }
  auto totalBounds = Rect::MakeEmpty();
  for (auto& run : _glyphRuns) {
    auto font = run.font;
    if (!FloatNearlyEqual(maxScale, 1.0f)) {
      // Scale the glyphs before measuring to prevent precision loss with small font sizes.
      font = font.makeWithSize(font.getSize() * maxScale);
    }
    size_t index = 0;
    auto& positions = run.positions;
    for (auto& glyphID : run.glyphs) {
      auto bounds = font.getBounds(glyphID);
      auto& position = positions[index];
      bounds.offset(position.x * maxScale, position.y * maxScale);
      totalBounds.join(bounds);
      index++;
    }
  }
  if (!totalBounds.isEmpty() && stroke && !hasColor()) {
    auto strokeWidth = stroke->width * maxScale;
    totalBounds.outset(strokeWidth, strokeWidth);
  }
  auto totalMatrix = matrix;
  totalMatrix.preScale(1.0f / maxScale, 1.0f / maxScale);
  return totalMatrix.mapRect(totalBounds);
}

bool GlyphRunList::getPath(Path* path, const Matrix& matrix, const Stroke* stroke) const {
  auto maxScale = matrix.getMaxScale();
  if (maxScale <= 0.0f || hasColor()) {
    return false;
  }
  Path totalPath = {};
  for (auto& run : _glyphRuns) {
    auto font = run.font;
    if (!FloatNearlyEqual(maxScale, 1.0f)) {
      // Scale the glyphs before measuring to prevent precision loss with small font sizes.
      font = font.makeWithSize(font.getSize() * maxScale);
    }
    size_t index = 0;
    auto& positions = run.positions;
    for (auto& glyphID : run.glyphs) {
      Path glyphPath = {};
      if (font.getPath(glyphID, &glyphPath)) {
        auto& position = positions[index];
        auto offsetMatrix = Matrix::MakeTrans(position.x * maxScale, position.y * maxScale);
        glyphPath.transform(offsetMatrix);
        totalPath.addPath(glyphPath);
      } else {
        return false;
      }
      index++;
    }
  }
  if (stroke) {
    auto scaledStroke = *stroke;
    scaledStroke.width *= maxScale;
    auto pathEffect = PathEffect::MakeStroke(&scaledStroke);
    if (pathEffect) {
      pathEffect->filterPath(&totalPath);
    }
  }
  auto totalMatrix = matrix;
  totalMatrix.preScale(1.0f / maxScale, 1.0f / maxScale);
  totalPath.transform(totalMatrix);
  *path = totalPath;
  return true;
}
}  // namespace tgfx
