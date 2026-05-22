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
#include "gpu/resources/StencilTextureResource.h"
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
  // Attach a depth/stencil texture only when at least one op opts in. The texture is reused
  // through StencilTextureResource's scratch cache; we keep the strong reference alive for the
  // duration of the render pass so the cache cannot recycle it mid-execution.
  std::shared_ptr<StencilTextureResource> stencilResource = nullptr;
  for (auto& op : drawOps) {
    if (op != nullptr && op->needsStencil()) {
      stencilResource = StencilTextureResource::FindOrCreate(
          renderTarget->getContext(), renderTarget->width(), renderTarget->height());
      if (stencilResource != nullptr) {
        descriptor.depthStencilAttachment.texture = stencilResource->getTexture();
        descriptor.depthStencilAttachment.loadAction = LoadAction::Clear;
        descriptor.depthStencilAttachment.storeAction = StoreAction::DontCare;
        descriptor.depthStencilAttachment.depthClearValue = 1.0f;
        descriptor.depthStencilAttachment.depthReadOnly = false;
        descriptor.depthStencilAttachment.stencilClearValue = 0;
        descriptor.depthStencilAttachment.stencilReadOnly = false;
      }
      break;
    }
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
