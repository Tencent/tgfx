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
static constexpr int MAX_TASK_COUNT = 100;

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

static const int CPUCores = GetCPUCores();
static const int MaxThreads = CPUCores > 16 ? 16 : CPUCores;

TaskGroup* TaskGroup::GetInstance() {
  static auto& taskGroup = *new TaskGroup();
  return &taskGroup;
}

void TaskGroup::RunLoop(TaskGroup* taskGroup) {
  while (true) {
    auto task = taskGroup->popTask();
    if (task == nullptr) {
      continue;
    }
    if (task->state == TaskState::Queued) {
      task->execute();
    }
  }
}

static void ReleaseThread(std::thread* thread) {
  if (thread->joinable()) {
    thread->join();
  }
  delete thread;
}

void OnAppExit() {
  // Forces all pending tasks to be finished when the app is exiting to prevent accessing wild
  // pointers.
  TaskGroup::GetInstance()->exit();
}

TaskGroup::TaskGroup() {
  std::atexit(OnAppExit);
}

bool TaskGroup::checkThreads() {
  auto totalThreads = static_cast<int>(threads.size());
  if (activeThreads < totalThreads || totalThreads >= MaxThreads) {
    return true;
  }
  auto thread = new (std::nothrow) std::thread(&TaskGroup::RunLoop, this);
  if (thread) {
    activeThreads++;
    threads.enqueue(thread);
    //    LOGI("TaskGroup: A task thread is created, the current number of threads : %lld",
    //         threads.size());
  }
  return !threads.empty();
}

bool TaskGroup::pushTask(std::shared_ptr<Task> task) {
#if defined(TGFX_BUILD_FOR_WEB) && !defined(__EMSCRIPTEN_PTHREADS__)
  return false;
#endif
  if (exited || !checkThreads()) {
    return false;
  }
  if (tasks.size() >= MAX_TASK_COUNT) {
    return false;
  }
  tasks.enqueue(std::move(task));
  if (waitDataCount > 0) {
    condition.notify_one();
  }
  return true;
}

std::shared_ptr<Task> TaskGroup::popTask() {
  std::unique_lock<std::mutex> autoLock(locker);
  while (!exited) {
    if (tasks.empty()) {
      waitDataCount++;
      auto status = condition.wait_for(autoLock, THREAD_TIMEOUT);
      waitDataCount--;
      if (exited || status == std::cv_status::timeout) {
        return nullptr;
      }
    } else {
      std::shared_ptr<Task> task = tasks.dequeue();
      while (task && task->state != TaskState::Queued) {
        task = tasks.dequeue();
      }
      return task;
    }
  }
  return nullptr;
}

void TaskGroup::exit() {
  exited = true;
  tasks.clear();
  condition.notify_all();
  auto thread = threads.dequeue();
  while (thread != nullptr) {
    ReleaseThread(thread);
    thread = threads.dequeue();
  }
}
}  // namespace tgfx
