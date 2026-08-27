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
