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

Path TextShape::onGetPath(float resolutionScale) const {
  if (FloatNearlyZero(resolutionScale)) {
    return {};
  }
  auto hasScale = !FloatNearlyEqual(resolutionScale, 1.0f);
  auto matrix = Matrix::MakeScale(resolutionScale, resolutionScale);
  // A single Path carries one fill rule, so the outlines cannot always be appended: outlines that
  // disagree would lose their meaning, and overlapping even-odd outlines would cancel each other
  // out. Collect them first to find out which case this blob is.
  std::vector<Path> glyphPaths = {};
  auto needsUnion = false;
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
        return {};
      }
      if (glyphPath.isEmpty()) {
        continue;
      }
      auto glyphMatrix = GetGlyphMatrix(run, index);
      glyphMatrix.preScale(1.0f / resolutionScale, 1.0f / resolutionScale);
      glyphMatrix.postConcat(matrix);
      glyphPath.transform(glyphMatrix);
      if (glyphPath.getFillType() != PathFillType::Winding) {
        needsUnion = true;
      }
      glyphPaths.push_back(std::move(glyphPath));
    }
  }
  Path totalPath = {};
  for (auto& glyphPath : glyphPaths) {
    if (totalPath.isEmpty()) {
      totalPath = glyphPath;
    } else if (needsUnion) {
      totalPath.addPath(glyphPath, PathOp::Union);
    } else {
      totalPath.addPath(glyphPath);
    }
  }
  auto inverseMatrix = Matrix::MakeScale(1.f / resolutionScale, 1.f / resolutionScale);
  totalPath.transform(inverseMatrix);
  return totalPath;
}
}  // namespace tgfx
