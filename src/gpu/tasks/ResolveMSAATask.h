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
 * A RenderTask that resolves MSAA content from the render texture to the sample texture. This task
 * is used for deferred MSAA resolve when the render target content needs to be read.
 */
class ResolveMSAATask : public RenderTask {
 public:
  ResolveMSAATask(BlockAllocator* allocator, std::shared_ptr<RenderTargetProxy> renderTargetProxy,
                  const Rect& resolveRect);

  void execute(CommandEncoder* encoder) override;

 private:
  std::shared_ptr<RenderTargetProxy> renderTargetProxy = nullptr;
  Rect resolveRect = {};
};
}  // namespace tgfx
