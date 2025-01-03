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
class NonCopyable
{
  protected:
  NonCopyable() {}
  ~NonCopyable() {}

  private:
  NonCopyable(const NonCopyable&);
  const NonCopyable& operator=(const NonCopyable&);
};

template<typename T>
class LockFreeQueue : NonCopyable {
 public:
  using ElementType = T;
  LockFreeQueue() : mSize(0), mListSize(0), mUniqDelFlag(0) {
    mHead = mTail = mDelTail = new TNode;
    mDelTail->BeDeleted = true;
  }
  ~LockFreeQueue() {
    TNode* pTail = mDelTail;
    while (pTail != nullptr) {
      TNode* pNode = pTail;
      pTail = pNode->NextNode;
      delete pNode;
    }
  }
  bool dequeue() {
    TNode* pTail = mTail;
    do
    {
      if (pTail->NextNode == nullptr) return false;
    } while (!std::atomic_compare_exchange_weak(&mTail, &pTail, pTail->NextNode));

    TNode* pPop = pTail->NextNode;
//    OutItem = std::move(pPop->Item);
    pPop->BeDeleted = true;
    pPop->Item = ElementType();
    uint64_t uCurrSize = --mSize;
    uint64_t uNeedDelNum = mListSize.load() - uCurrSize;
    if (uNeedDelNum > 256) {
      if (++mUniqDelFlag == 1) {
        while (mDelTail->NextNode != nullptr && mDelTail->NextNode->BeDeleted && uNeedDelNum > 256) {
          TNode* pDel = mDelTail;
          mDelTail = mDelTail->NextNode;
          mListSize--;
          uNeedDelNum--;
          delete pDel;
        }
      }
      --mUniqDelFlag;
    }
    return true;
  }

  bool enqueue(const ElementType& InItem) {
    TNode* pNewNode = new (std::nothrow) TNode(InItem);
    if (pNewNode == nullptr) return false;

    TNode* pCurrHead = mHead;
    while (!std::atomic_compare_exchange_weak(&mHead, &pCurrHead, pNewNode));
    std::atomic_thread_fence(std::memory_order_seq_cst);
    pCurrHead->NextNode = pNewNode;
    mSize++;
    mListSize++;
    return true;
  }

  bool isEmpty() const { return mSize == 0; }

  // not support multi thread comsumers
  ElementType* peek() {
    if (mTail.load()->NextNode == nullptr) return nullptr;
    return &(mTail.load()->NextNode->Item);
  }

  // not support multi thread comsumers
  const ElementType* peek() const { return const_cast<LockFreeQueue*>(this)->peek(); }

  bool pop() {
    TNode* pTail = mTail;
    do {
      if (pTail->NextNode == nullptr) return false;
    } while (!std::atomic_compare_exchange_weak(&mTail, &pTail, pTail->NextNode));
    TNode* pPop = pTail->NextNode;
    pPop->BeDeleted = true;
    pPop->Item = ElementType();
    uint64_t uCurrSize = --mSize;
    uint64_t uNeedDelNum = mListSize.load() - uCurrSize;
    if (uNeedDelNum > 10) {
      if (++mUniqDelFlag == 1) {
        while (mDelTail->NextNode != nullptr && mDelTail->NextNode->BeDeleted && uNeedDelNum > 10) {
          TNode* pDel = mDelTail;
          mDelTail = mDelTail->NextNode;
          mListSize--;
          uNeedDelNum--;
          delete pDel;
        }
      }
      --mUniqDelFlag;
    }
    return true;
  }

  void empty() { while (pop()); }

  uint64_t getSize() const { return mSize; }

 private:
  struct TNode {
    TNode* volatile NextNode;
    ElementType Item;
    std::atomic_bool BeDeleted;
    TNode() : NextNode(nullptr), BeDeleted(false){}
    explicit TNode(const ElementType& InItem) : NextNode(nullptr), Item(InItem), BeDeleted(false) {}
    explicit TNode(ElementType& InItem) : NextNode(nullptr), Item(std::move(InItem)), BeDeleted(false) {}
  };

  std::atomic<TNode*> mHead;
  std::atomic<TNode*> mTail;
  TNode* mDelTail;

  std::atomic_uint64_t mSize;
  std::atomic_uint64_t mListSize;
  std::atomic_int mUniqDelFlag;
};

} // namespace tgfx

