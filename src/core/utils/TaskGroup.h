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

#pragma once

#include <atomic>
#include <thread>
#include "concurrentqueue.h"
#include "core/utils/TaskQueue.h"
#include "tgfx/core/Task.h"

namespace tgfx {

/**
 * TaskGroup manages a pool of worker threads that claim tasks from an internal TaskQueue. The
 * queue owns the scheduling counters and its own lock; this class only decides when to spawn,
 * shrink, and release workers.
 */
class TaskGroup {
 private:
  TaskQueue taskQueue = {};
  std::atomic_size_t totalThreads = 0;
  std::atomic_size_t maxThreads = 32;
  std::atomic_size_t lowPriorityThreads = 2;
  moodycamel::ConcurrentQueue<std::thread*>* threads = nullptr;

  static TaskGroup* GetInstance();
  static void RunLoop(TaskGroup* taskGroup);

  TaskGroup();
  bool spawnWorker();
  bool shrinkSelf();
  void setMaxThreadCount(size_t maxThreadCount);
  size_t maxThreadCount() const;
  bool pushTask(std::shared_ptr<Task> task, TaskPriority priority);
  void releaseThreads(bool exit);

  friend class Task;
  friend void OnAppExit();
};
}  // namespace tgfx
