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
#include <utility>
#include "core/utils/BlockBuffer.h"
#include "core/utils/UniqueID.h"
#include "gpu/RectsVertexProvider.h"
#include "gpu/resources/Resource.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Task.h"
#include "utils/TestUtils.h"

namespace tgfx {
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

TGFX_TEST(ResourceCacheTest, multiThreadRecycling) {
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
TGFX_TEST(ResourceCacheTest, blockBufferRefCount) {
  BlockBuffer blockBuffer;
  {
    auto vertexProvider =
        RectsVertexProvider::MakeFrom(&blockBuffer, Rect::MakeWH(100, 100), AAType::Coverage);
    std::array<float, 16> vertices;
    auto task = std::make_shared<VertexProviderTask>(std::move(vertexProvider), vertices.data());
    Task::Run(task);
  }
  blockBuffer.clear();
}
#endif

}  // namespace tgfx
