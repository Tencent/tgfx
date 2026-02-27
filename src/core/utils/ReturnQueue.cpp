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

#include "ReturnQueue.h"
#include "core/utils/Log.h"

namespace tgfx {
std::shared_ptr<ReturnQueue> ReturnQueue::Make() {
  auto queue = std::shared_ptr<ReturnQueue>(new ReturnQueue());
  queue->weakThis = queue;
  return queue;
}

void ReturnQueue::NotifyReferenceReachedZero(ReturnNode* node) {
  // Move unreferencedQueue to a local variable before enqueue. Once enqueued, the node may be
  // immediately dequeued and deleted by another thread, causing a data race if we access the node's
  // unreferencedQueue member afterwards.
  auto unreferencedQueue = std::move(node->unreferencedQueue);
  DEBUG_ASSERT(unreferencedQueue != nullptr);
  unreferencedQueue->queue.enqueue(node);
}

ReturnQueue::~ReturnQueue() {
  ReturnNode* node = nullptr;
  while (queue.try_dequeue(node)) {
    delete node;
  }
}

std::shared_ptr<ReturnNode> ReturnQueue::makeShared(ReturnNode* node) {
  auto reference = std::shared_ptr<ReturnNode>(node, NotifyReferenceReachedZero);
  reference->unreferencedQueue = weakThis.lock();
  return reference;
}

}  // namespace tgfx
