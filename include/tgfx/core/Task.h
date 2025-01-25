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

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace tgfx {
class TaskGroup;

/**
 * Defines the possible states of a Task.
 */
enum class TaskStatus {
  /**
   * The Task is waiting to be executed.
   */
  Queueing,
  /**
   * The Task is currently executing.
   */
  Executing,
  /**
   * The Task has finished executing.
   */
  Finished,
  /**
   * The Task has been canceled.
   */
  Canceled
};

/**
 * The Task class manages the concurrent execution of one or more code blocks.
 */
class Task {
 public:
  /**
   * Submits a code block for asynchronous execution immediately and returns a Task wraps the code
   * block. Hold a reference to the returned Task if you want to cancel it or wait for it to finish
   * execution. Returns nullptr if the block is nullptr.
   */
  static std::shared_ptr<Task> Run(std::function<void()> block);

  /**
   * Advises the Task that it should stop executing its code block. Cancellation does not affect the
   * execution of a Task that has already begun.
   */
  void cancel();

  /**
   * Blocks the current thread until the Task finishes its execution. Returns immediately if the
   * Task is finished or canceled. The task may be executed on the calling thread if it is not
   * canceled and still in the queue.
   */
  void wait();

  /**
   * Return the current status of the Task.
   */
  TaskStatus status() const;

 private:
  std::mutex locker = {};
  std::condition_variable condition = {};
  std::function<void()> block = nullptr;
  std::atomic<TaskStatus> _status = TaskStatus::Queueing;

  explicit Task(std::function<void()> block);
  void execute();

  friend class TaskGroup;
};

}  // namespace tgfx
