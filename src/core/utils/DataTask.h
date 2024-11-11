/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/Task.h"

namespace tgfx {
/**
 * The DataTask class handles running a generator function concurrently, which produces a value of
 * type T. The generator function runs asynchronously, and its result is stored in a holder object.
 */
template <typename T>
class DataTask {
 public:
  struct Holder {
    std::shared_ptr<T> data;
  };

  /**
   * Schedules an asynchronous task to run the generator function immediately and store the result
   * in the holder.
   */
  static std::shared_ptr<DataTask<T>> Run(std::function<std::shared_ptr<T>()> generator) {
    return std::shared_ptr<DataTask<T>>(new DataTask<T>(std::move(generator)));
  }

  ~DataTask() {
    task->cancel();
  }

  /**
   * Waits for the generator function to finish executing and returns the result. After this method
   * is called, the task is considered finished and cannot be restarted.
   */
  std::shared_ptr<T> wait() const {
    task->wait();
    return holder->data;
  }

 private:
  std::shared_ptr<Holder> holder = std::make_shared<Holder>();
  std::shared_ptr<Task> task = nullptr;

  explicit DataTask(std::function<std::shared_ptr<T>()> generator) {
    task = Task::Run(
        [holder = holder, generator = std::move(generator)]() { holder->data = generator(); });
  }
};
}  // namespace tgfx
