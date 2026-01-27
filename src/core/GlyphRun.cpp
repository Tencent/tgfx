/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "core/GlyphRun.h"
#include "core/RunRecord.h"

namespace tgfx {

GlyphRun GlyphRun::From(const RunRecord* record) {
  GlyphRun run;
  run.font = record->font;
  run.positioning = record->positioning;
  run._runSize = record->glyphCount;
  run.glyphs = record->glyphBuffer();
  run.positions = record->posBuffer();
  run.offsetY = record->y;
  return run;
}

Matrix GlyphRun::getMatrix(size_t index) const {
  Matrix matrix = {};
  getMatrix(index, &matrix);
  return matrix;
}

void GlyphRun::getMatrix(size_t index, Matrix* matrix) const {
  switch (positioning) {
    case GlyphPositioning::Horizontal:
      matrix->setTranslate(positions[index], offsetY);
      break;
    case GlyphPositioning::Point: {
      auto point = reinterpret_cast<const Point*>(positions)[index];
      matrix->setTranslate(point.x, point.y);
      break;
    }
    case GlyphPositioning::RSXform: {
      const float* p = positions + index * 4;
      float scos = p[0];
      float ssin = p[1];
      matrix->setAll(scos, -ssin, p[2], ssin, scos, p[3]);
      break;
    }
    case GlyphPositioning::Matrix: {
      const float* p = positions + index * 6;
      matrix->setAll(p[0], p[1], p[2], p[3], p[4], p[5]);
      break;
    }
  }
}

bool GlyphRun::isGlyphVisible(size_t index, const Rect& scaledBounds,
                              const Rect& clipBounds) const {
  auto bounds = scaledBounds;
  switch (positioning) {
    case GlyphPositioning::Horizontal:
      bounds.offset(positions[index], offsetY);
      break;
    case GlyphPositioning::Point: {
      auto point = reinterpret_cast<const Point*>(positions)[index];
      bounds.offset(point.x, point.y);
      break;
    }
    default: {
      Matrix glyphMatrix = {};
      getMatrix(index, &glyphMatrix);
      bounds = glyphMatrix.mapRect(bounds);
    }
  }
  return Rect::Intersects(bounds, clipBounds);
}

}  // namespace tgfx
