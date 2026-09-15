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

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include "base/TGFXTest.h"
#include "core/utils/BlockAllocator.h"
#include "core/utils/TaskGroup.h"
#include "core/utils/UniqueID.h"
#include "gpu/RectsVertexProvider.h"
#include "gpu/resources/Resource.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Task.h"
#include "utils/TestUtils.h"

namespace tgfx {

// ==================== Task Tests ====================

TGFX_TEST(ResourceTest, TaskRelease) {
  Task::ReleaseThreads();
  TGFX_PRIVATE_ACCESS(auto group = TaskGroup::GetInstance(); std::thread* thead = nullptr;
                      group->threads->try_dequeue(thead); EXPECT_EQ(thead, nullptr);
                      EXPECT_EQ(group->waitingThreads, 0u); EXPECT_EQ(group->totalThreads, 0u);
                      for (auto& queue
                           : group->priorityQueues) {
                        std::shared_ptr<Task> task = nullptr;
                        queue->try_dequeue(task);
                        EXPECT_EQ(task, nullptr);
                      })
}

#ifdef TGFX_USE_THREADS
TGFX_TEST(ResourceTest, MaxThreadCountShrink) {
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
  TGFX_PRIVATE_ACCESS(auto group = TaskGroup::GetInstance(); EXPECT_EQ(group->totalThreads, 4u);
                      Task::SetMaxThreadCount(1);
                      for (int i = 0; i < 100 && group->totalThreads > 1u; ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                      } EXPECT_EQ(group->totalThreads, 1u););
  Task::SetMaxThreadCount(0);
}
#endif

class TaskWaitGate final : public Task {
 public:
  bool waitUntilStarted() {
    std::unique_lock<std::mutex> lock(mutex);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!started) {
      if (condition.wait_until(lock, deadline) == std::cv_status::timeout) {
        return started;
      }
    }
    return true;
  }

  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    condition.notify_all();
  }

  void onExecute() override {
    std::unique_lock<std::mutex> lock(mutex);
    started = true;
    condition.notify_all();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!released) {
      if (condition.wait_until(lock, deadline) == std::cv_status::timeout) {
        break;
      }
    }
  }

 private:
  std::mutex mutex = {};
  std::condition_variable condition = {};
  bool started = false;
  bool released = false;
};

class TaskWaitCaller {
 public:
  explicit TaskWaitCaller(std::shared_ptr<Task> task, uint64_t timeout = 0)
      : task(std::move(task)), timeout(timeout) {
  }

  ~TaskWaitCaller() {
    if (thread.joinable()) {
      thread.join();
    }
  }

  void start() {
    thread = std::thread(&TaskWaitCaller::run, this);
  }

  bool waitForResult(uint64_t timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!finished) {
      if (condition.wait_until(lock, deadline) == std::cv_status::timeout) {
        return finished;
      }
    }
    return true;
  }

  bool result() {
    std::lock_guard<std::mutex> lock(mutex);
    return succeeded;
  }

 private:
  void run() {
    auto result = task->wait(timeout);
    std::lock_guard<std::mutex> lock(mutex);
    succeeded = result;
    finished = true;
    condition.notify_all();
  }

  std::shared_ptr<Task> task = nullptr;
  uint64_t timeout = 0;
  std::thread thread = {};
  std::mutex mutex = {};
  std::condition_variable condition = {};
  bool succeeded = false;
  bool finished = false;
};

TGFX_TEST(ResourceTest, TaskWaitInlineNotifiesWaiters) {
  auto task = std::make_shared<TaskWaitGate>();
  TaskWaitCaller executor(task);
  executor.start();
  ASSERT_TRUE(task->waitUntilStarted());
  TaskWaitCaller first(task, 500);
  TaskWaitCaller second(task, 500);
  first.start();
  second.start();
  EXPECT_FALSE(first.waitForResult(20));
  EXPECT_FALSE(second.waitForResult(20));
  task->release();
  EXPECT_TRUE(executor.waitForResult(200));
  EXPECT_TRUE(first.waitForResult(200));
  EXPECT_TRUE(second.waitForResult(200));
  EXPECT_TRUE(first.result());
  EXPECT_TRUE(second.result());
  EXPECT_EQ(task->status(), TaskStatus::Finished);
}

