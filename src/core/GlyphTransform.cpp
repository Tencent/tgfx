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
  switch (run.positioning) {
    case GlyphPositioning::Horizontal:
      return Matrix::MakeTrans(run.positions[index], run.offsetY);
    case GlyphPositioning::Point: {
      auto position = reinterpret_cast<const Point*>(run.positions)[index];
      return Matrix::MakeTrans(position.x, position.y);
    }
    case GlyphPositioning::RSXform: {
      const float* p = run.positions + index * 4;
      return Matrix::MakeAll(p[0], -p[1], p[2], p[1], p[0], p[3]);
    }
    case GlyphPositioning::Matrix: {
      const float* p = run.positions + index * 6;
      return Matrix::MakeAll(p[0], p[1], p[2], p[3], p[4], p[5]);
    }
  }
  return {};
}


}  // namespace tgfx
