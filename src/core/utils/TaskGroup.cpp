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
#include <cmath>
#include <cstdlib>
#include "MathExtra.h"
#include "core/utils/Log.h"

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace tgfx {
static constexpr auto THREAD_TIMEOUT = std::chrono::seconds(10);
static constexpr size_t MAX_THREADS_SIZE = 32;
static constexpr size_t TASK_PRIORITY_SIZE = 3;
// 70% of max threads can run low priority tasks
static constexpr float LOW_PRIORITY_THREAD_RATIO = 0.7f;

static size_t GetDefaultMaxThreadCount() {
  size_t cpuCores = 0;
#ifdef __APPLE__
  size_t len = sizeof(cpuCores);
  // We can get the exact number of physical CPUs on apple platforms.
  sysctlbyname("hw.physicalcpu", &cpuCores, &len, nullptr, 0);
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
    auto task = taskGroup->popTask();
    if (task == nullptr) {
      if (taskGroup->exited) {
        break;
      }
      if (taskGroup->shrinkThread()) {
        break;
      }
      continue;
    }
    task->execute();
  }
}

void OnAppExit() {
  // Forces all pending tasks to be finished when the app is exiting to prevent accessing wild
  // pointers.
  TaskGroup::GetInstance()->exit();
}

TaskGroup::TaskGroup() : maxThreads(GetDefaultMaxThreadCount()) {
  lowPriorityThreads = static_cast<size_t>(
      FloatRoundToInt(static_cast<float>(maxThreads.load()) * LOW_PRIORITY_THREAD_RATIO));
  if (lowPriorityThreads < 1) {
    lowPriorityThreads = 1;
  }
  threads = new moodycamel::ConcurrentQueue<std::thread*>(maxThreads.load());
  priorityQueues.reserve(TASK_PRIORITY_SIZE);
  for (size_t i = 0; i < TASK_PRIORITY_SIZE; i++) {
    auto queue = new moodycamel::ConcurrentQueue<std::shared_ptr<Task>>();
    priorityQueues.push_back(queue);
  }
  std::atexit(OnAppExit);
}

void TaskGroup::setMaxThreadCount(size_t maxThreadCount) {
  if (maxThreadCount == 0) {
    maxThreadCount = GetDefaultMaxThreadCount();
  }
  std::lock_guard<std::mutex> autoLock(locker);
  maxThreads = maxThreadCount;
  lowPriorityThreads = static_cast<size_t>(
      FloatRoundToInt(static_cast<float>(maxThreadCount) * LOW_PRIORITY_THREAD_RATIO));
  if (lowPriorityThreads < 1) {
    lowPriorityThreads = 1;
  }
  // Wake up idle threads so they can exit if the pool exceeds the new limit.
  condition.notify_all();
}

size_t TaskGroup::maxThreadCount() const {
  return maxThreads.load();
}

bool TaskGroup::shouldExit() const {
  return exited || totalThreads.load() > maxThreads.load();
}

bool TaskGroup::shrinkThread() {
  size_t total = totalThreads.load();
  while (total > maxThreads.load()) {
    if (totalThreads.compare_exchange_weak(total, total - 1)) {
      return true;
    }
  }
  return false;
}

bool TaskGroup::checkThreads() {
  if (waitingThreads.load() == 0 && totalThreads.load() < maxThreads.load()) {
    auto thread = new (std::nothrow) std::thread(TaskGroup::RunLoop, this);
    if (thread) {
      if (threads->enqueue(thread)) {
        ++totalThreads;
      } else {
        delete thread;
        return false;
      }
    }
  } else {
    return true;
  }
  return totalThreads > 0;
}

bool TaskGroup::pushTask(std::shared_ptr<Task> task, TaskPriority priority) {
#ifndef TGFX_USE_THREADS
  return false;
#endif
  if (exited || !checkThreads()) {
    return false;
  }
  auto& queue = priorityQueues[static_cast<size_t>(priority)];
  if (!queue->enqueue(task)) {
    return false;
  }
  std::lock_guard<std::mutex> autoLock(locker);
  if (waitingThreads > 0) {
    condition.notify_one();
  }
  return true;
}

std::shared_ptr<Task> TaskGroup::tryDequeueTask() {
  std::shared_ptr<Task> task = nullptr;
  for (size_t i = 0; i < static_cast<size_t>(TaskPriority::Low); i++) {
    if (priorityQueues[i]->try_dequeue(task)) {
      return task;
    }
  }
  if (totalThreads.load() - waitingThreads.load() < lowPriorityThreads.load()) {
    auto& queue = priorityQueues[static_cast<size_t>(TaskPriority::Low)];
    if (queue->try_dequeue(task)) {
      return task;
    }
  }
  return nullptr;
}

std::shared_ptr<Task> TaskGroup::popTask() {
  while (!exited) {
    auto task = tryDequeueTask();
    if (task) {
      return task;
    }
    if (shouldExit()) {
      return nullptr;
    }
    ++waitingThreads;
    {
      std::unique_lock<std::mutex> autoLock(locker);
      // Re-check the queues while holding the lock so a task pushed concurrently cannot be missed
      // by a lost notification.
      task = tryDequeueTask();
      if (task) {
        --waitingThreads;
        return task;
      }
      if (shouldExit()) {
        --waitingThreads;
        return nullptr;
      }
      auto status = condition.wait_for(autoLock, THREAD_TIMEOUT);
      if (status == std::cv_status::timeout) {
        --waitingThreads;
        return nullptr;
      }
    }
    --waitingThreads;
  }
  return nullptr;
}

void TaskGroup::exit() {
  releaseThreads(true);
}

static void ReleaseThread(std::thread* thread) {
  if (thread->joinable()) {
    thread->join();
  }
  delete thread;
}

void TaskGroup::releaseThreads(bool exit) {
  // Set exited and notify while holding the lock, so a worker that has not entered its wait yet
  // will observe exited in the locked section of popTask() instead of missing the notification
  // and sleeping for the full THREAD_TIMEOUT.
  {
    std::lock_guard<std::mutex> autoLock(locker);
    exited = true;
    condition.notify_all();
  }
  std::thread* thread = nullptr;
  while (threads->try_dequeue(thread)) {
    ReleaseThread(thread);
  }
  totalThreads = 0;
  DEBUG_ASSERT(waitingThreads == 0)
  if (exit) {
    delete threads;
    for (auto& queue : priorityQueues) {
      delete queue;
    }
    priorityQueues.clear();
  } else {
    exited = false;
  }
}
}  // namespace tgfx
