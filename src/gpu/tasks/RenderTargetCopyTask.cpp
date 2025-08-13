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

namespace tgfx {
RenderTargetCopyTask::RenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                                           std::shared_ptr<TextureProxy> dest, int srcX, int srcY)
    : source(std::move(source)), dest(std::move(dest)), srcX(srcX), srcY(srcY) {
}

void RenderTargetCopyTask::execute(CommandEncoder* encoder) {
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
  auto srcOffset = Point::Make(srcX, srcY);
  auto dstRect = Rect::MakeWH(textureView->width(), textureView->height());
  encoder->copyFrameBufferToTexture(renderTarget->getFrameBuffer(), srcOffset,
                                    textureView->getTexture(), dstRect);
  encoder->generateMipmapsForTexture(textureView->getTexture());
}

}  // namespace tgfx
