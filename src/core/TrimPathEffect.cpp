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

std::shared_ptr<PathEffect> PathEffect::MakeTrim(float startT, float stopT) {
  if (isnan(startT) || isnan(stopT)) {
    return nullptr;
  }
  if (startT <= 0 && stopT >= 1) {
    return nullptr;
  }
  startT = std::max(0.f, std::min(startT, 1.f));
  stopT = std::max(0.f, std::min(stopT, 1.f));
  return std::shared_ptr<PathEffect>(new TrimPathEffect(startT, stopT));
}

bool TrimPathEffect::filterPath(Path* path) const {
  if (path == nullptr) {
    return false;
  }
  if (startT >= stopT) {
    path->reset();
    return true;
  }
  auto fillType = path->getFillType();
  auto pathMeasure = PathMeasure::MakeFrom(*path);
  auto length = pathMeasure->getLength();
  auto start = startT * length;
  auto end = stopT * length;
  Path tempPath = {};
  if (!pathMeasure->getSegment(start, end, &tempPath)) {
    return false;
  }
  tempPath.setFillType(fillType);
  *path = std::move(tempPath);
  return true;
}

}  // namespace tgfx
