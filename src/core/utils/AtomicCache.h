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

#pragma once

#include <atomic>

namespace tgfx {
/**
 * Returns the cached value from the atomic pointer. Returns nullptr if no value is cached.
 */
template <typename T>
const T* AtomicCacheGet(const std::atomic<T*>& cache) {
  return cache.load(std::memory_order_acquire);
}

/**
 * Sets the value to the atomic cache. If the cache already has a value, the new value is deleted.
 */
template <typename T>
void AtomicCacheSet(std::atomic<T*>& cache, const T* value) {
  auto newValue = new T(*value);
  T* oldValue = nullptr;
  if (!cache.compare_exchange_strong(oldValue, newValue, std::memory_order_acq_rel)) {
    delete newValue;
  }
}

/**
 * Clears the atomic cache and deletes the old value.
 */
template <typename T>
void AtomicCacheReset(std::atomic<T*>& cache) {
  auto oldValue = cache.exchange(nullptr, std::memory_order_acq_rel);
  delete oldValue;
}
}  // namespace tgfx
