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
#include <cstdlib>
#include <string>
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ops/StandardDrawOp.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {

// Fault injection (diagnostics contract): TGFX_AOT_TEST_INJECT_PASS_FAILURE selects which pass of
// a plan task fails its render-pass setup during execution: "first" (the first producer pass),
// "middle" (a later producer pass) or "last" (the terminal pass onto the destination). The
// task must stop after the failure, record the execution-failure diagnostic, and not crash.
// Test-only.
static bool InjectFailureAt(bool isFirstProducer, bool isTerminal) {
  const char* mode = std::getenv("TGFX_AOT_TEST_INJECT_PASS_FAILURE");
  if (mode == nullptr) {
    return false;
  }
  std::string value = mode;
  if (value == "first") {
    return isFirstProducer && !isTerminal;
  }
  if (value == "middle") {
    return !isFirstProducer && !isTerminal;
  }
  return value == "last" && isTerminal;
}

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
  // A pass whose draw submitted nothing (no uniform backing store) has not executed: report it as
  // failed so the task records an execution failure instead of a complete AOT draw.
  bool submitted = drawOp->executePrepared(renderPass.get(), renderTarget, false);
  renderPass->end();
  if (!submitted) {
    LOGE("AOTPlanRenderTask::execute() A prepared pass submitted no draw!");
  }
  return submitted;
}

AOTPlanRenderTask::AOTPlanRenderTask(BlockAllocator* allocator, AOTRenderPlan&& plan,
                                     std::shared_ptr<RenderTargetProxy> destination)
    : RenderTask(allocator), plan(std::move(plan)), destination(std::move(destination)) {
  DEBUG_ASSERT(this->plan.isWellFormed());
}

void AOTPlanRenderTask::setOriginalDraw(PlacementPtr<DrawOp> drawOp) {
  originalDraw = std::move(drawOp);
}

void AOTPlanRenderTask::execute(CommandEncoder* encoder) {
  std::vector<std::shared_ptr<RenderTarget>> renderTargets = {};
  renderTargets.reserve(plan.passes.size());
  bool targetsResolved = true;
  for (const auto& pass : plan.passes) {
    auto renderTarget = pass.target->getRenderTarget();
    targetsResolved = targetsResolved && renderTarget != nullptr;
    renderTargets.push_back(std::move(renderTarget));
  }
  auto finalTarget = destination->getRenderTarget();
  if (!targetsResolved || finalTarget == nullptr) {
    executeFallback(encoder, std::move(finalTarget));
    return;
  }
  // The strict prepares double as route-validation probes: a plan pass the matcher cannot serve
  // (e.g. a perspective leaf transform) falls back to the original draw, so its lookup failure
  // must not record a diagnostic miss against the rewritten pipeline — the fallback path records
  // the draw's original pipeline instead.
  auto* cache = finalTarget->getContext()->precompiledShaderCache();
  cache->setMissRecordingPaused(true);
  bool prepared = true;
  for (size_t index = 0; index < plan.passes.size() && prepared; ++index) {
    auto drawOp = static_cast<StandardDrawOp*>(plan.passes[index].drawOp.get());
    prepared =
        drawOp->prepareForTask(renderTargets[index].get(), ProgramLookupMode::PrecompiledOnly);
  }
  prepared = prepared && static_cast<StandardDrawOp*>(originalDraw.get())
                             ->prepareForTask(finalTarget.get(), ProgramLookupMode::PrecompiledOnly,
                                              std::move(plan.terminalColors));
  cache->setMissRecordingPaused(false);
  if (!prepared) {
    executeFallback(encoder, std::move(finalTarget));
    return;
  }
  auto recordExecutionFailure = [cache]() {
    if (cache->diagnosticRecordingEnabled()) {
      AOTDrawStats failStats = {};
      failStats.planExecutionFailures = 1;
      cache->recordDraw(failStats, false);
    }
  };
  // Passes run in plan order, which places every producer before the passes that sample it.
  for (size_t index = 0; index < plan.passes.size(); ++index) {
    if (InjectFailureAt(index == 0, false)) {
      LOGE("AOTPlanRenderTask::execute() Injected pass failure at producer pass %zu", index);
      recordExecutionFailure();
      return;
    }
    if (!ExecutePreparedPass(encoder, renderTargets[index].get(),
                             static_cast<StandardDrawOp*>(plan.passes[index].drawOp.get()),
                             LoadAction::Clear, PMColor::Transparent())) {
      LOGE("AOTPlanRenderTask::execute() Producer pass %zu failed to begin", index);
      recordExecutionFailure();
      return;
    }
  }
  if (InjectFailureAt(plan.passes.empty(), true)) {
    LOGE("AOTPlanRenderTask::execute() Injected pass failure at the terminal pass");
    recordExecutionFailure();
    return;
  }
  if (!ExecutePreparedPass(
          encoder, finalTarget.get(), static_cast<StandardDrawOp*>(originalDraw.get()),
          clearDestination ? LoadAction::Clear : LoadAction::Load, PMColor::Transparent())) {
    LOGE("AOTPlanRenderTask::execute() Terminal pass failed to begin");
    recordExecutionFailure();
    return;
  }
  if (cache->diagnosticRecordingEnabled()) {
    AOTDrawStats drawStats = {};
    auto edges = plan.passes.size();
    drawStats.kernelInvocations = edges + 1;
    drawStats.offscreenTargets = edges;
    drawStats.materializedEdges = edges;
    drawStats.renderTargetSwitches = edges;
    drawStats.offscreenPlanDraws = 1;
    drawStats.planMaterializedEdges = edges;
    auto lastBucket = drawStats.planPassHistogram.size() - 1;
    drawStats.planPassHistogram[edges < lastBucket ? edges : lastBucket]++;
    for (const auto& renderTarget : renderTargets) {
      auto bytes = static_cast<uint64_t>(renderTarget->width()) *
                   static_cast<uint64_t>(renderTarget->height()) * 4;
      drawStats.intermediateReadBytes += bytes;
      drawStats.intermediateWriteBytes += bytes;
      drawStats.peakTemporaryBytes += bytes;
      // The plan's intermediates also join the flush cycle's simultaneously-live set (the per-draw
      // delta above is a different, draw-local view of the same bytes).
      cache->recordActiveTemporaryBytes(bytes);
    }
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
  if (!drawOp->prepareForTask(finalTarget.get(), ProgramLookupMode::AllowRuntimeFallback)) {
    return;
  }
  if (!ExecutePreparedPass(encoder, finalTarget.get(), drawOp,
                           clearDestination ? LoadAction::Clear : LoadAction::Load,
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
