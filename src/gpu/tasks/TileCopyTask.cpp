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

#include "TileCopyTask.h"
#include "core/utils/Log.h"
#include "inspect/InspectorMark.h"

namespace tgfx {
TileCopyTask::TileCopyTask(BlockAllocator* allocator, std::shared_ptr<RenderTargetProxy> source,
                           std::shared_ptr<RenderTargetProxy> dest, int tileSize, int dstX,
                           int dstY)
    : RenderTask(allocator), source(std::move(source)), dest(std::move(dest)), tileSize(tileSize),
      dstX(dstX), dstY(dstY) {
}

void TileCopyTask::execute(CommandEncoder* encoder) {
  TASK_MARK(tgfx::inspect::OpTaskType::TileCopyTask);
  auto srcRenderTarget = source->getRenderTarget();
  if (srcRenderTarget == nullptr) {
    LOGE("TileCopyTask::execute() Failed to get the source render target!");
    return;
  }
  auto dstRenderTarget = dest->getRenderTarget();
  if (dstRenderTarget == nullptr) {
    LOGE("TileCopyTask::execute() Failed to get the dest render target!");
    return;
  }
  auto srcRect = Rect::MakeWH(tileSize, tileSize);
  auto dstOffset = Point::Make(static_cast<float>(dstX), static_cast<float>(dstY));
  encoder->blitRenderTarget(srcRenderTarget.get(), srcRect, dstRenderTarget.get(), dstOffset);
}

}  // namespace tgfx
