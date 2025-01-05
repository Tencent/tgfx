/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
class NonCopyable {
 protected:
  NonCopyable() {
  }
  ~NonCopyable() {
  }

 private:
  NonCopyable(const NonCopyable&);
  const NonCopyable& operator=(const NonCopyable&);
};

template <typename T>
class LockFreeQueue : NonCopyable {
 public:
  using ElementType = T;
  LockFreeQueue() : _size(0), listSize(0), uniqueFlag(0) {
    header = tail = mDelTail = new TNode;
    mDelTail->beDeleted = true;
  }
  ~LockFreeQueue() {
    TNode* pTail = mDelTail;
    while (pTail != nullptr) {
      TNode* pNode = pTail;
      pTail = pNode->nextNode;
      delete pNode;
    }
  }
  bool dequeue(ElementType& OutItem) {
    TNode* pTail = tail;
    do {
      if (pTail->nextNode == nullptr) return false;
    } while (!std::atomic_compare_exchange_weak(&tail, &pTail, pTail->nextNode));

    TNode* pPop = pTail->nextNode;
    OutItem = std::move(pPop->item);
    pPop->beDeleted = true;
    pPop->item = ElementType();
    uint64_t uCurrSize = --_size;
    uint64_t uNeedDelNum = listSize.load() - uCurrSize;
    if (uNeedDelNum > 256) {
      if (++uniqueFlag == 1) {
        while (mDelTail->nextNode != nullptr && mDelTail->nextNode->beDeleted &&
               uNeedDelNum > 256) {
          TNode* pDel = mDelTail;
          mDelTail = mDelTail->nextNode;
          listSize--;
          uNeedDelNum--;
          delete pDel;
        }
      }
      --uniqueFlag;
    }
    return true;
  }

  bool enqueue(const ElementType& InItem) {
    TNode* pNewNode = new (std::nothrow) TNode(InItem);
    if (pNewNode == nullptr) return false;

    TNode* pCurrHead = header;
    while (!std::atomic_compare_exchange_weak(&header, &pCurrHead, pNewNode))
      ;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    pCurrHead->nextNode = pNewNode;
    _size++;
    listSize++;
    return true;
  }

  bool empty() const {
    return _size == 0;
  }

  bool pop() {
    TNode* pTail = tail;
    do {
      if (pTail->nextNode == nullptr) return false;
    } while (!std::atomic_compare_exchange_weak(&tail, &pTail, pTail->nextNode));
    TNode* pPop = pTail->nextNode;
    pPop->beDeleted = true;
    pPop->item = ElementType();
    uint64_t uCurrSize = --_size;
    uint64_t uNeedDelNum = listSize.load() - uCurrSize;
    if (uNeedDelNum > 256) {
      if (++uniqueFlag == 1) {
        while (mDelTail->nextNode != nullptr && mDelTail->nextNode->beDeleted &&
               uNeedDelNum > 256) {
          TNode* pDel = mDelTail;
          mDelTail = mDelTail->nextNode;
          listSize--;
          uNeedDelNum--;
          delete pDel;
        }
      }
      --uniqueFlag;
    }
    return true;
  }

  void clear() {
    while (pop())
      ;
  }

  uint64_t size() const {
    return _size;
  }

 private:
  struct TNode {
    TNode* volatile nextNode;
    ElementType item;
    std::atomic_bool beDeleted;
    TNode() : nextNode(nullptr), beDeleted(false) {
    }
    explicit TNode(const ElementType& InItem) : nextNode(nullptr), item(InItem), beDeleted(false) {
    }
    explicit TNode(ElementType& InItem)
        : nextNode(nullptr), item(std::move(InItem)), beDeleted(false) {
    }
  };

  std::atomic<TNode*> header = nullptr;
  std::atomic<TNode*> tail = nullptr;
  TNode* mDelTail = nullptr;

  std::atomic_uint64_t _size;
  std::atomic_uint64_t listSize;
  std::atomic_int uniqueFlag;
};

}  // namespace tgfx
