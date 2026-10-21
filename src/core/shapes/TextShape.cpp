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

// A path with no contour segments fills nothing, unless it carries an inverse fill type: that one
// fills the entire plane. Shape::MakeFrom(Path) and ShapeRasterizer already draw this distinction.
static bool FillsWholePlane(const Path& path) {
  return path.isEmpty() && path.isInverseFillType();
}

// Whether two outlines can be appended instead of merged. Three things have to hold, and disjoint
// bounds only buys the last one:
//  * the rules must match, because the merged path carries a single rule - appending a parity
//    outline onto a winding one would fill the former's counters;
//  * neither may be inverse, since an inverse outline covers the whole plane and swallows the rest;
//  * the bounds must not meet, because once they do, two contours running opposite ways cancel
//    where they cross and the union of the glyphs is lost.
static bool CanAppendWithoutInteraction(const Path& total, const Path& glyph) {
  if (total.getFillType() != glyph.getFillType()) {
    return false;
  }
  if (total.isInverseFillType() || glyph.isInverseFillType()) {
    return false;
  }
  auto a = total.getBounds();
  auto b = glyph.getBounds();
  return a.isEmpty() || b.isEmpty() || !a.intersect(b);
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
  Path totalPath = {};
  auto havePath = false;
  for (auto& glyphPath : glyphPaths) {
    if (!havePath || FillsWholePlane(glyphPath)) {
      // A whole-plane outline absorbs every other one, so it also replaces what was collected.
      totalPath = glyphPath;
      havePath = true;
    } else if (FillsWholePlane(totalPath)) {
      continue;
    } else if (CanAppendWithoutInteraction(totalPath, glyphPath)) {
      // Appending keeps each outline's own area only when the two cannot touch: disjoint bounds
      // mean no overlap to resolve. Two winding outlines that do overlap must still be unioned,
      // because opposite directions cancel each other out where they meet - which is what a
      // mirrored or reflected glyph produces.
      totalPath.addPath(glyphPath);
    } else {
      // isEmpty() cannot stand in for "not initialized" here: an intermediate result may fill the
      // whole plane, and pathops treats an empty operand as absent, dropping the inverse fill
      // type and with it the whole plane.
      auto merged = false;
      totalPath.addPath(glyphPath, PathOp::Union, &merged);
      if (!merged) {
        // Falling back to an append would hand back a shape whose filled area is not the union of
        // the glyphs, which is the one thing this merge has to guarantee.
        LOGE("TextShape::getPath() Failed to merge a glyph outline!");
        return {};
      }
    }
  }
  auto inverseMatrix = Matrix::MakeScale(1.f / resolutionScale, 1.f / resolutionScale);
  totalPath.transform(inverseMatrix);
  return totalPath;
}
}  // namespace tgfx
