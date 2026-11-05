/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "TextShape.h"
#include <vector>
#include "core/GlyphTransform.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

std::shared_ptr<Shape> Shape::MakeFrom(std::shared_ptr<TextBlob> textBlob) {
  if (textBlob == nullptr) {
    return nullptr;
  }
  for (auto run : *textBlob) {
    if (run.font.hasOutlines()) {
      return std::make_shared<TextShape>(std::move(textBlob));
    }
  }
  return nullptr;
}

Rect TextShape::onGetBounds() const {
  return textBlob->getBounds();
}

bool TextShape::getGlyphPaths(float resolutionScale, std::vector<Path>* paths) const {
  if (paths == nullptr || FloatNearlyZero(resolutionScale)) {
    return false;
  }
  auto hasScale = !FloatNearlyEqual(resolutionScale, 1.0f);
  auto matrix = Matrix::MakeScale(resolutionScale, resolutionScale);
  std::vector<Path> glyphPaths = {};
  for (auto run : *textBlob) {
    if (!run.font.hasOutlines()) {
      continue;
    }
    auto font = run.font;
    if (hasScale) {
      font = font.makeWithSize(resolutionScale * font.getSize());
    }
    for (size_t index = 0; index < run.glyphCount; index++) {
      auto glyphID = run.glyphs[index];
      Path glyphPath = {};
      if (!font.getPath(glyphID, &glyphPath)) {
        LOGE("TextShape::getPath() Failed to get path for glyph!");
        return false;
      }
      // A path without contour segments is empty, except when it carries an inverse fill type:
      // that one fills the whole plane. Only the genuinely empty outlines are dropped here.
      if (glyphPath.isEmpty() && !glyphPath.isInverseFillType()) {
        continue;
      }
      auto glyphMatrix = GetGlyphMatrix(run, index);
      glyphMatrix.preScale(1.0f / resolutionScale, 1.0f / resolutionScale);
      glyphMatrix.postConcat(matrix);
      glyphPath.transform(glyphMatrix);
      glyphPaths.push_back(std::move(glyphPath));
    }
  }
  auto inverseMatrix = Matrix::MakeScale(1.f / resolutionScale, 1.f / resolutionScale);
  for (auto& glyphPath : glyphPaths) {
    glyphPath.transform(inverseMatrix);
  }
  *paths = std::move(glyphPaths);
  return true;
}

bool TextShape::isInverseFillType() const {
  std::vector<Path> glyphPaths = {};
  if (!getGlyphPaths(1.0f, &glyphPaths) || glyphPaths.empty()) {
    return false;
  }
  if (glyphPaths.front().isInverseFillType()) {
    return true;
  }
  for (const auto& glyphPath : glyphPaths) {
    if (glyphPath.isInverseFillType()) {
      // Union is inverse here, but the append fallback retains the first non-inverse rule.
      return getPath().isInverseFillType();
    }
  }
  return false;
}

Path TextShape::onGetPath(float resolutionScale) const {
  std::vector<Path> glyphPaths = {};
  if (!getGlyphPaths(resolutionScale, &glyphPaths)) {
    return {};
  }
  auto merged = Path::Merge(glyphPaths);
  if (!merged.has_value()) {
    // An empty result is what Shape::getPath() would cache, while getBounds() still reports the
    // text blob's bounds, so the run would vanish with nothing pointing at why. Appending is what
    // this did before the merge existed: it cannot fail, and a run whose glyphs disagree about the
    // fill rule is then drawn under the first one's rather than not drawn at all.
    LOGE("TextShape::getPath() Failed to merge the glyph outlines; falling back to append.");
    Path result = {};
    for (const auto& glyphPath : glyphPaths) {
      result.addPath(glyphPath);
    }
    return result;
  }
  return std::move(*merged);
}
}  // namespace tgfx
