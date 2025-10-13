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

#include "ReadPixelsTask.h"

namespace tgfx {
ReadPixelsTask::ReadPixelsTask(std::shared_ptr<RenderTargetProxy> source, const Rect& srcRect,
                               std::shared_ptr<GPUBufferProxy> dest)
    : source(std::move(source)), srcRect(srcRect), dest(std::move(dest)) {
}

void ReadPixelsTask::execute(CommandEncoder* encoder) {
  auto renderTarget = source->getRenderTarget();
  if (renderTarget == nullptr) {
    LOGE("ReadPixelsTask::execute() Failed to get the source render target!");
    return;
  }
  auto readbackBuffer = dest->getBuffer();
  if (readbackBuffer == nullptr) {
    LOGE("ReadPixelsTask::execute() Failed to get the dest readback buffer!");
    return;
  }
  encoder->copyTextureToBuffer(renderTarget->getSampleTexture(), srcRect,
                               readbackBuffer->gpuBuffer());
}
}  // namespace tgfx
