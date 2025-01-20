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

bool Task::waiting() {
  return status.load(std::memory_order_relaxed) == TaskStatus::waiting;
}

bool Task::executing() {
  return status.load(std::memory_order_relaxed) == TaskStatus::executing;
}

bool Task::cancelled() {
  return status.load(std::memory_order_relaxed) == TaskStatus::cancelled;
}

bool Task::finished() {
  return status.load(std::memory_order_relaxed) == TaskStatus::finished;
}

void Task::wait() {
  auto oldStatus = status.load(std::memory_order_relaxed);
  if (oldStatus == TaskStatus::cancelled || oldStatus == TaskStatus::finished) {
    return;
  }
  if (oldStatus == TaskStatus::waiting) {
    if (status.compare_exchange_weak(oldStatus, TaskStatus::executing, std::memory_order_acq_rel,
                                     std::memory_order_relaxed)) {
      block();
      status.store(TaskStatus::finished, std::memory_order_relaxed);
      return;
    }
  }
  std::unique_lock<std::mutex> autoLock(locker);
  if (status.load(std::memory_order_acquire) == TaskStatus::executing) {
    condition.wait(autoLock);
  }
}

void Task::cancel() {
  auto currentStatus = status.load(std::memory_order_relaxed);
  if (currentStatus == TaskStatus::waiting) {
    status.store(TaskStatus::cancelled, std::memory_order_relaxed);
  }
}

void Task::execute() {
  auto oldStatus = status.load(std::memory_order_relaxed);
  if (oldStatus == TaskStatus::waiting &&
      status.compare_exchange_weak(oldStatus, TaskStatus::executing, std::memory_order_acq_rel,
                                   std::memory_order_relaxed)) {
    block();
    status.store(TaskStatus::finished, std::memory_order_relaxed);
    std::unique_lock<std::mutex> autoLock(locker);
    condition.notify_all();
  }
}
}  // namespace tgfx
