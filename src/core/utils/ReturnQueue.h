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

#include <concurrentqueue.h>
#include <memory>

namespace tgfx {
class ReturnNode;

/**
 * A thread-safe queue for storing ReturnNode objects that are no longer referenced. When a
 * ReturnNode's reference count drops to zero, it is added to this queue for later processing.
 * Any returned nodes that are not processed will be deleted when the ReturnQueue is destroyed.
 */
class ReturnQueue {
 public:
  /**
   * Creates a new ReturnQueue instance.
   */
  static std::shared_ptr<ReturnQueue> Make();

  ~ReturnQueue();

  /**
   * Wraps a ReturnNode in a shared_ptr that will add the node to this queue when its reference
   * count reaches zero.
   */
  std::shared_ptr<ReturnNode> makeShared(ReturnNode* node);

  /**
   * Attempts to dequeue a ReturnNode from the queue. Returns nullptr if the queue is empty.
   */
  ReturnNode* dequeue() {
    ReturnNode* node = nullptr;
    return queue.try_dequeue(node) ? node : nullptr;
  }

 private:
  std::weak_ptr<ReturnQueue> weakThis;
  moodycamel::ConcurrentQueue<ReturnNode*> queue;

  static void NotifyReferenceReachedZero(ReturnNode* node);

  ReturnQueue() = default;
};

/**
 * A base class for objects that can be managed by a ReturnQueue. When the reference count of a
 * ReturnNode reaches zero, it is added to its associated ReturnQueue.
 */
class ReturnNode {
 public:
  virtual ~ReturnNode() = default;

 private:
  std::shared_ptr<ReturnQueue> unreferencedQueue = nullptr;

  friend class ReturnQueue;
};
}  // namespace tgfx
