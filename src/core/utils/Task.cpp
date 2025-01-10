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

#include "tgfx/core/Task.h"
#include "core/utils/TaskGroup.h"

namespace tgfx {
std::shared_ptr<Task> Task::Run(std::function<void()> block) {
  if (block == nullptr) {
    return nullptr;
  }
  auto task = std::shared_ptr<Task>(new Task(std::move(block)));
  if (!TaskGroup::GetInstance()->pushTask(task)) {
    task->execute();
  }
  return task;
}

Task::Task(std::function<void()> block) : block(std::move(block)) {
}

bool Task::executing() {
  return state == TaskState::Executing;
}

bool Task::cancelled() {
  return state == TaskState::Canceled;
}

bool Task::finished() {
  return state == TaskState::Finished;
}

void Task::wait() {
  std::unique_lock<std::mutex> autoLock(locker);
  if (state == TaskState::Finished || state == TaskState::Canceled) {
    return;
  }
  // Try to remove the task from the queue. Execute it directly on the current thread if the task is
  // not in the queue. This is to avoid the deadlock situation.
  if (state == TaskState::Queued) {
    state = TaskState::Executing;
    block();
    state = TaskState::Finished;
    condition.notify_all();
    return;
  }
  condition.wait(autoLock);
}

void Task::cancel() {
  if (state == TaskState::Queued) {
    state = TaskState::Canceled;
  }
}

void Task::execute() {
  state = TaskState::Executing;
  block();
  state = TaskState::Finished;
  condition.notify_all();
}
}  // namespace tgfx
