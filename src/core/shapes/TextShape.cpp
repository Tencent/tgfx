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

TextShape::TextShape(std::shared_ptr<TextBlob> textBlob) : textBlob(std::move(textBlob)) {
  // onGetPath() appends the glyph outlines into one path, so a single parity typeface decides
  // the rule for the whole blob. Mixing both kinds in one blob cannot be expressed with a single
  // fill type; parity wins because filled-in counters are the more visible error.
  for (auto run : *this->textBlob) {
    auto typeface = run.font.getTypeface();
    if (typeface != nullptr && typeface->usesEvenOddFill()) {
      evenOddFill = true;
      break;
    }
  }
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
  Path totalPath = {};
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
      if (font.getPath(glyphID, &glyphPath)) {
        auto glyphMatrix = GetGlyphMatrix(run, index);
        glyphMatrix.preScale(1.0f / resolutionScale, 1.0f / resolutionScale);
        glyphMatrix.postConcat(matrix);
        glyphPath.transform(glyphMatrix);
        totalPath.addPath(glyphPath);
      } else {
        LOGE("TextShape::getPath() Failed to get path for glyph!");
        return {};
      }
    }
  }
  auto inverseMatrix = Matrix::MakeScale(1.f / resolutionScale, 1.f / resolutionScale);
  totalPath.transform(inverseMatrix);
  // addPath() in append mode keeps the destination fill type, which for an initially empty path
  // means the first glyph silently decides it. State it explicitly so the path and fillType()
  // always agree.
  totalPath.setFillType(fillType());
  return totalPath;
}
}  // namespace tgfx
