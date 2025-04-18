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
#include <thread>
#include <vector>
#include "TaskWorkerThread.h"
#include "core/utils/Log.h"

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace tgfx {
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

TaskGroup* TaskGroup::GetInstance() {
  static auto& taskGroup = *new TaskGroup();
  return &taskGroup;
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
  if (!TaskWorkerThread::HasWaitingThread() && totalThreads < MaxThreads) {
    auto thread = TaskWorkerThread::Create();
    if (thread->start()) {
      threads->enqueue(thread);
      totalThreads++;
    } else {
      delete thread;
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
  TaskWorkerThread::NotifyNewTask();
  return true;
}

std::shared_ptr<Task> TaskGroup::popTask() {
  return tasks->dequeue();
}

void TaskGroup::exit() {
  exited = true;
  releaseThreadsInternal(true);
  delete tasks;
  delete threads;
}

void TaskGroup::releaseThreads() {
  if (exited) {
    return;
  }
  // stop accepting new tasks before clearing threads
  exited = true;
  releaseThreadsInternal(false);
  // continue to accept new tasks
  exited = false;
}

void TaskGroup::releaseThreadsInternal(bool wait) {
  TaskWorkerThread* thread = nullptr;
  std::vector<TaskWorkerThread*> threadsToDelete;
  while ((thread = threads->dequeue()) != nullptr) {
    if (wait) {
      thread->exit();
      threadsToDelete.push_back(thread);
    } else {
      // Note: Immediate deletion here would cause a crash becasue the threadProc will use the
      // thread object.
      // Threads in idle state will self-destruct after completing total tasks.
      thread->exitWhileIdle();
    }
  }
  totalThreads = 0;
  TaskWorkerThread::NotifyExit();
  for (auto thread : threadsToDelete) {
    delete thread;
  }
}
}  // namespace tgfx
