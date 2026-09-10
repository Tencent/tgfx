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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "AOTPlanRenderTask.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ops/StandardDrawOp.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {

static bool ExecutePreparedPass(CommandEncoder* encoder, RenderTarget* renderTarget,
                                StandardDrawOp* drawOp, LoadAction loadAction,
                                const PMColor& clearColor) {
  auto resolveTexture =
      renderTarget->sampleCount() > 1 ? renderTarget->getSampleTexture() : nullptr;
  RenderPassDescriptor descriptor(renderTarget->getRenderTexture(), loadAction, StoreAction::Store,
                                  clearColor, resolveTexture);
  auto renderPass = encoder->beginRenderPass(descriptor);
  if (renderPass == nullptr) {
    LOGE("AOTPlanRenderTask::execute() Failed to initialize the render pass!");
    return false;
  }
  drawOp->executePrepared(renderPass.get(), renderTarget, false);
  renderPass->end();
  return true;
}

AOTPlanRenderTask::AOTPlanRenderTask(BlockAllocator* allocator,
                                     std::vector<AOTIntermediatePass>&& intermediatePasses,
                                     DrawOp::ColorProcessorList&& terminalColors,
                                     std::shared_ptr<RenderTargetProxy> destination)
    : RenderTask(allocator), intermediatePasses(std::move(intermediatePasses)),
      terminalColors(std::move(terminalColors)), destination(std::move(destination)) {
}

void AOTPlanRenderTask::setOriginalDraw(PlacementPtr<DrawOp> drawOp) {
  originalDraw = std::move(drawOp);
}

void AOTPlanRenderTask::execute(CommandEncoder* encoder) {
  std::vector<std::shared_ptr<RenderTarget>> renderTargets = {};
  renderTargets.reserve(intermediatePasses.size());
  bool targetsResolved = true;
  for (const auto& pass : intermediatePasses) {
    auto renderTarget = pass.target->getRenderTarget();
    targetsResolved = targetsResolved && renderTarget != nullptr;
    renderTargets.push_back(std::move(renderTarget));
  }
  auto finalTarget = destination->getRenderTarget();
  targetsResolved = targetsResolved && finalTarget != nullptr;
  if (!targetsResolved) {
    executeFallback(encoder, std::move(finalTarget));
    return;
  }

  bool prepared = true;
  // These strict prepares double as route-validation probes: a plan pass the matcher cannot
  // serve (e.g. a perspective leaf transform) falls back to the runtime route, so its lookup
  // failure must not record a diagnostic miss against the rewritten pipeline — the fallback
  // path records the draw's original pipeline instead.
  auto* statsCache = finalTarget->getContext()->precompiledShaderCache();
  statsCache->setMissRecordingPaused(true);
  for (size_t index = 0; index < intermediatePasses.size(); ++index) {
    auto drawOp = static_cast<StandardDrawOp*>(intermediatePasses[index].drawOp.get());
    if (!drawOp->prepare(renderTargets[index].get(), ProgramLookupMode::PrecompiledOnly)) {
      prepared = false;
      break;
    }
  }
  if (prepared && !static_cast<StandardDrawOp*>(originalDraw.get())
                       ->prepare(finalTarget.get(), ProgramLookupMode::PrecompiledOnly,
                                 std::move(terminalColors))) {
    prepared = false;
  }
  statsCache->setMissRecordingPaused(false);
  if (!prepared) {
    executeFallback(encoder, std::move(finalTarget));
    return;
  }

  AOTDrawStats drawStats = {};
  drawStats.kernelInvocations = intermediatePasses.size() + 1;
  drawStats.offscreenTargets = intermediatePasses.size();
  drawStats.materializedEdges = intermediatePasses.size();
  drawStats.renderTargetSwitches = intermediatePasses.size();
  for (const auto& renderTarget : renderTargets) {
    auto bytes = static_cast<uint64_t>(renderTarget->width()) *
                 static_cast<uint64_t>(renderTarget->height()) * 4;
    drawStats.intermediateReadBytes += bytes;
    drawStats.intermediateWriteBytes += bytes;
    drawStats.peakTemporaryBytes += bytes;
  }

  for (size_t index = 0; index < intermediatePasses.size(); ++index) {
    if (!ExecutePreparedPass(encoder, renderTargets[index].get(),
                             static_cast<StandardDrawOp*>(intermediatePasses[index].drawOp.get()),
                             LoadAction::Clear, PMColor::Transparent())) {
      return;
    }
  }
  if (!ExecutePreparedPass(encoder, finalTarget.get(),
                           static_cast<StandardDrawOp*>(originalDraw.get()), LoadAction::Load,
                           PMColor::Transparent())) {
    return;
  }
  auto cache = finalTarget->getContext()->precompiledShaderCache();
  if (cache->diagnosticRecordingEnabled()) {
    cache->recordDraw(drawStats, true);
  }
}

void AOTPlanRenderTask::executeFallback(CommandEncoder* encoder,
                                        std::shared_ptr<RenderTarget> finalTarget) {
  if (finalTarget == nullptr) {
    LOGE("AOTPlanRenderTask::executeFallback() Final render target is null!");
    return;
  }
  auto drawOp = static_cast<StandardDrawOp*>(originalDraw.get());
  if (!drawOp->prepare(finalTarget.get(), ProgramLookupMode::AllowRuntimeFallback)) {
    return;
  }
  if (!ExecutePreparedPass(encoder, finalTarget.get(), drawOp, LoadAction::Load,
                           PMColor::Transparent())) {
    return;
  }
  auto cache = finalTarget->getContext()->precompiledShaderCache();
  if (cache->diagnosticRecordingEnabled()) {
    AOTDrawStats drawStats = {};
    drawStats.atomicFallbacks = 1;
    drawStats.kernelInvocations = 1;
    cache->recordDraw(drawStats, false);
  }
}
}  // namespace tgfx
