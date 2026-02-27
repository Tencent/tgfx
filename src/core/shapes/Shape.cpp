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

#include "tgfx/core/Shape.h"
#include "core/utils/AtomicCache.h"
#include "tgfx/core/Path.h"

namespace tgfx {
Shape::~Shape() {
  AtomicCacheReset(bounds);
  AtomicCacheReset(path);
}

bool Shape::isInverseFillType() const {
  auto type = fillType();
  return type == PathFillType::InverseWinding || type == PathFillType::InverseEvenOdd;
}

Rect Shape::getBounds() const {
  if (auto cachedBounds = AtomicCacheGet(bounds)) {
    return *cachedBounds;
  }
  auto totalBounds = onGetBounds();
  AtomicCacheSet(bounds, &totalBounds);
  return totalBounds;
}

Path Shape::getPath() const {
  if (auto cachedPath = AtomicCacheGet(path)) {
    return *cachedPath;
  }
  auto totalPath = onGetPath(1.0f);
  AtomicCacheSet(path, &totalPath);
  return totalPath;
}
}  // namespace tgfx
