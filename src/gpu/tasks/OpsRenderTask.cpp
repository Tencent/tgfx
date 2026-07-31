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
  // targeting the same proxy share one stencil texture for the proxy's lifetime.
  bool stencilAvailable = false;
  auto stencilClearBoundsRect = Rect::MakeEmpty();
  for (auto& op : drawOps) {
    if (op->needsStencil()) {
      if (!stencilAvailable) {
        auto stencilTexture = renderTargetProxy->getStencil(renderTarget->sampleCount());
        if (stencilTexture == nullptr) {
          LOGE(
              "OpsRenderTask::execute() Failed to acquire stencil texture; "
              "skipping stencil-aware ops in this pass.");
          break;
        }
        descriptor.depthStencilAttachment.texture = std::move(stencilTexture);
        descriptor.depthStencilAttachment.loadAction = LoadAction::Clear;
        descriptor.depthStencilAttachment.storeAction = StoreAction::DontCare;
        descriptor.depthStencilAttachment.depthClearValue = 1.0f;
        descriptor.depthStencilAttachment.depthReadOnly = false;
        descriptor.depthStencilAttachment.stencilClearValue = 0;
        descriptor.depthStencilAttachment.stencilReadOnly = false;
        stencilAvailable = true;
      }
      auto bounds = op->getStencilResolveBounds();
      if (!bounds.isEmpty()) {
        stencilClearBoundsRect.join(bounds);
      }
    }
  }
  if (stencilAvailable) {
    if (stencilClearBoundsRect.isEmpty()) {
      descriptor.depthStencilAttachment.clearScissor = std::nullopt;
    } else {
      // Ops report their stencil write footprint in canvas top-left device space, but the
      // backend scissor semantics follow the render target origin (GL windows are BottomLeft).
      // Match the transform OpsCompositor::FlipYIfNeeded applies to draw-time scissors, and
      // roundOut so the integer scissor covers every pixel any stencil pass may touch — a
      // truncated edge would leave stale stencil values that the cover pass then treats as
      // hits, producing 1-pixel artefacts on rotated/scaled geometry.
      stencilClearBoundsRect.roundOut();
      renderTargetProxy->getOriginTransform().mapRect(&stencilClearBoundsRect);
      descriptor.depthStencilAttachment.clearScissor = stencilClearBoundsRect;
    }
  }
  auto renderPass = encoder->beginRenderPass(descriptor);
  if (renderPass == nullptr) {
    LOGE("OpsRenderTask::execute() Failed to initialize the render pass!");
    return;
  }
  for (auto& op : drawOps) {
    if (op != nullptr && !stencilAvailable && op->needsStencil()) {
      // Drop stencil-aware ops when no stencil attachment was bound — running them would
      // hit silent backend validation errors. Non-stencil ops continue to execute normally.
      op = nullptr;
      continue;
    }
    op->execute(renderPass.get(), renderTarget.get());
    // Release the Op immediately after execution to maximize GPU resource reuse.
    op = nullptr;
  }
  renderPass->end();
}
}  // namespace tgfx
