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

#include "TaskGroup.h"
#include <algorithm>
#include <cstdlib>
#include "MathExtra.h"
#include "core/utils/Log.h"

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace tgfx {
static constexpr size_t MAX_THREADS_SIZE = 32;
static constexpr float LOW_PRIORITY_THREAD_RATIO = 0.7f;

static size_t LowPriorityThreadCount(size_t maxThreadCount) {
  return std::max(size_t{1}, static_cast<size_t>(FloatRoundToInt(
                                 static_cast<float>(maxThreadCount) * LOW_PRIORITY_THREAD_RATIO)));
}

static size_t GetDefaultMaxThreadCount() {
  size_t cpuCores = 0;
#ifdef __APPLE__
  int cores = 0;
  size_t len = sizeof(cores);
  sysctlbyname("hw.physicalcpu", &cores, &len, nullptr, 0);
  cpuCores = cores > 0 ? static_cast<size_t>(cores) : 0;
#else
  cpuCores = std::thread::hardware_concurrency();
#endif
  return std::min(cpuCores == 0 ? size_t{8} : cpuCores, MAX_THREADS_SIZE);
}

TaskPool::TaskPool() : threadHandles(MAX_THREADS_SIZE), maxThreads(GetDefaultMaxThreadCount()) {
  lowPriorityThreads = LowPriorityThreadCount(maxThreads);
}

TaskPool::~TaskPool() {
  releaseThreads(true);
}

bool TaskPool::push(std::shared_ptr<Task> task, TaskPriority priority) {
  if (!enterPush()) {
    return false;
  }
  SubmissionGuard guard(this);
  if (!ensureStarted()) {
    return false;
  }
  if (!priorityQueues[static_cast<size_t>(priority)].enqueue(std::move(task))) {
    return false;
  }
  workSignal.signal();
  return true;
}

void TaskPool::setMaxThreadCount(size_t maxThreadCount) {
  if (maxThreadCount == 0) {
    maxThreadCount = GetDefaultMaxThreadCount();
  }
  std::lock_guard<std::mutex> lock(stateMutex);
  maxThreads = maxThreadCount;
  lowPriorityThreads = LowPriorityThreadCount(maxThreads);
  if (phase == Phase::Running && (admission.load(std::memory_order_acquire) & STARTED_BIT)) {
    ensureStandbyLocked();
    workSignal.signal(static_cast<ptrdiff_t>(liveThreads));
  }
}

size_t TaskPool::maxThreadCount() {
  std::lock_guard<std::mutex> lock(stateMutex);
  return maxThreads;
}

void TaskPool::releaseThreads(bool exit) {
  std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
  auto state = admission.fetch_or(CLOSED_BIT, std::memory_order_acq_rel);
  if ((state & PUSH_COUNT_MASK) != 0) {
    while (!producersDone.wait()) {
    }
  }
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (phase == Phase::Closed) {
      return;
    }
    // exit=true drops queued tasks and exits workers immediately (app exit); exit=false drains
    // accepted work first so the pool can reopen with nothing lost.
    phase = exit ? Phase::Closing : Phase::Draining;
    workSignal.signal(static_cast<ptrdiff_t>(liveThreads));
  }
  std::thread* handle = nullptr;
  while (threadHandles.try_dequeue(handle)) {
    if (handle->joinable()) {
      handle->join();
    }
    delete handle;
  }
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    // Detached workers (handle-queue OOM fallback) are not joinable and may still be exiting
    // here, so no thread-count assertion is made on this path.
    DEBUG_ASSERT(phase == Phase::Closing || phase == Phase::Draining);
    for (auto& queue : priorityQueues) {
      if (exit) {
        std::shared_ptr<Task> task = nullptr;
        while (queue.try_dequeue(task)) {
        }
      } else {
        DEBUG_ASSERT(queue.size_approx() == 0);
        static_cast<void>(queue);
      }
    }
    while (workSignal.tryWait()) {
    }
    lowNeedsCheck = false;
    phase = Phase::Closed;
  }
}

void TaskPool::reopen() {
  std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
  std::lock_guard<std::mutex> lock(stateMutex);
  DEBUG_ASSERT(phase == Phase::Closed);
  phase = Phase::Running;
  admission.store(0, std::memory_order_release);
}

