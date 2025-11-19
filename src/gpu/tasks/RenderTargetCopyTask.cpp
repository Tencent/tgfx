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

#include "RenderTargetCopyTask.h"
#include "inspect/InspectorMark.h"

namespace tgfx {
RenderTargetCopyTask::RenderTargetCopyTask(BlockAllocator* allocator,
                                           std::shared_ptr<RenderTargetProxy> source,
                                           std::shared_ptr<TextureProxy> dest, int srcX, int srcY)
    : RenderTask(allocator), source(std::move(source)), dest(std::move(dest)), srcX(srcX),
      srcY(srcY) {
}

void RenderTargetCopyTask::execute(CommandEncoder* encoder) {
  TASK_MARK(tgfx::inspect::OpTaskType::RenderTargetCopyTask);
  auto renderTarget = source->getRenderTarget();
  if (renderTarget == nullptr) {
    LOGE("RenderTargetCopyTask::execute() Failed to get the source render target!");
    return;
  }
  auto textureView = dest->getTextureView();
  if (textureView == nullptr) {
    LOGE("RenderTargetCopyTask::execute() Failed to get the dest texture view!");
    return;
  }
  auto srcRect = Rect::MakeXYWH(srcX, srcY, textureView->width(), textureView->height());
  encoder->copyTextureToTexture(renderTarget->getSampleTexture(), srcRect,
                                textureView->getTexture(), Point::Zero());
  encoder->generateMipmapsForTexture(textureView->getTexture());
}

}  // namespace tgfx
