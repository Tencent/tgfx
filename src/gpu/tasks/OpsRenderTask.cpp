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
#include "inspect/InspectorMark.h"

namespace tgfx {
void OpsRenderTask::execute(CommandEncoder* encoder) {
  TASK_MARK(tgfx::inspect::OpTaskType::OpsRenderTask);
  auto renderTarget = renderTargetProxy->getRenderTarget();
  if (renderTarget == nullptr) {
    LOGE("OpsRenderTask::execute() Render target is null!");
    return;
  }
  auto loadOp = clearColor.has_value() ? LoadAction::Clear : LoadAction::Load;
  auto resolveTexture =
      renderTarget->sampleCount() > 1 ? renderTarget->getSampleTexture() : nullptr;
  RenderPassDescriptor descriptor(renderTarget->getRenderTexture(), loadOp, StoreAction::Store,
                                  clearColor.value_or(Color::Transparent()), resolveTexture);
  auto renderPass = encoder->beginRenderPass(descriptor);
  if (renderPass == nullptr) {
    LOGE("OpsRenderTask::execute() Failed to initialize the render pass!");
    return;
  }
  for (auto& op : drawOps) {
    op->execute(renderPass.get(), renderTarget.get());
    // Release the Op immediately after execution to maximize GPU resource reuse.
    op = nullptr;
  }
  renderPass->end();
}
}  // namespace tgfx
