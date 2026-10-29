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

#include "gpu/AOTRenderPlan.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/tasks/RenderTask.h"

namespace tgfx {

class RenderTarget;

/**
 * Render task for an AOTRenderPlan: it renders every producer pass into its offscreen target in
 * plan order, then replays the original draw with the plan's terminal color processors onto the
 * destination. The prepare phase is atomic: every producer pass and the terminal draw are prepared
 * strictly against the precompiled artifacts, and if any lookup or target resolution fails, the
 * task replays the original draw unchanged instead. Once passes begin, a render-pass setup failure
 * stops the task at that pass (the destination is untouched unless the terminal pass started) and
 * is recorded as a planExecutionFailures diagnostic, matching the plain task family's behavior on
 * backend errors.
 */
class AOTPlanRenderTask : public RenderTask {
 public:
  /**
   * The plan must be well-formed (AOTRenderPlan::isWellFormed).
   */
  AOTPlanRenderTask(BlockAllocator* allocator, AOTRenderPlan&& plan,
                    std::shared_ptr<RenderTargetProxy> destination);

  void setOriginalDraw(PlacementPtr<DrawOp> drawOp);

  void setClearDestination(bool clear) {
    clearDestination = clear;
  }

  void execute(CommandEncoder* encoder) override;

 private:
  // Test-only fault injection (diagnostics contract): which pass of a plan task fails its
  // render-pass setup during execution — the first producer pass, a later producer pass, or the
  // terminal pass onto the destination. Production never sets it, so the environment cannot steer
  // a release build; the tests that steer it access the member directly.
  enum class InjectedPassFailure { None, FirstProducer, MiddleProducer, Terminal };

  static InjectedPassFailure injectedPassFailure;

  static bool ShouldInjectFailure(bool isFirstProducer, bool isTerminal);

  AOTRenderPlan plan = {};
  std::shared_ptr<RenderTargetProxy> destination = nullptr;
  PlacementPtr<DrawOp> originalDraw = nullptr;
  bool clearDestination = false;

  void executeFallback(CommandEncoder* encoder, std::shared_ptr<RenderTarget> finalTarget);
};
}  // namespace tgfx
