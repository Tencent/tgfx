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
#include <list>
#include <mutex>
#include <thread>
#include "core/utils/Log.h"

namespace tgfx {

class Thread {
 public:
  enum class Priority { Lowest, Low, Normal, High, Highest };
  static Thread* Create(std::function<void()> task, Priority priority = Priority::Normal);
  virtual ~Thread() = default;

  void start() {
    if (joinable()) {
      return;
    }
    onStart();
  }

  void join() {
    if (joinable()) {
      onJoin();
    }
  }

 protected:
  Thread(std::function<void()> task, Priority priority)
      : task(std::move(task)), priority(priority) {
  }

  virtual void onStart() = 0;

  virtual bool joinable() const = 0;

  virtual void onJoin() = 0;

  std::function<void()> task;
  Priority priority = Priority::Normal;
};

}  // namespace tgfx
