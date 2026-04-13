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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RenderTask.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
/**
 * A RenderTask that copies a tile-sized region from a source render target to a destination
 * render target at a specified offset. Used for MSAA tile optimization in tiled rendering mode.
 */
class TileCopyTask : public RenderTask {
 public:
  TileCopyTask(BlockAllocator* allocator, std::shared_ptr<RenderTargetProxy> source,
               std::shared_ptr<RenderTargetProxy> dest, int tileSize, int dstX, int dstY);

  void execute(CommandEncoder* encoder) override;

 private:
  std::shared_ptr<RenderTargetProxy> source = nullptr;
  std::shared_ptr<RenderTargetProxy> dest = nullptr;
  int tileSize = 0;
  int dstX = 0;
  int dstY = 0;
};
}  // namespace tgfx
