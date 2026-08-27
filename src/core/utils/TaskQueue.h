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

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include "concurrentqueue.h"
#include "tgfx/core/Task.h"

namespace tgfx {

/**
 * A self-synchronized priority task queue used by the TaskGroup thread pool. Producers enqueue
 * concurrently; workers claim tasks through waitForTask(), which sleeps on an internal condition
 * variable between claims. All scheduling counters live inside the queue's critical section, so
 * callers never handle locks or counters.
 */
class TaskQueue {
 public:
  /**
   * The outcome of enqueue().
   */
  enum class EnqueueResult {
    /**
     * The task was rejected, and the caller must execute it inline.
     */
    Rejected,
    /**
     * The task was enqueued; the existing workers are enough to absorb it.
     */
    Enqueued,
    /**
     * The task was enqueued, and the claimable backlog now exceeds the sleeping workers, so the
     * caller should ensure another worker thread exists.
     */
    NeedsWorker,
  };

  /**
   * The outcome of waitForTask().
   */
  enum class ClaimResult {
    /**
     * A task was claimed; the out parameter holds it.
     */
    Claimed,
    /**
     * No task was claimed before the timeout elapsed.
     */
    Timeout,
    /**
     * The wait was interrupted by wakeAll() or close().
     */
    Woken,
  };

  /**
   * Enqueues a task at the given priority.
   */
  EnqueueResult enqueue(std::shared_ptr<Task> task, TaskPriority priority);

  /**
   * Claims a task from the queues, sleeping up to the timeout when none is available.
   * @param task Set to the claimed task when returning Claimed.
   * @param totalThreads The current number of live workers in the pool.
   * @param lowPriorityThreads The maximum number of busy workers allowed before Low tasks can be
   * claimed.
   * @param timeoutMs The maximum duration in milliseconds to sleep when no task is available.
   * @return The outcome of the claim attempt.
   */
  ClaimResult waitForTask(std::shared_ptr<Task>* task, size_t totalThreads,
                          size_t lowPriorityThreads, uint64_t timeoutMs);

  /**
   * Wakes all sleeping workers without closing the queue. Used when the thread limit changes.
   */
  void wakeAll();

  /**
   * Closes the queue and wakes all sleeping workers. Further claims return immediately.
   */
  void close();

  /**
   * Reopens a closed queue, keeping any tasks that are still pending.
   */
  void reset();

  /**
   * Returns true if the queue has been closed and not reset.
   */
  bool isClosed() const;

  /**
   * Returns the number of workers currently sleeping inside waitForTask().
   */
  size_t sleeperCount();

  /**
   * Returns the approximate number of tasks still pending in the queues.
   */
  size_t pendingCount();

 private:
  std::mutex locker = {};
  std::condition_variable condition = {};
  std::atomic_bool closed = {false};
  // Counters below are guarded by locker. Workers only change waitingThreads inside the
  // critical section of waitForTask(), so producers reading it under the lock always see the
  // exact number of sleepers: a notified-but-not-yet-awake worker no longer counts.
  size_t waitingThreads = 0;
  // High and Medium priority tasks enqueued but not yet claimed. Low priority tasks are not
  // counted: the low priority gate can hold them for a long time by design, and counting them
  // would report a backlog that workers are not allowed to take.
  size_t backlog = 0;
  moodycamel::ConcurrentQueue<std::shared_ptr<Task>> priorityQueues[3];

  std::shared_ptr<Task> claimLocked(size_t totalThreads, size_t lowPriorityThreads);
};
}  // namespace tgfx
