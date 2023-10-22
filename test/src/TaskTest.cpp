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

#include <thread>
#include "tgfx/utils/Task.h"
#include "utils/Log.h"
#include "utils/TestUtils.h"

namespace tgfx {

GTEST_TEST(TaskTest, Task) {
  std::vector<std::shared_ptr<Task>> tasks = {};
  for (int i = 0; i < 17; i++) {
    auto task = Task::Run([=] {
      LOGI("Task %d is executing...", i);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      LOGI("Task %d is finished", i);
    });
    tasks.push_back(task);
  }
  // Wait a little moment for the tasks to be executed.
  std::this_thread::yield();
  auto task = tasks[0];
  EXPECT_TRUE(task->executing());
  task->cancel();
  EXPECT_FALSE(task->cancelled());
  task = tasks[16];
  EXPECT_TRUE(task->executing());
  task->cancel();
  EXPECT_TRUE(task->cancelled());
  for (auto& item : tasks) {
    item->wait();
    EXPECT_FALSE(item->executing());
  }
  task = tasks[0];
  EXPECT_TRUE(task->finished());
  task = tasks[16];
  EXPECT_FALSE(task->finished());
  tasks = {};
}
}  // namespace tgfx
