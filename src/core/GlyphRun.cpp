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

unsigned ScalarsPerGlyph(GlyphPositionMode layout) {
  switch (layout) {
    case GlyphPositionMode::Horizontal:
      return 1;
    case GlyphPositionMode::Point:
      return 2;
    case GlyphPositionMode::RSXform:
      return 4;
    case GlyphPositionMode::Matrix:
      return 6;
  }
  return 0;
}

size_t GlyphRun::glyphCount() const {
  return record->glyphCount;
}

const GlyphID* GlyphRun::glyphs() const {
  return record->glyphBuffer();
}

const Font& GlyphRun::font() const {
  return record->font;
}

GlyphPositionMode GlyphRun::positionMode() const {
  return record->positionMode;
}

const float* GlyphRun::positions() const {
  return record->posBuffer();
}

float GlyphRun::offsetY() const {
  return record->y;
}

const Point* GlyphRun::points() const {
  return reinterpret_cast<const Point*>(record->posBuffer());
}

const RSXform* GlyphRun::xforms() const {
  return reinterpret_cast<const RSXform*>(record->posBuffer());
}

const Matrix* GlyphRun::matrices() const {
  return reinterpret_cast<const Matrix*>(record->posBuffer());
}

Matrix ComputeGlyphMatrix(const GlyphRun& run, size_t index) {
  Matrix matrix = {};
  ComputeGlyphMatrix(run, index, &matrix);
  return matrix;
}

void ComputeGlyphMatrix(const GlyphRun& run, size_t index, Matrix* matrix) {
  const float* positions = run.positions();
  switch (run.positionMode()) {
    case GlyphPositionMode::Horizontal:
      matrix->setTranslate(positions[index], run.offsetY());
      break;
    case GlyphPositionMode::Point: {
      auto point = run.points()[index];
      matrix->setTranslate(point.x, point.y);
      break;
    }
    case GlyphPositionMode::RSXform: {
      const float* p = positions + index * 4;
      float scos = p[0];
      float ssin = p[1];
      matrix->setAll(scos, -ssin, p[2], ssin, scos, p[3]);
      break;
    }
    case GlyphPositionMode::Matrix: {
      const float* p = positions + index * 6;
      matrix->setAll(p[0], p[1], p[2], p[3], p[4], p[5]);
      break;
    }
  }
}

Rect MapGlyphBounds(const GlyphRun& run, size_t index, const Rect& bounds) {
  switch (run.positionMode()) {
    case GlyphPositionMode::Horizontal:
      return bounds.makeOffset(run.positions()[index], run.offsetY());
    case GlyphPositionMode::Point: {
      auto point = run.points()[index];
      return bounds.makeOffset(point.x, point.y);
    }
    default:
      return ComputeGlyphMatrix(run, index).mapRect(bounds);
  }
}

bool HasComplexTransform(const GlyphRun& run) {
  return run.positionMode() == GlyphPositionMode::RSXform ||
         run.positionMode() == GlyphPositionMode::Matrix;
}

}  // namespace tgfx
