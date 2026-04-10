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

#include "ResolveMSAATask.h"
#include "core/utils/Log.h"
#include "tgfx/gpu/CommandEncoder.h"

namespace tgfx {
ResolveMSAATask::ResolveMSAATask(BlockAllocator* allocator,
                                 std::shared_ptr<RenderTargetProxy> renderTargetProxy,
                                 const Rect& resolveRect)
    : RenderTask(allocator), renderTargetProxy(std::move(renderTargetProxy)),
      resolveRect(resolveRect) {
}

void ResolveMSAATask::execute(CommandEncoder* encoder) {
  auto renderTarget = renderTargetProxy->getRenderTarget();
  if (renderTarget == nullptr) {
    LOGE("ResolveMSAATask::execute() Failed to get the render target!");
    return;
  }
  encoder->resolveRenderTarget(renderTarget.get(), resolveRect);
}
}  // namespace tgfx
