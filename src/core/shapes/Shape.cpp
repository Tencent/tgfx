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

namespace tgfx {
Shape::~Shape() {
  auto oldBounds = bounds.exchange(nullptr, std::memory_order_acq_rel);
  delete oldBounds;
  auto oldPath = cachedPath.exchange(nullptr, std::memory_order_acq_rel);
  delete oldPath;
}

Rect Shape::getBounds() const {
  if (auto cachedBounds = bounds.load(std::memory_order_acquire)) {
    return *cachedBounds;
  }
  auto totalBounds = onGetBounds();
  auto newBounds = new Rect(totalBounds);
  Rect* oldBounds = nullptr;
  if (!bounds.compare_exchange_strong(oldBounds, newBounds, std::memory_order_acq_rel)) {
    delete newBounds;
  }
  return totalBounds;
}

Path Shape::getPath() const {
  // Check if we have a cached path
  if (auto cached = cachedPath.load(std::memory_order_acquire)) {
    return *cached;
  }

  // For the outermost layer (PathShape), cache the path
  // For complex shapes, still compute without caching to maintain flexibility
  auto computedPath = getPathWithCache();

  auto newPath = new Path(computedPath);
  Path* oldPath = nullptr;
  if (!cachedPath.compare_exchange_strong(oldPath, newPath, std::memory_order_acq_rel)) {
    delete newPath;
    // If cache was set by another thread, return the cached value
    if (auto cached = cachedPath.load(std::memory_order_acquire)) {
      return *cached;
    }
  }

  return computedPath;
}

Path Shape::getPathWithCache() const {
  if (auto cached = cachedPath.load(std::memory_order_acquire)) {
    return *cached;
  }
  return onGetPath(1.0f);
}

}  // namespace tgfx
