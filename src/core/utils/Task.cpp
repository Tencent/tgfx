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

void Task::wait() {
  auto oldStatus = _status.load(std::memory_order_acquire);
  if (oldStatus == TaskStatus::Canceled || oldStatus == TaskStatus::Finished) {
    return;
  }
  // If wait() is called from the thread pool, all threads might block, leaving no thread to execute
  // this task. To avoid deadlock, execute the task directly on the current thread if it's queued.
  if (oldStatus == TaskStatus::Queueing) {
    if (_status.compare_exchange_strong(oldStatus, TaskStatus::Executing, std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
      onExecute();
      oldStatus = TaskStatus::Executing;
      while (!_status.compare_exchange_weak(oldStatus, TaskStatus::Finished,
                                            std::memory_order_acq_rel, std::memory_order_relaxed)) {
      }
      return;
    }
  }
  std::unique_lock<std::mutex> autoLock(locker);
  if (_status.load(std::memory_order_acquire) == TaskStatus::Executing) {
    condition.wait(autoLock);
  }
}

void Task::execute() {
  auto oldStatus = _status.load(std::memory_order_acquire);
  if (oldStatus == TaskStatus::Queueing &&
      _status.compare_exchange_strong(oldStatus, TaskStatus::Executing, std::memory_order_acq_rel,
                                      std::memory_order_relaxed)) {
    onExecute();
    oldStatus = TaskStatus::Executing;
    while (!_status.compare_exchange_weak(oldStatus, TaskStatus::Finished,
                                          std::memory_order_acq_rel, std::memory_order_relaxed)) {
    }
    std::unique_lock<std::mutex> autoLock(locker);
    condition.notify_all();
  }
}
}  // namespace tgfx
