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

#include "RunLoop.h"
#include "TaskGroup.h"

namespace tgfx {

RunLoop* RunLoop::Create() {
  return new RunLoop();
}

RunLoop::~RunLoop() {
  if (!thread) {
    return;
  }
  if (waitingWhileDealloc) {
    if (thread->joinable()) {
      thread->join();
    }
  } else {
    thread->detach();
  }
  delete thread;
}

void RunLoop::exit(bool wait) {
  waitingWhileDealloc = wait;
  exited = true;
}

bool RunLoop::start() {
  if (thread) {
    return true;
  }
  thread = new (std::nothrow) std::thread([this]() {
    while (!exited) {
      Execute();
    }
  });
  return thread != nullptr;
}

void RunLoop::Execute() {
  auto task = TaskGroup::GetInstance()->popTask();
  if (task == nullptr) {
    return;
  }
  task->execute();
}

}  // namespace tgfx
