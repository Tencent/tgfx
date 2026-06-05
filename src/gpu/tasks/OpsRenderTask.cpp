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
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {
void OpsRenderTask::execute(CommandEncoder* encoder) {
  auto renderTarget = renderTargetProxy->getRenderTarget();
  if (renderTarget == nullptr) {
    LOGE("OpsRenderTask::execute() Render target is null!");
    return;
  }
  auto loadOp = clearColor.has_value() ? LoadAction::Clear : LoadAction::Load;
  auto resolveTexture =
      renderTarget->sampleCount() > 1 ? renderTarget->getSampleTexture() : nullptr;
  RenderPassDescriptor descriptor(renderTarget->getRenderTexture(), loadOp, StoreAction::Store,
                                  clearColor.value_or(PMColor::Transparent()), resolveTexture);
  // Attach a depth/stencil texture only when at least one op opts in. The stencil is owned by
  // the RenderTargetProxy itself (see RenderTargetProxy::getStencil), so all OpsRenderTasks
  // targeting the same proxy reuse one stencil allocation across the proxy's lifetime; the
  // buffer contents are cleared at the start of each pass that opts in.
  std::shared_ptr<Texture> stencilTexture = nullptr;
  bool stencilRequestedButMissing = false;
  for (auto& op : drawOps) {
    if (op != nullptr && op->needsStencil()) {
      stencilTexture = renderTargetProxy->getStencil();
      if (stencilTexture != nullptr) {
        descriptor.depthStencilAttachment.texture = stencilTexture;
        descriptor.depthStencilAttachment.loadAction = LoadAction::Clear;
        descriptor.depthStencilAttachment.storeAction = StoreAction::DontCare;
        descriptor.depthStencilAttachment.depthClearValue = 1.0f;
        descriptor.depthStencilAttachment.depthReadOnly = false;
        descriptor.depthStencilAttachment.stencilClearValue = 0;
        descriptor.depthStencilAttachment.stencilReadOnly = false;
      } else {
        // The op declared it needs a stencil buffer but the render target proxy could not
        // hand one out (most likely because the underlying texture allocation failed). Bail
        // out of the whole task: continuing would let stencil-aware ops execute against a
        // pass without an attached stencil and produce silent backend validation errors.
        stencilRequestedButMissing = true;
      }
      break;
    }
  }
  if (stencilRequestedButMissing) {
    LOGE("OpsRenderTask::execute() Failed to acquire stencil texture for stencil-aware ops!");
    return;
  }
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
