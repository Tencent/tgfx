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
// Which way an outline runs, taken from the sign of the area it encloses. Only the sign is used, so
// walking the polygon through each segment's end point -- the chord of a curve -- is enough for any
// outline a font produces; a curve does not turn a contour around without leaving its bounds.
static int OutlineDirection(const Path& path) {
  auto bounds = path.getBounds();
  if (bounds.isEmpty()) {
    return 0;
  }
  auto originX = static_cast<double>(bounds.centerX());
  auto originY = static_cast<double>(bounds.centerY());
  double area = 0.0;
  Point from = {};
  auto hasFrom = false;
  for (const auto& segment : path) {
    if (segment.verb == PathVerb::Move) {
      from = segment.points[0];
      hasFrom = true;
      continue;
    }
    if (!hasFrom || segment.verb == PathVerb::Close) {
      continue;
    }
    auto to = segment.points[1];
    if (segment.verb == PathVerb::Quad || segment.verb == PathVerb::Conic) {
      to = segment.points[2];
    } else if (segment.verb == PathVerb::Cubic) {
      to = segment.points[3];
    }
    auto ax = static_cast<double>(from.x) - originX;
    auto ay = static_cast<double>(from.y) - originY;
    auto bx = static_cast<double>(to.x) - originX;
    auto by = static_cast<double>(to.y) - originY;
    area += ax * by - bx * ay;
    from = to;
  }
  if (area > 0.0) {
    return 1;
  }
  return area < 0.0 ? -1 : 0;
}

static bool CanAppendWithoutInteraction(const Path& total, const Path& glyph) {
  if (total.getFillType() != glyph.getFillType()) {
    return false;
  }
  if (total.isInverseFillType() || glyph.isInverseFillType()) {
    return false;
  }
  if (total.getFillType() != PathFillType::Winding) {
    // Parity counts an overlap twice and cancels it, so an even-odd outline is always unioned.
    // Keeping that unconditional also keeps the merge identical to what it did before the fast
    // path existed, which matters for outlines that reach this from an embedded format.
    return false;
  }
  auto a = total.getBounds();
  auto b = glyph.getBounds();
  if (a.isEmpty() || b.isEmpty() || !a.intersect(b)) {
    // Disjoint bounds cannot touch, so there is no overlap for the merge to resolve.
    return true;
  }
  // Winding keeps the overlap filled, but only while both outlines run the same way: opposite
  // directions cancel out where they meet, which is what a mirrored or reflected glyph produces.
  auto totalDirection = OutlineDirection(total);
  auto glyphDirection = OutlineDirection(glyph);
  return totalDirection != 0 && glyphDirection != 0 && totalDirection == glyphDirection;
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
  // A boolean op reports the area it resolved with parity, so merging rewrites the rule the outline
  // claims. Outlines that overlap while running the same way round -- which is what faux bold and
  // tight kerning produce -- would then read as holes wherever they cross, because parity counts the
  // overlap twice and cancels it. Remember the rule every glyph agreed on and put it back once the
  // merge is done; nothing about the resolved area depends on parity here.
  auto commonFillType =
      glyphPaths.empty() ? PathFillType::Winding : glyphPaths.front().getFillType();
  auto mixedFillTypes = false;
  for (const auto& glyphPath : glyphPaths) {
    if (glyphPath.getFillType() != commonFillType) {
      mixedFillTypes = true;
      break;
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
  if (!mixedFillTypes && havePath) {
    totalPath.setFillType(commonFillType);
  }
  auto inverseMatrix = Matrix::MakeScale(1.f / resolutionScale, 1.f / resolutionScale);
  totalPath.transform(inverseMatrix);
  return totalPath;
}
}  // namespace tgfx
