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

#pragma once

#include <cstdint>
#include "RenderTask.h"
#include "gpu/proxies/TextureProxy.h"

namespace tgfx {
class GenerateMipmapsTask : public RenderTask {
 public:
  /**
   * Per-frame profiling counters accumulated by GenerateMipmapsTask. Fetched and reset from
   * DrawingBuffer::encode() when emitting slow-frame diagnostics.
   */
  struct ProfileSnapshot {
    int64_t totalUs = 0;
    uint32_t count = 0;
  };

  /**
   * Returns the accumulated profiling counters and resets them to zero.
   */
  static ProfileSnapshot FetchProfileAndReset();

  GenerateMipmapsTask(BlockAllocator* allocator, std::shared_ptr<TextureProxy> textureProxy);

  void execute(CommandEncoder* encoder) override;

 private:
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
};
}  // namespace tgfx
