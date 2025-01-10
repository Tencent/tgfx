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
template <typename T>
class LockFreeQueue {
 public:
  using ElementType = T;
  LockFreeQueue() : _size(0) {
    header = tail = deleteTail = new QueueNode;
  }
  LockFreeQueue& operator=(const LockFreeQueue& other) = delete;
  LockFreeQueue(const LockFreeQueue& other) = delete;

  ~LockFreeQueue() {
    QueueNode* pTail = deleteTail;
    while (pTail != nullptr) {
      QueueNode* pNode = pTail;
      pTail = pNode->nextNode;
      delete pNode;
    }
  }

  ElementType dequeue() {
    QueueNode* tailNode = tail;
    ElementType element = ElementType();
    do {
      if (tailNode->nextNode == nullptr) return element;
    } while (!std::atomic_compare_exchange_weak(&tail, &tailNode, tailNode->nextNode));
    QueueNode* popNode = tailNode->nextNode;
    element = std::move(popNode->item);
    popNode->item = ElementType();
    if (deleteTail->nextNode && deleteTail->nextNode->nextNode) {
      QueueNode* deleteNode = deleteTail;
      deleteTail = deleteTail->nextNode;
      delete deleteNode;
      deleteNode = nullptr;
    }
    if (_size > 0) {
      _size--;
    }
    return element;
  }

  bool enqueue(const ElementType& element) {
    QueueNode* newNode = new (std::nothrow) QueueNode(element);
    if (newNode == nullptr) {
      return false;
    }
    QueueNode* currentHeader = header;
    while (!std::atomic_compare_exchange_weak(&header, &currentHeader, newNode))
      ;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    currentHeader->nextNode = newNode;
    _size++;
    return true;
  }

  bool empty() const {
    return _size == 0;
  }

  void clear() {
    while (dequeue() != nullptr)
      ;
  }

  uint64_t size() const {
    return _size;
  }

 private:
  struct QueueNode {
    QueueNode() : nextNode(nullptr) {
    }
    explicit QueueNode(const ElementType& element) : nextNode(nullptr), item(element) {
    }
    explicit QueueNode(ElementType& element) : nextNode(nullptr), item(std::move(element)) {
    }
    QueueNode* volatile nextNode;
    ElementType item;
  };

  std::atomic<QueueNode*> header = nullptr;
  std::atomic<QueueNode*> tail = nullptr;
  QueueNode* deleteTail = nullptr;
  std::atomic_uint64_t _size = 0;
};

}  // namespace tgfx
