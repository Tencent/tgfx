/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "concurrentqueue.h"
#ifdef TGFX_USE_THREADS
#include "lightweightsemaphore.h"
#endif
#include "tgfx/core/Task.h"

namespace tgfx {

#ifdef TGFX_USE_THREADS

/**
 * A priority task pool with lock-free submission and mutex-protected scheduling. Producers only
 * pay one atomic admission update, a lock-free enqueue, and a semaphore signal per task; workers
 * make every scheduling decision (priority, low-priority budget, standby growth, shrink, drain)
 * inside a single scheduling mutex. Wakeup permits are durable, so a notification can never be
 * lost the way a condition_variable signal can.
 */
class TaskPool {
 public:
  TaskPool();
  ~TaskPool();

  /**
   * Publishes a task and a durable scheduling notification. Returns false if submission is closed
   * or the queue rejects the task. An accepted call remains tracked until publication completes.
   */
  bool push(std::shared_ptr<Task> task, TaskPriority priority);

  /**
   * Changes the worker limit. Running tasks are not interrupted. Zero restores the default.
   */
  void setMaxThreadCount(size_t maxThreadCount);

  /**
   * Rejects new submissions and drains accepted work before joining workers. Must not be called
   * by a task executing in this pool. Concurrent releases are serialized.
   */
  void releaseThreads();

  /**
   * Reopens a fully released pool for subsequent submissions.
   */
  void reopen();

  size_t maxThreadCount();
  size_t totalThreads();
  size_t sleeperCount();
  size_t pendingCount();

 private:
  enum class Phase { Running, Draining, Closed };

  class SubmissionGuard {
   public:
    explicit SubmissionGuard(TaskPool* pool) : pool(pool) {
    }
    ~SubmissionGuard() {
      pool->leavePush();
    }
    SubmissionGuard(const SubmissionGuard&) = delete;
    SubmissionGuard& operator=(const SubmissionGuard&) = delete;

   private:
    TaskPool* pool = nullptr;
  };

  static constexpr uint64_t CLOSED_BIT = uint64_t{1} << 63;
  static constexpr uint64_t STARTED_BIT = uint64_t{1} << 62;
  static constexpr uint64_t PUSH_COUNT_MASK = STARTED_BIT - 1;
  static constexpr size_t PRIORITY_QUEUE_COUNT = static_cast<size_t>(TaskPriority::Low) + 1;
  std::atomic<uint64_t> admission = 0;
  std::mutex lifecycleMutex = {};
  std::mutex stateMutex = {};
  moodycamel::ConcurrentQueue<std::shared_ptr<Task>> priorityQueues[PRIORITY_QUEUE_COUNT];
  moodycamel::LightweightSemaphore workSignal{0, 0};
  moodycamel::LightweightSemaphore producersDone{0, 0};
  std::vector<std::thread> threadHandles = {};
  Phase phase = Phase::Running;
  size_t liveThreads = 0;
  size_t busyThreads = 0;
  size_t waitingThreads = 0;
  size_t maxThreads = 0;
  size_t lowPriorityThreads = 0;
  bool lowNeedsCheck = false;

  bool enterPush();
  void leavePush();
  void ensureStarted();
  void ensureStandbyLocked();
  void spawnWorkerLocked();
  std::shared_ptr<Task> waitForTask();
  std::shared_ptr<Task> claimLocked();
  void finishTask();
  void runLoop();
};

#else

/**
 * Single-threaded builds run every task inline on the submitting thread. The pool keeps the
 * same interface so callers need no conditional code, but submission always fails and the
 * caller executes the task itself.
 */
class TaskPool {
 public:
  bool push(std::shared_ptr<Task> task, TaskPriority priority) {
    static_cast<void>(task);
    static_cast<void>(priority);
    return false;
  }

  void setMaxThreadCount(size_t maxThreadCount) {
    static_cast<void>(maxThreadCount);
  }

  void releaseThreads() {
  }

  void reopen() {
  }

  size_t maxThreadCount() {
    return 0;
  }

  size_t totalThreads() {
    return 0;
  }

  size_t sleeperCount() {
    return 0;
  }

  size_t pendingCount() {
    return 0;
  }
};

#endif

class TaskGroup {
 private:
  TaskPool pool = {};
  static TaskGroup* GetInstance();
  TaskGroup();
  void setMaxThreadCount(size_t maxThreadCount);
  size_t maxThreadCount();
  bool pushTask(std::shared_ptr<Task> task, TaskPriority priority);
  void releaseThreads(bool exit);

  friend class Task;
  friend void OnAppExit();
};
}  // namespace tgfx
