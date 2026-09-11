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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "TaskGroup.h"
#include <chrono>
#include <cmath>
#include "MathExtra.h"
#include "core/utils/Log.h"

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace tgfx {
// Workers wake up periodically to recheck the exit conditions, so a missed wakeAll() delays
// shutdown or shrink by at most this long.
static constexpr auto THREAD_TIMEOUT = std::chrono::milliseconds(10000);
static constexpr size_t MAX_THREADS_SIZE = 32;
// 70% of max threads can run low priority tasks
static constexpr float LOW_PRIORITY_THREAD_RATIO = 0.7f;

static size_t GetDefaultMaxThreadCount() {
  size_t cpuCores = 0;
#ifdef __APPLE__
  int cores = 0;
  // hw.physicalcpu returns an int, so the buffer must be int-sized to avoid reading past it.
  size_t len = sizeof(cores);
  // We can get the exact number of physical CPUs on apple platforms.
  sysctlbyname("hw.physicalcpu", &cores, &len, nullptr, 0);
  cpuCores = cores > 0 ? static_cast<size_t>(cores) : 0;
#else
  cpuCores = std::thread::hardware_concurrency();
#endif
  if (cpuCores == 0) {
    cpuCores = 8;
  }
  if (cpuCores > MAX_THREADS_SIZE) {
    cpuCores = MAX_THREADS_SIZE;
  }
  return cpuCores;
}

TaskGroup* TaskGroup::GetInstance() {
  static auto& taskGroup = *new TaskGroup();
  return &taskGroup;
}

void TaskGroup::RunLoop(TaskGroup* taskGroup) {
  while (true) {
    std::shared_ptr<Task> task = nullptr;
    auto result = taskGroup->taskQueue.waitForTask(&task, taskGroup->totalThreads.load(),
                                                   taskGroup->lowPriorityThreads.load(),
                                                   THREAD_TIMEOUT.count());
    if (result == TaskQueue::ClaimResult::Claimed) {
      task->execute();
      continue;
    }
    if (taskGroup->taskQueue.isClosed()) {
      break;
    }
    if (taskGroup->shrinkSelf()) {
      break;
    }
    // Timed out or woken with the pool under the limit: claim again.
  }
}

void OnAppExit() {
  // Forces all pending tasks to be finished when the app is exiting to prevent accessing wild
  // pointers.
  TaskGroup::GetInstance()->releaseThreads(true);
}

TaskGroup::TaskGroup() : maxThreads(GetDefaultMaxThreadCount()) {
  lowPriorityThreads = static_cast<size_t>(
      FloatRoundToInt(static_cast<float>(maxThreads.load()) * LOW_PRIORITY_THREAD_RATIO));
  if (lowPriorityThreads < 1) {
    lowPriorityThreads = 1;
  }
  threads = new moodycamel::ConcurrentQueue<std::thread*>(maxThreads.load());
  std::atexit(OnAppExit);
}

void TaskGroup::setMaxThreadCount(size_t maxThreadCount) {
  if (maxThreadCount == 0) {
    maxThreadCount = GetDefaultMaxThreadCount();
  } else if (maxThreadCount > MAX_THREADS_SIZE) {
    maxThreadCount = MAX_THREADS_SIZE;
  }
  maxThreads = maxThreadCount;
  lowPriorityThreads = static_cast<size_t>(
      FloatRoundToInt(static_cast<float>(maxThreadCount) * LOW_PRIORITY_THREAD_RATIO));
  if (lowPriorityThreads < 1) {
    lowPriorityThreads = 1;
  }
  // Wake idle workers so they can exit if the pool exceeds the new limit.
  taskQueue.wakeAll();
}

size_t TaskGroup::maxThreadCount() const {
  return maxThreads.load();
}

bool TaskGroup::shrinkSelf() {
  size_t total = totalThreads.load();
  while (total > maxThreads.load()) {
    if (totalThreads.compare_exchange_weak(total, total - 1)) {
      return true;
    }
  }
  return false;
}

bool TaskGroup::spawnWorker() {
  // Reserve the slot before starting the thread so concurrent pushes cannot spawn past the cap.
  auto reserved = totalThreads.fetch_add(1) + 1;
  if (reserved > maxThreads.load()) {
    totalThreads.fetch_sub(1);
    return false;
  }
  auto thread = new (std::nothrow) std::thread(TaskGroup::RunLoop, this);
  if (thread == nullptr) {
    totalThreads.fetch_sub(1);
    return false;
  }
  if (!threads->enqueue(thread)) {
    // The threads queue is unbounded, so this only happens on OOM. Detach the started thread
    // so deleting the handle stays safe; it exits on the next close().
    thread->detach();
    delete thread;
    totalThreads.fetch_sub(1);
    return false;
  }
  return true;
}

bool TaskGroup::pushTask(std::shared_ptr<Task> task, TaskPriority priority) {
#ifndef TGFX_USE_THREADS
  return false;
#endif
  if (taskQueue.isClosed()) {
    return false;
  }
  auto result = taskQueue.enqueue(task, priority);
  if (result == TaskQueue::EnqueueResult::Rejected) {
    return false;
  }
  if (result == TaskQueue::EnqueueResult::NeedsWorker && totalThreads.load() < maxThreads.load()) {
    spawnWorker();
  }
  return true;
}

static void ReleaseThread(std::thread* thread) {
  if (thread->joinable()) {
    thread->join();
  }
  delete thread;
}

void TaskGroup::releaseThreads(bool exit) {
  // The queue closes and notifies while holding its lock, so a worker that has not entered its
  // wait yet observes the closed flag instead of missing the notification and sleeping for the
  // full THREAD_TIMEOUT.
  taskQueue.close();
  std::thread* thread = nullptr;
  while (threads->try_dequeue(thread)) {
    ReleaseThread(thread);
  }
  totalThreads = 0;
  DEBUG_ASSERT(taskQueue.sleeperCount() == 0)
  if (!exit) {
    taskQueue.reset();
  }
}
}  // namespace tgfx
