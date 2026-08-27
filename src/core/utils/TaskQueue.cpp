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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "TaskQueue.h"
#include <chrono>

namespace tgfx {

TaskQueue::EnqueueResult TaskQueue::enqueue(std::shared_ptr<Task> task, TaskPriority priority) {
  if (!priorityQueues[static_cast<size_t>(priority)].enqueue(task)) {
    return EnqueueResult::Rejected;
  }
  std::lock_guard<std::mutex> autoLock(locker);
  if (priority != TaskPriority::Low) {
    backlog++;
  }
  // A sleeping worker absorbs at most one task per wakeup. Comparing the claimable backlog
  // against the exact sleeper count makes a push landing inside a worker's wakeup window still
  // report the extra worker it needs.
  auto needsWorker = backlog > waitingThreads;
  if (waitingThreads > 0) {
    condition.notify_one();
  }
  return needsWorker ? EnqueueResult::NeedsWorker : EnqueueResult::Enqueued;
}

TaskQueue::ClaimResult TaskQueue::waitForTask(std::shared_ptr<Task>* task, size_t totalThreads,
                                              size_t lowPriorityThreads, uint64_t timeoutMs) {
  std::unique_lock<std::mutex> autoLock(locker);
  ++waitingThreads;
  *task = claimLocked(totalThreads, lowPriorityThreads);
  if (*task) {
    --waitingThreads;
    return ClaimResult::Claimed;
  }
  if (closed.load()) {
    --waitingThreads;
    return ClaimResult::Woken;
  }
  auto status = condition.wait_for(autoLock, std::chrono::milliseconds(timeoutMs));
  --waitingThreads;
  // Any wakeup hands control back to the caller, which re-evaluates the pool state (a task may
  // have arrived, the limit may have changed, or the queue may have closed) before claiming again.
  return status == std::cv_status::timeout ? ClaimResult::Timeout : ClaimResult::Woken;
}

std::shared_ptr<Task> TaskQueue::claimLocked(size_t totalThreads, size_t lowPriorityThreads) {
  std::shared_ptr<Task> task = nullptr;
  for (size_t i = 0; i < static_cast<size_t>(TaskPriority::Low); i++) {
    if (priorityQueues[i].try_dequeue(task)) {
      backlog--;
      return task;
    }
  }
  // Written in additive form (busy < low) so a stale totalThreads snapshot crossing a
  // concurrent shrink cannot underflow the subtraction.
  if (totalThreads < lowPriorityThreads + waitingThreads) {
    priorityQueues[static_cast<size_t>(TaskPriority::Low)].try_dequeue(task);
  }
  return task;
}

void TaskQueue::wakeAll() {
  std::lock_guard<std::mutex> autoLock(locker);
  condition.notify_all();
}

void TaskQueue::close() {
  std::lock_guard<std::mutex> autoLock(locker);
  closed = true;
  condition.notify_all();
}

void TaskQueue::reset() {
  std::lock_guard<std::mutex> autoLock(locker);
  closed = false;
}

bool TaskQueue::isClosed() const {
  return closed.load();
}

size_t TaskQueue::sleeperCount() {
  std::lock_guard<std::mutex> autoLock(locker);
  return waitingThreads;
}

size_t TaskQueue::pendingCount() {
  std::lock_guard<std::mutex> autoLock(locker);
  size_t count = 0;
  for (auto& queue : priorityQueues) {
    count += queue.size_approx();
  }
  return count;
}
}  // namespace tgfx
