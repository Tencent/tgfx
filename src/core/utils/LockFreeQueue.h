/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "Log.h"

#if __APPLE__
#include <TargetConditionals.h>
#if defined(__IPHONE_OS_VERSION_MIN_REQUIRED) && __IPHONE_OS_VERSION_MIN_REQUIRED < 110000
#define DISABLE_ALIGNAS 1
#endif
#endif

namespace tgfx {
static constexpr size_t CACHELINE_SIZE = 64;
// QUEUE_SIZE needs to be a power of 2, otherwise the implementation of GetIndex needs to be changed.
static constexpr uint32_t QUEUE_SIZE = 1024;

inline uint32_t GetIndex(uint32_t position) {
  return position & (QUEUE_SIZE - 1);
}

template <typename T>
class LockFreeQueue {
 public:
  LockFreeQueue() {
    queuePool = reinterpret_cast<T*>(std::calloc(QUEUE_SIZE, sizeof(T)));
    if (queuePool == nullptr) {
      LOGE("LockFreeQueue init Failed!\n");
      return;
    }
  }

  ~LockFreeQueue() {
    if (queuePool) {
      std::free(queuePool);
      queuePool = nullptr;
    }
  }

  T dequeue() {
    if (queuePool == nullptr) {
      return nullptr;
    }
    uint32_t newHead = 0;
    uint32_t oldHead = head.load(std::memory_order_relaxed);
    T element = nullptr;

    do {
      newHead = oldHead + 1;
      if (newHead == tailPosition.load(std::memory_order_acquire)) {
        return nullptr;
      }
      element = queuePool[GetIndex(newHead)];
    } while (!head.compare_exchange_weak(oldHead, newHead, std::memory_order_acq_rel,
                                         std::memory_order_relaxed));

    queuePool[GetIndex(newHead)] = nullptr;

    uint32_t newHeadPosition = 0;
    uint32_t oldHeadPosition = headPosition.load(std::memory_order_relaxed);
    do {
      newHeadPosition = oldHeadPosition + 1;
    } while (!headPosition.compare_exchange_weak(
        oldHeadPosition, newHeadPosition, std::memory_order_acq_rel, std::memory_order_relaxed));
    return element;
  }

  bool enqueue(const T& element) {
    if (queuePool == nullptr) {
      return false;
    }
    uint32_t newTail = 0;
    uint32_t oldTail = tail.load(std::memory_order_relaxed);

    do {
      newTail = oldTail + 1;
      if (GetIndex(newTail) == GetIndex(headPosition.load(std::memory_order_acquire))) {
        LOGI("The queue has reached its maximum capacity, capacity: %u!\n", QUEUE_SIZE);
        return false;
      }
    } while (!tail.compare_exchange_weak(oldTail, newTail, std::memory_order_acq_rel,
                                         std::memory_order_relaxed));

    queuePool[GetIndex(oldTail)] = std::move(element);

    uint32_t newTailPosition = 0;
    uint32_t oldTailPosition = tailPosition.load(std::memory_order_relaxed);
    do {
      newTailPosition = oldTailPosition + 1;
    } while (!tailPosition.compare_exchange_weak(
        oldTailPosition, newTailPosition, std::memory_order_acq_rel, std::memory_order_relaxed));
    return true;
  }

 private:
  T* queuePool = nullptr;
#ifdef DISABLE_ALIGNAS
  // head indicates the position after requesting space.
  std::atomic<uint32_t> head = {0};
  // headPosition indicates the position after release data.
  std::atomic<uint32_t> headPosition = {0};
  // tail indicates the position after requesting space.
  std::atomic<uint32_t> tail = {1};
  // tailPosition indicates the position after filling data.
  std::atomic<uint32_t> tailPosition = {1};
#else
  // head indicates the position after requesting space.
  alignas(CACHELINE_SIZE) std::atomic<uint32_t> head = {0};
  // headPosition indicates the position after release data.
  alignas(CACHELINE_SIZE) std::atomic<uint32_t> headPosition = {0};
  // tail indicates the position after requesting space.
  alignas(CACHELINE_SIZE) std::atomic<uint32_t> tail = {1};
  // tailPosition indicates the position after filling data.
  alignas(CACHELINE_SIZE) std::atomic<uint32_t> tailPosition = {1};
#endif
};

}  // namespace tgfx
