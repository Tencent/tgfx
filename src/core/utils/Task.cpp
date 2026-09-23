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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "tgfx/core/Task.h"
#include <chrono>
#include "core/utils/TaskGroup.h"

namespace tgfx {
class BlockTask : public Task {
 public:
  explicit BlockTask(std::function<void()> block) : block(std::move(block)) {
  }

 protected:
  void onExecute() override {
    block();
  }

 private:
  std::function<void()> block;
};

void Task::SetMaxThreadCount(size_t maxThreadCount) {
  TaskGroup::GetInstance()->setMaxThreadCount(maxThreadCount);
}

size_t Task::MaxThreadCount() {
  return TaskGroup::GetInstance()->maxThreadCount();
}

void Task::ReleaseThreads() {
  TaskGroup::GetInstance()->releaseThreads(false);
}

std::shared_ptr<Task> Task::Run(std::function<void()> block, TaskPriority priority) {
  if (block == nullptr) {
    return nullptr;
  }
  auto task = std::make_shared<BlockTask>(std::move(block));
  Run(task, priority);
  return task;
}

void Task::Run(std::shared_ptr<Task> task, TaskPriority priority) {
  if (task == nullptr) {
    return;
  }
  if (!TaskGroup::GetInstance()->pushTask(task, priority)) {
    task->execute();
  }
}

void Task::cancel() {
  auto currentStatus = _status.load(std::memory_order_acquire);
  if (currentStatus == TaskStatus::Queueing) {
    if (_status.compare_exchange_strong(currentStatus, TaskStatus::Canceled,
                                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
      onCancel();
    }
  }
}

bool Task::wait(uint64_t timeout) {
  // A queued task can run on the calling thread. Use the same completion notification as workers
  // so other callers waiting for this task are also released.
  execute();
  std::unique_lock<std::mutex> autoLock(locker);
  if (timeout == 0) {
    while (_status.load(std::memory_order_acquire) == TaskStatus::Executing) {
      condition.wait(autoLock);
    }
    return true;
  }
  auto now = std::chrono::steady_clock::now();
  auto maxDeadline = std::chrono::steady_clock::time_point::max();
  auto maxTimeout =
      std::chrono::duration_cast<std::chrono::milliseconds>(maxDeadline - now).count();
  auto deadline = timeout >= static_cast<uint64_t>(maxTimeout)
                      ? maxDeadline
                      : now + std::chrono::milliseconds(timeout);
  while (_status.load(std::memory_order_acquire) == TaskStatus::Executing) {
    if (condition.wait_until(autoLock, deadline) == std::cv_status::timeout) {
      return _status.load(std::memory_order_acquire) != TaskStatus::Executing;
    }
  }
  return true;
}

void Task::execute() {
  auto oldStatus = TaskStatus::Queueing;
  if (!_status.compare_exchange_strong(oldStatus, TaskStatus::Executing, std::memory_order_acq_rel,
                                       std::memory_order_relaxed)) {
    return;
  }
  onExecute();
  {
    std::lock_guard<std::mutex> autoLock(locker);
    _status.store(TaskStatus::Finished, std::memory_order_release);
  }
  condition.notify_all();
}
}  // namespace tgfx