bool TaskPool::enterPush() {
  auto state = admission.load(std::memory_order_acquire);
  while (!(state & CLOSED_BIT)) {
    DEBUG_ASSERT((state & PUSH_COUNT_MASK) != PUSH_COUNT_MASK);
    if (admission.compare_exchange_weak(state, state + 1, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
      return true;
    }
  }
  return false;
}

void TaskPool::leavePush() {
  auto state = admission.fetch_sub(1, std::memory_order_acq_rel);
  DEBUG_ASSERT((state & PUSH_COUNT_MASK) > 0);
  if ((state & CLOSED_BIT) && (state & PUSH_COUNT_MASK) == 1) {
    producersDone.signal();
  }
}

bool TaskPool::ensureStarted() {
  if (admission.load(std::memory_order_acquire) & STARTED_BIT) {
    return true;
  }
  std::lock_guard<std::mutex> lock(stateMutex);
  if (!(admission.load(std::memory_order_acquire) & STARTED_BIT)) {
    DEBUG_ASSERT(phase == Phase::Running);
    if (!spawnWorkerLocked()) {
      return false;
    }
    admission.fetch_or(STARTED_BIT, std::memory_order_release);
  }
  return true;
}

void TaskPool::ensureStandbyLocked() {
  if (phase == Phase::Running && liveThreads == busyThreads && liveThreads < maxThreads) {
    spawnWorkerLocked();
  }
}

bool TaskPool::spawnWorkerLocked() {
  // Register the handle while holding stateMutex; a new worker must acquire it before doing any
  // work. Allocation failure returns false so the caller falls back to inline execution. If the
  // handle queue rejects the thread (OOM), the thread keeps running detached and exits when the
  // pool closes; it just cannot be joined, so liveThreads may briefly stay above zero after the
  // last join.
  auto thread = new (std::nothrow) std::thread(&TaskPool::runLoop, this);
  if (thread == nullptr) {
    return false;
  }
  if (!threadHandles.enqueue(thread)) {
    // The handle queue only fails on OOM. The thread is already running, so it releases its slot
    // when the pool closes; it just cannot be joined.
    thread->detach();
    delete thread;
  }
  ++liveThreads;
  return true;
}

std::shared_ptr<Task> TaskPool::waitForTask() {
  while (true) {
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      ++waitingThreads;
    }
    while (!workSignal.wait()) {
    }
    std::lock_guard<std::mutex> lock(stateMutex);
    --waitingThreads;
    if (phase == Phase::Closing) {
      --liveThreads;
      return nullptr;
    }
    if (phase == Phase::Running && liveThreads > maxThreads) {
      --liveThreads;
      // Shrinking does not hand back a notification: lowering the limit posts liveThreads permits,
      // which already covers every waiting worker; the exiting worker's consumed permit is covered
      // by that extra signal.
      return nullptr;
    }
    auto task = claimLocked();
    if (task != nullptr) {
      ++busyThreads;
      ensureStandbyLocked();
      return task;
    }
    if (phase == Phase::Draining) {
      --liveThreads;
      return nullptr;
    }
  }
}

std::shared_ptr<Task> TaskPool::claimLocked() {
  std::shared_ptr<Task> task = nullptr;
  for (size_t i = 0; i < static_cast<size_t>(TaskPriority::Low); ++i) {
    if (priorityQueues[i].try_dequeue(task)) {
      return task;
    }
  }
  if (phase == Phase::Draining || busyThreads < lowPriorityThreads) {
    if (priorityQueues[static_cast<size_t>(TaskPriority::Low)].try_dequeue(task)) {
      return task;
    }
    lowNeedsCheck = false;
  } else {
    // Keep this hint until an eligible dequeue actually finds the Low queue empty. Notifications
    // consumed while its budget was exhausted must not strand tasks after other work completes.
    lowNeedsCheck = true;
  }
  return nullptr;
}

void TaskPool::finishTask() {
  std::lock_guard<std::mutex> lock(stateMutex);
  DEBUG_ASSERT(busyThreads > 0);
  --busyThreads;
  if (phase == Phase::Draining) {
    // Continue draining even if Low notifications were consumed before the pool closed.
    workSignal.signal();
  } else if (lowNeedsCheck && busyThreads < lowPriorityThreads) {
    workSignal.signal(static_cast<ptrdiff_t>(liveThreads));
  }
}

void TaskPool::runLoop() {
  while (auto task = waitForTask()) {
    task->execute();
    finishTask();
  }
}

TaskGroup* TaskGroup::GetInstance() {
  static auto& taskGroup = *new TaskGroup();
  return &taskGroup;
}

// Finishes in-flight tasks and drops queued ones when the app is exiting, so no task code runs
// during static destruction.
void OnAppExit() {
  TaskGroup::GetInstance()->releaseThreads(true);
}

TaskGroup::TaskGroup() {
  std::atexit(OnAppExit);
}

void TaskGroup::setMaxThreadCount(size_t maxThreadCount) {
  pool.setMaxThreadCount(maxThreadCount);
}

size_t TaskGroup::maxThreadCount() {
  return pool.maxThreadCount();
}

bool TaskGroup::pushTask(std::shared_ptr<Task> task, TaskPriority priority) {
#ifndef TGFX_USE_THREADS
  static_cast<void>(task);
  static_cast<void>(priority);
  return false;
#else
  return pool.push(std::move(task), priority);
#endif
}

void TaskGroup::releaseThreads(bool exit) {
  pool.releaseThreads(exit);
  if (!exit) {
    pool.reopen();
  }
}
}  // namespace tgfx
