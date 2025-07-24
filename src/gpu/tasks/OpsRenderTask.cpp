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

#include "OpsRenderTask.h"
#include "gpu/RenderPass.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
void OpsRenderTask::execute(GPU*) {
  auto renderPass = RenderPass::Make(renderTargetProxy->getRenderTarget());
  if (renderPass == nullptr) {
    LOGE("OpsRenderTask::execute() Failed to initialize the render pass!");
    return;
  }
  auto tempOps = std::move(ops);
  for (auto& op : tempOps) {
    op->execute(renderPass.get());
    op = nullptr;
  }
  renderPass->end();
}
}  // namespace tgfx
