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

#include "LockFreeQueue.h"
#include "core/utils/TaskWorkerThread.h"
#include "tgfx/core/Task.h"

namespace tgfx {
class TaskGroup {
 private:
  std::atomic_int totalThreads = 0;
  std::atomic_bool exited = false;
  LockFreeQueue<std::shared_ptr<Task>>* tasks = nullptr;
  LockFreeQueue<TaskWorkerThread*>* threads = nullptr;
  static TaskGroup* GetInstance();

  TaskGroup();
  bool checkThreads();
  bool pushTask(std::shared_ptr<Task> task);
  std::shared_ptr<Task> popTask();

  void exit();
  void releaseThreads();

  void releaseThreadsInternal(bool wait);

  friend class Task;
  friend class TaskWorkerThread;
  friend void OnAppExit();
};
}  // namespace tgfx
