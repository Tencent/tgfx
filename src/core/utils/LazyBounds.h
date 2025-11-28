/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#pragma once

#include <atomic>
#include "tgfx/core/Once.h"
#include "tgfx/core/Rect.h"

namespace tgfx {
/**
 * LazyBounds is a thread-safe cache for storing bounding box.
 */
class LazyBounds {
 public:
  ~LazyBounds() {
    reset();
  }

  /**
   * Returns the cached bounding box. This method is thread-safe as long as there is no concurrent
   * reset() call.
   */
  const Rect* get() const {
    return bounds.load(std::memory_order_acquire);
  }

  /**
   * Sets the bounding box to the cache. This method is thread-safe as long as there is no
   * concurrent reset() call.
   */
  void update(const Rect& rect) const;

  /**
   * Resets the cached bounding box to its empty state. This method is not thread-safe.
   */
  void reset();

 private:
  mutable std::atomic<Rect*> bounds = {nullptr};
  mutable Once initOnce;
};
}  // namespace tgfx
