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

#include <atomic>
#include <chrono>
#include <thread>
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
  EXPECT_EQ(group->waitingThreads, 0u);
  EXPECT_EQ(group->totalThreads, 0u);
  for (auto& queue : group->priorityQueues) {
    std::shared_ptr<Task> task = nullptr;
    queue->try_dequeue(task);
    EXPECT_EQ(task, nullptr);
  }
}

#ifdef TGFX_USE_THREADS
TGFX_TEST(TaskTest, MaxThreadCountShrink) {
  Task::ReleaseThreads();
  Task::SetMaxThreadCount(4);
  EXPECT_EQ(Task::MaxThreadCount(), 4u);
  std::atomic<int> started{0};
  std::atomic<int> finished{0};
  std::atomic_bool release{false};
  auto blockTask = [&started, &finished, &release] {
    ++started;
    while (!release.load()) {
    }
    ++finished;
  };
  Task::Run(blockTask);
  // Wait for the first worker thread to start, then submit more tasks. All existing threads are
  // busy with the blocking tasks, so each submission guarantees a new worker thread is created.
  while (started.load() < 1) {
  }
  for (int i = 0; i < 3; ++i) {
    Task::Run(blockTask);
  }
  while (started.load() < 4) {
  }
  release = true;
  while (finished.load() < 4) {
  }
  // Give the threads a moment to become idle before lowering the limit.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  auto group = TaskGroup::GetInstance();
  EXPECT_EQ(group->totalThreads, 4u);
  Task::SetMaxThreadCount(1);
  for (int i = 0; i < 100 && group->totalThreads > 1u; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_EQ(group->totalThreads, 1u);
  Task::SetMaxThreadCount(0);
}
#endif
}  // namespace tgfx
