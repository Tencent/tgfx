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

#include "core/GlyphTransform.h"

namespace tgfx {

Matrix GetGlyphMatrix(const GlyphRun& run, size_t index) {
  Matrix matrix = {};
  switch (run.positioning) {
    case GlyphPositioning::Horizontal:
      matrix.setTranslate(run.positions[index], run.offsetY);
      break;
    case GlyphPositioning::Point: {
      auto* points = reinterpret_cast<const Point*>(run.positions);
      matrix.setTranslate(points[index].x, points[index].y);
      break;
    }
    case GlyphPositioning::RSXform: {
      const float* p = run.positions + index * 4;
      float scos = p[0];
      float ssin = p[1];
      matrix.setAll(scos, -ssin, p[2], ssin, scos, p[3]);
      break;
    }
    case GlyphPositioning::Matrix: {
      const float* p = run.positions + index * 6;
      matrix.setAll(p[0], p[1], p[2], p[3], p[4], p[5]);
      break;
    }
  }
  return matrix;
}


}  // namespace tgfx
