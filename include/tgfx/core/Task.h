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
   * Release all task threads once the pending tasks have completed. This method will block the
   * current thread.
   */
  static void ReleaseThreads();

  /**
   * Submits a code block for asynchronous execution immediately and returns a Task wraps the code
   * block. Hold a reference to the returned Task if you want to cancel it or wait for it to finish
   * execution. Returns nullptr if the block is nullptr.
   */
  static std::shared_ptr<Task> Run(std::function<void()> block);

  /**
   * Submits a Task for asynchronous execution immediately. Hold a reference to the Task if you want
   * to cancel it or wait for it to finish execution. Does nothing if the Task is nullptr.
   */
  static void Run(std::shared_ptr<Task> task);

  virtual ~Task() = default;

  /**
   * Return the current status of the Task.
   */
  TaskStatus status() const {
    return _status.load(std::memory_order_acquire);
  }

  /**
   * Requests the Task to skip executing its Runnable object. Cancellation does not affect the
   * execution of a Task that has already begun. This method does not block the current thread.
   */
  void cancel();

  /**
   * Blocks the current thread until the Task finishes its execution. Returns immediately if the
   * Task is finished or canceled. The task may be executed on the calling thread if it is not
   * canceled and still in the queue.
   */
  void wait();

  /**
   * Cancels the Task if it is still in the queue, otherwise waits for the Task to finish its
   * execution. Returns immediately if the Task is finished or canceled, otherwise blocks the
   * current thread.
   */
  void cancelOrWait();

 protected:
  virtual void onExecute() = 0;

 private:
  std::mutex locker = {};
  std::condition_variable condition = {};
  std::atomic<TaskStatus> _status = TaskStatus::Queueing;

  void execute();

  friend class TaskGroup;
};

}  // namespace tgfx
