/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include <string>
#include "core/utils/Log.h"

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace tgfx {
static constexpr auto THREAD_TIMEOUT = std::chrono::seconds(10);
static constexpr uint32_t THREAD_POOL_SIZE = 32;
static constexpr uint32_t TASK_QUEUE_SIZE = 4096;

int GetCPUCores() {
  int cpuCores = 0;
#ifdef __APPLE__
  size_t len = sizeof(cpuCores);
  // We can get the exact number of physical CPUs on apple platforms.
  sysctlbyname("hw.physicalcpu", &cpuCores, &len, nullptr, 0);
#else
  cpuCores = static_cast<int>(std::thread::hardware_concurrency());
#endif
  if (cpuCores <= 0) {
    cpuCores = 8;
  }
  return cpuCores;
}

TaskWorkerThread::~TaskWorkerThread() {
  if (thread) {
    if (_exitWhileIdle) {
      thread->detach();
    } else if (thread->joinable()) {
      thread->join();
    }
    delete thread;
  }
  --TaskGroup::GetInstance()->totalThreads;
}

bool TaskWorkerThread::start() {
  if (thread) {
    return true;
  }
  thread = new (std::nothrow) std::thread(TaskGroup::RunLoop, this);
  return thread != nullptr;
}

TaskGroup* TaskGroup::GetInstance() {
  static auto& taskGroup = *new TaskGroup();
  return &taskGroup;
}

void TaskGroup::RunLoop(TaskWorkerThread* thread) {
  auto taskGroup = TaskGroup::GetInstance();
  while (!taskGroup->exited) {
    auto task = taskGroup->popTask();
    if (task == nullptr) {
      if (thread->_exitWhileIdle) {
        // TaskGroup no longer manages threads marked with exitWhileIdle,
        // so the thread must self-destruct here
        delete thread;
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

TaskGroup::TaskGroup() {
  threads = new LockFreeQueue<TaskWorkerThread*>(THREAD_POOL_SIZE);
  tasks = new LockFreeQueue<std::shared_ptr<Task>>(TASK_QUEUE_SIZE);
  std::atexit(OnAppExit);
}

bool TaskGroup::checkThreads() {
  static const int CPUCores = GetCPUCores();
  static const int MaxThreads = CPUCores > 16 ? 16 : CPUCores;
  if (waitingThreads == 0 && totalThreads < MaxThreads) {
    auto thread = new TaskWorkerThread();
    if (thread->start()) {
      threads->enqueue(thread);
      ++totalThreads;
    }
  } else {
    return true;
  }
  return totalThreads > 0;
}

bool TaskGroup::pushTask(std::shared_ptr<Task> task) {
#ifndef TGFX_USE_THREADS
  return false;
#endif
  if (exited || !checkThreads()) {
    return false;
  }
  if (!tasks->enqueue(std::move(task))) {
    return false;
  }
  if (waitingThreads > 0) {
    condition.notify_one();
  }
  return true;
}

std::shared_ptr<Task> TaskGroup::popTask() {
  std::unique_lock<std::mutex> autoLock(locker);
  while (!exited) {
    auto task = tasks->dequeue();
    if (task) {
      return task;
    }
    ++waitingThreads;
    auto status = condition.wait_for(autoLock, THREAD_TIMEOUT);
    --waitingThreads;
    if (exited || status == std::cv_status::timeout) {
      return nullptr;
    }
  }
  return nullptr;
}

void TaskGroup::exit() {
  exited = true;
  condition.notify_all();
  TaskWorkerThread* thread = nullptr;
  while ((thread = threads->dequeue()) != nullptr) {
    delete thread;
  }
  delete threads;
  delete tasks;
  DEBUG_ASSERT(totalThreads == 0);
  DEBUG_ASSERT(waitingThreads == 0);
}

void TaskGroup::releaseThreads() {
  TaskWorkerThread* thread = nullptr;
  while ((thread = threads->dequeue()) != nullptr) {
    thread->_exitWhileIdle = true;
  }
}
}  // namespace tgfx