TGFX_TEST_PRIVATE(ResourceTest, TaskWaitIgnoresNotifications) {
  auto task = std::make_shared<TaskWaitGate>();
  TaskWaitCaller executor(task);
  executor.start();
  ASSERT_TRUE(task->waitUntilStarted());
  TaskWaitCaller waiter(task, 500);
  waiter.start();
  for (int i = 0; i < 5; ++i) {
    TGFX_PRIVATE_ACCESS(task->condition.notify_all();)
    EXPECT_FALSE(waiter.waitForResult(10));
  }
  EXPECT_EQ(task->status(), TaskStatus::Executing);
  task->release();
  EXPECT_TRUE(waiter.waitForResult(200));
  EXPECT_TRUE(waiter.result());
}

TGFX_TEST_PRIVATE(ResourceTest, TaskWaitTimeoutUsesFixedDeadline) {
  auto task = std::make_shared<TaskWaitGate>();
  TaskWaitCaller executor(task);
  executor.start();
  ASSERT_TRUE(task->waitUntilStarted());
  TaskWaitCaller waiter(task, 80);
  waiter.start();
  EXPECT_FALSE(waiter.waitForResult(10));
  bool returned = false;
  for (int i = 0; i < 30 && !returned; ++i) {
    TGFX_PRIVATE_ACCESS(task->condition.notify_all();)
    returned = waiter.waitForResult(10);
  }
  EXPECT_TRUE(returned);
  EXPECT_FALSE(waiter.result());
  EXPECT_EQ(task->status(), TaskStatus::Executing);
  task->release();
  EXPECT_TRUE(task->wait());
}

TGFX_TEST(ResourceTest, TaskWaitQueuedAndCanceled) {
  auto queued = std::make_shared<TaskWaitGate>();
  queued->release();
  EXPECT_TRUE(queued->wait(1));
  EXPECT_EQ(queued->status(), TaskStatus::Finished);
  auto canceled = std::make_shared<TaskWaitGate>();
  canceled->cancel();
  EXPECT_TRUE(canceled->wait());
  EXPECT_EQ(canceled->status(), TaskStatus::Canceled);
}

#ifdef TGFX_USE_THREADS
TGFX_TEST(ResourceTest, TaskWaitWorkerCompletion) {
  auto task = std::make_shared<TaskWaitGate>();
  Task::Run(task);
  ASSERT_TRUE(task->waitUntilStarted());
  TaskWaitCaller waiter(task, 500);
  waiter.start();
  EXPECT_FALSE(waiter.waitForResult(20));
  task->release();
  EXPECT_TRUE(waiter.waitForResult(200));
  EXPECT_TRUE(waiter.result());
  EXPECT_TRUE(task->wait(std::numeric_limits<uint64_t>::max()));
}
#endif

// ==================== Resource Cache Tests ====================

class TestResource : public Resource {
 public:
  static std::shared_ptr<const TestResource> Make(Context* context, uint32_t id) {
    static const uint32_t TestResourceType = UniqueID::Next();
    BytesKey bytesKey = {};
    bytesKey.write(TestResourceType);
    bytesKey.write(id);
    return Resource::AddToCache(context, new TestResource(), bytesKey);
  }

  size_t memoryUsage() const override {
    return 1;
  }
};

TGFX_TEST(ResourceTest, MultiThreadRecycling) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  tgfx::Task::Run([device] {
    for (uint32_t i = 0; i < 100; ++i) {
      auto context = device->lockContext();
      ASSERT_TRUE(context != nullptr);
      auto resource = TestResource::Make(context, i);
      context->flushAndSubmit();
      context->resourceCache()->purgeUntilMemoryTo(0);
      device->unlock();
      tgfx::Task::Run([resource, device] {
        resource.get();
        device.get();
      });
    }
  });
};

#ifdef TGFX_USE_THREADS
TGFX_TEST(ResourceTest, BlockAllocatorRefCount) {
  BlockAllocator blockAllocator;
  // make sure vertices expire after task is done
  float* vertices = nullptr;
  {
    auto vertexProvider =
        RectsVertexProvider::MakeFrom(&blockAllocator, Rect::MakeWH(100, 100), AAType::Coverage);
    vertices = new float[vertexProvider->vertexCount()];
    auto task = std::make_shared<VertexProviderTask>(std::move(vertexProvider), vertices);
    Task::Run(task);
  }
  blockAllocator.clear();
  delete[] vertices;
}
#endif

}  // namespace tgfx
