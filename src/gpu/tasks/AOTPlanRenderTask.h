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

#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/tasks/RenderTask.h"

namespace tgfx {

class RenderTarget;

/**
 * Render task for a single-pass AOT plan: it replays the original draw with the plan's rebuilt
 * color processors onto the destination. The prepare phase is atomic: the draw is prepared strictly
 * against the precompiled artifacts, and if the lookup or target resolution fails, the task replays
 * the original draw unchanged instead. Once the render pass begins, a setup failure stops the task
 * and is recorded as a planExecutionFailures diagnostic, matching the plain task family's behavior
 * on backend errors.
 */
class AOTPlanRenderTask : public RenderTask {
 public:
  AOTPlanRenderTask(BlockAllocator* allocator, DrawOp::ColorProcessorList&& planColors,
                    std::shared_ptr<RenderTargetProxy> destination);

  void setOriginalDraw(PlacementPtr<DrawOp> drawOp);

  void setClearDestination(bool clear) {
    clearDestination = clear;
  }

  void execute(CommandEncoder* encoder) override;

 private:
  DrawOp::ColorProcessorList planColors = {};
  std::shared_ptr<RenderTargetProxy> destination = nullptr;
  PlacementPtr<DrawOp> originalDraw = nullptr;
  bool clearDestination = false;

  void executeFallback(CommandEncoder* encoder, std::shared_ptr<RenderTarget> finalTarget);
};
}  // namespace tgfx
