/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include <vector>
#include "base/TGFXTest.h"
#include "core/utils/TaskGroup.h"
#include "tgfx/core/Task.h"

namespace tgfx {
TGFX_TEST(TaskTest, release) {
  Task::ReleaseThreads();
  auto group = TaskGroup::GetInstance();
  std::thread* thead = nullptr;
  group->threads->try_dequeue(thead);
  EXPECT_EQ(thead, nullptr);
  EXPECT_EQ(group->waitingThreads, 0);
  EXPECT_EQ(group->totalThreads, 0);
  for (auto& queue : group->priorityQueues) {
    std::shared_ptr<Task> task = nullptr;
    queue->try_dequeue(task);
    EXPECT_EQ(task, nullptr);
  }
}
}  // namespace tgfx
