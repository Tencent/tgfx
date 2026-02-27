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

#pragma once

#include "RenderTask.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
class RenderTargetCopyTask : public RenderTask {
 public:
  RenderTargetCopyTask(BlockAllocator* allocator, std::shared_ptr<RenderTargetProxy> source,
                       std::shared_ptr<TextureProxy> dest, int srcX, int srcY);

  void execute(CommandEncoder* encoder) override;

 private:
  std::shared_ptr<RenderTargetProxy> source = nullptr;
  std::shared_ptr<TextureProxy> dest = nullptr;
  int srcX = 0;
  int srcY = 0;
};
}  // namespace tgfx
