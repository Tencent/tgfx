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

#pragma once

#include <vector>
#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/tasks/RenderTask.h"

namespace tgfx {

class RenderTarget;

// One materialized step of a multi-pass AOT plan: the exact-fit target the step renders into and
// the prepared draw op that renders it.
struct AOTIntermediatePass {
  std::shared_ptr<RenderTargetProxy> target = nullptr;
  PlacementPtr<DrawOp> drawOp = nullptr;
};

/**
 * Render task for a supported linear AOT plan: it renders every materializing pass into its
 * intermediate target and then replays the original draw with the plan's terminal color list onto
 * the destination. The guarantee is two-phase, and only the first phase is atomic:
 *
 *  - Prepare phase (atomic fallback): every intermediate pass and the terminal pass are prepared
 *    strictly against precompiled artifacts; if any lookup or target resolution fails, the whole
 *    task falls back atomically to the original draw on the runtime route, so up to the execution
 *    boundary a plan either runs fully precompiled or not at all.
 *  - Execution phase (fail-stop, non-atomic): once passes begin, a render-pass setup failure stops
 *    the task at that pass. The destination may then carry partially executed passes; the failure
 *    is recorded as a planExecutionFailures diagnostic instead of being silently dropped. This
 *    matches the plain task family's behavior on backend errors and deliberately does not attempt
 *    a mid-flight replay.
 */
class AOTPlanRenderTask : public RenderTask {
 public:
  AOTPlanRenderTask(BlockAllocator* allocator,
                    std::vector<AOTIntermediatePass>&& intermediatePasses,
                    DrawOp::ColorProcessorList&& terminalColors,
                    std::shared_ptr<RenderTargetProxy> destination);

  void setOriginalDraw(PlacementPtr<DrawOp> drawOp);

  void execute(CommandEncoder* encoder) override;

 private:
  std::vector<AOTIntermediatePass> intermediatePasses = {};
  DrawOp::ColorProcessorList terminalColors = {};
  std::shared_ptr<RenderTargetProxy> destination = nullptr;
  PlacementPtr<DrawOp> originalDraw = nullptr;

  void executeFallback(CommandEncoder* encoder, std::shared_ptr<RenderTarget> finalTarget);
};
}  // namespace tgfx
