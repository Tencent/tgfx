/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
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

namespace tgfx {
static constexpr int kDelayedDeleteCount = 256;

inline uint16_t GetUniqueID() {
  static std::atomic<uint16_t> nextID{1};
  return nextID.fetch_add(1, std::memory_order_relaxed);
}

template <typename T>
class QueueNode {
 public:
  QueueNode() {
    QueueNode(nullptr);
  }

  explicit QueueNode(const T& element) : nextNode(nullptr), item(element), needDelete(false) {
    init();
  }

  explicit QueueNode(T&& element) : nextNode(nullptr), item(element), needDelete(false) {
    init();
  }

  uint64_t getPtrs() {
    return ptrs.load(std::memory_order_relaxed);
  }

  QueueNode* volatile nextNode = nullptr;
  T item = nullptr;
  std::atomic_bool needDelete = false;
  uint16_t uniqueID = 0;

 private:
  std::atomic_uint64_t ptrs = 0;

  void init() {
    uniqueID = GetUniqueID();
    auto address = reinterpret_cast<uint64_t>(this);
    ptrs.store((static_cast<uint64_t>(uniqueID) << 48) | (address & 0xFFFFFFFFFFFF),
               std::memory_order_relaxed);
  }
};

template <typename T>
class LockFreeQueue {
 public:
  LockFreeQueue() {
    header = tail = deleteTail = new QueueNode<T>();
    deleteTail->needDelete = true;
  }

  LockFreeQueue& operator=(const LockFreeQueue& other) = delete;
  LockFreeQueue(const LockFreeQueue& other) = delete;

  ~LockFreeQueue() {
    QueueNode<T>* pTail = deleteTail;
    while (pTail != nullptr) {
      QueueNode<T>* pNode = pTail;
      pTail = pNode->nextNode;
      delete pNode;
    }
  }

  T dequeue() {
    QueueNode<T>* tailNode = tail;
    do {
      if (tailNode->nextNode == nullptr) {
        return nullptr;
      }
    } while (tail.load()->getPtrs() == tailNode->getPtrs() &&
             !std::atomic_compare_exchange_weak(&tail, &tailNode, tailNode->nextNode));
    QueueNode<T>* popNode = tailNode->nextNode;
    T element = std::move(popNode->item);
    popNode->needDelete = true;
    popNode->item = nullptr;
    uint64_t currentSize = --size;
    uint64_t needDeleteNum = listSize.load() - currentSize;
    if (needDeleteNum > kDelayedDeleteCount) {
      if (++uniqueDeleteFlag == 1) {
        while (deleteTail->nextNode != nullptr && deleteTail->nextNode->needDelete &&
               needDeleteNum > kDelayedDeleteCount) {
          QueueNode<T>* deleteNode = deleteTail;
          deleteTail = deleteTail->nextNode;
          listSize--;
          needDeleteNum--;
          delete deleteNode;
          deleteNode = nullptr;
        }
      }
      --uniqueDeleteFlag;
    }
    return element;
  }

  bool enqueue(const T& element) {
    QueueNode<T>* newNode = new (std::nothrow) QueueNode<T>(std::move(element));
    if (newNode == nullptr) {
      return false;
    }
    QueueNode<T>* currentHeader = header;
    while (header.load()->getPtrs() == currentHeader->getPtrs() &&
           !std::atomic_compare_exchange_weak(&header, &currentHeader, newNode))
      ;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    currentHeader->nextNode = newNode;
    size++;
    listSize++;
    return true;
  }

  void clear() {
    while (dequeue() != nullptr)
      ;
    size = 0;
    listSize = 0;
  }

 private:
  std::atomic<QueueNode<T>*> header = nullptr;
  std::atomic<QueueNode<T>*> tail = nullptr;
  QueueNode<T>* deleteTail = nullptr;
  std::atomic_uint64_t size = 0;
  std::atomic_uint64_t listSize = 0;
  std::atomic_int uniqueDeleteFlag = 0;
};

}  // namespace tgfx
