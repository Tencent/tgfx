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
  DEBUG_ASSERT(_glyphRuns[0].glyphFace != nullptr);
  DEBUG_ASSERT(std::all_of(
      _glyphRuns.begin(), _glyphRuns.end(),
      [hasColor = _glyphRuns[0].glyphFace->hasColor()](const GlyphRun& glyphRun) {
        return !glyphRun.glyphs.empty() && glyphRun.glyphs.size() == glyphRun.positions.size() &&
               glyphRun.glyphFace != nullptr && glyphRun.glyphFace->hasColor() == hasColor;
      }));
}

Rect GlyphRunList::getBounds(float resolutionScale) const {
  if (resolutionScale <= 0.0f) {
    return Rect::MakeEmpty();
  }
  auto hasScale = !FloatNearlyEqual(resolutionScale, 1.0f);
  auto totalBounds = Rect::MakeEmpty();
  for (auto& run : _glyphRuns) {
    auto glyphFace = run.glyphFace;
    if (glyphFace == nullptr) {
      continue;
    }
    if (hasScale) {
      // Scale the glyphs before measuring to prevent precision loss with small font sizes.
      glyphFace = glyphFace->makeScaled(resolutionScale);
    }
    size_t index = 0;
    auto& positions = run.positions;
    for (auto& glyphID : run.glyphs) {
      auto bounds = glyphFace->getBounds(glyphID);
      auto& position = positions[index];
      bounds.offset(position.x * resolutionScale, position.y * resolutionScale);
      totalBounds.join(bounds);
      index++;
    }
  }
  if (hasScale) {
    totalBounds.scale(1.0f / resolutionScale, 1.0f / resolutionScale);
  }
  return totalBounds;
}

bool GlyphRunList::getPath(Path* path, float resolutionScale) const {
  if (resolutionScale <= 0.0f || hasColor()) {
    return false;
  }
  auto hasScale = !FloatNearlyEqual(resolutionScale, 1.0f);
  Path totalPath = {};
  for (auto& run : _glyphRuns) {
    auto glyphFace = run.glyphFace;
    if (glyphFace == nullptr) {
      continue;
    }
    if (hasScale) {
      // Scale the glyphs before measuring to prevent precision loss with small font sizes.
      glyphFace = glyphFace->makeScaled(resolutionScale);
    }
    size_t index = 0;
    auto& positions = run.positions;
    for (auto& glyphID : run.glyphs) {
      Path glyphPath = {};
      if (glyphFace->getPath(glyphID, &glyphPath)) {
        auto& position = positions[index];
        auto glyphMatrix = Matrix::MakeScale(1.0f / resolutionScale, 1.0f / resolutionScale);
        glyphMatrix.postTranslate(position.x, position.y);
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
