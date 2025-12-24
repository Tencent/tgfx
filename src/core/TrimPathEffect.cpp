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

#include "TrimPathEffect.h"
#include "core/PathRef.h"
#include "tgfx/core/PathMeasure.h"

namespace tgfx {
using namespace pk;

std::shared_ptr<PathEffect> PathEffect::MakeTrim(float startT, float stopT, bool inverted) {
  if (std::isnan(startT) || std::isnan(stopT)) {
    return nullptr;
  }
  // Return nullptr if the result would be the full path (no trimming needed)
  if (!inverted && startT <= 0.f && stopT >= 1.f) {
    return nullptr;
  }
  // Clamp to [0, 1] range
  startT = std::max(0.f, std::min(startT, 1.f));
  stopT = std::max(0.f, std::min(stopT, 1.f));
  // Inverted mode with startT >= stopT means full path
  if (inverted && startT >= stopT) {
    return nullptr;
  }
  return std::shared_ptr<PathEffect>(new TrimPathEffect(startT, stopT, inverted));
}

bool TrimPathEffect::filterPath(Path* path) const {
  if (path == nullptr) {
    return false;
  }
  // Normal mode with startT >= stopT results in empty path
  if (!inverted && startT >= stopT) {
    path->reset();
    return true;
  }
  auto fillType = path->getFillType();
  auto pathMeasure = PathMeasure::MakeFrom(*path);
  auto length = pathMeasure->getLength();
  Path tempPath = {};

  if (!inverted) {
    // Normal mode: return segment [startT, stopT]
    auto start = startT * length;
    auto end = stopT * length;
    pathMeasure->getSegment(start, end, &tempPath);
  } else {
    // Inverted mode: return segments [0, startT] + [stopT, 1]
    if (startT > 0.f) {
      pathMeasure->getSegment(0.f, startT * length, &tempPath);
    }
    if (stopT < 1.f) {
      pathMeasure->getSegment(stopT * length, length, &tempPath);
    }
  }

  tempPath.setFillType(fillType);
  *path = std::move(tempPath);
  return true;
}

}  // namespace tgfx
