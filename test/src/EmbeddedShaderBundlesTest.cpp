/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <utility>
#include "gpu/aot/bundle/EmbeddedShaderBundles.h"
#include "gtest/gtest.h"

namespace tgfx {

namespace {

// The payload the test registers; its address is what GetBundle must hand back.
const uint8_t TestPayload[] = {0x54, 0x47, 0x46, 0x58};

// The bundle table is process-wide, so a test that registers into it must hand the slot back:
// a leftover payload would reach the next Context as an embedded bundle and fail its parse.
class SlotRestorer {
 public:
  explicit SlotRestorer(Backend backend) : backend(backend) {
    saved = EmbeddedShaderBundles::GetBundle(backend);
  }

  ~SlotRestorer() {
    EmbeddedShaderBundles::Register(backend, saved.first, saved.second);
    saved = {};
  }

  SlotRestorer(const SlotRestorer&) = delete;
  SlotRestorer& operator=(const SlotRestorer&) = delete;

 private:
  Backend backend = Backend::Unknown;
  std::pair<const uint8_t*, size_t> saved = {};
};

}  // namespace

TEST(EmbeddedShaderBundlesTest, EveryBackendHasASlot) {
  for (int value = 0; value <= static_cast<int>(Backend::D3D12); ++value) {
    auto backend = static_cast<Backend>(value);
    SlotRestorer restorer(backend);
    EmbeddedShaderBundles::Register(backend, TestPayload, sizeof(TestPayload));
    auto bundle = EmbeddedShaderBundles::GetBundle(backend);
    EXPECT_EQ(bundle.first, TestPayload) << "backend value " << value << " has no bundle slot";
    EXPECT_EQ(bundle.second, sizeof(TestPayload)) << "backend value " << value;
  }
}

TEST(EmbeddedShaderBundlesTest, AnUnknownBackendReceivesNothing) {
  auto unknown = static_cast<Backend>(static_cast<int>(Backend::D3D12) + 1);
  EmbeddedShaderBundles::Register(unknown, TestPayload, sizeof(TestPayload));
  auto bundle = EmbeddedShaderBundles::GetBundle(unknown);
  EXPECT_EQ(bundle.first, nullptr);
  EXPECT_EQ(bundle.second, 0u);
}

}  // namespace tgfx
