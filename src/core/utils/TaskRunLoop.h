/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
#include <thread>
#include "tgfx/core/Task.h"

namespace tgfx {
class TaskRunLoop {
 public:
  static void NotifyNewTask();

  static void NotifyExit();

  static bool HasWaitingRunLoop();

  static TaskRunLoop* Create();

  virtual ~TaskRunLoop();

  bool start();

  void exit();

  void exitWhileIdle();

 private:
  static void ThreadProc(TaskRunLoop* runLoop);
  std::thread* thread = nullptr;
  std::atomic_bool exited = false;
  std::atomic_bool _exitWhileIdle = false;
};
}  // namespace tgfx
