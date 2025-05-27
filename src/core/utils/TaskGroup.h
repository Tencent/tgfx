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

#pragma once

#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>
#include <vector>
#include "concurrentqueue.h"
#include "tgfx/core/Task.h"

namespace tgfx {

class TaskGroup {
 private:
  std::mutex locker = {};
  std::condition_variable condition = {};
  std::atomic_int totalThreads = 0;
  std::atomic_bool exited = false;
  std::atomic_int waitingThreads = 0;
  std::vector<moodycamel::ConcurrentQueue<std::shared_ptr<Task>>*> priorityQueues = {};
  moodycamel::ConcurrentQueue<std::thread*>* threads = nullptr;
  static TaskGroup* GetInstance();
  static void RunLoop(TaskGroup* taskGroup);

  TaskGroup();
  bool checkThreads();
  bool pushTask(std::shared_ptr<Task> task, TaskPriority priority);
  std::shared_ptr<Task> popTask();
  void exit();
  void releaseThreads(bool exit);

  friend class Task;
  friend class TaskThread;
  friend void OnAppExit();
};
}  // namespace tgfx
