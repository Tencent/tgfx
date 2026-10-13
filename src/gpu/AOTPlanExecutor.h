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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/tasks/RenderTask.h"

namespace tgfx {
class Context;
class RenderTargetProxy;

class AOTPlanExecutor {
 public:
  /**
   * Whether the executor can run this plan with the precompiled kernels: exactly one pass, of a
   * kernel the executor runs on a draw (PointwiseChain/Graph, PointwiseTail, PerlinNoiseFill,
   * YUVTextureFill), within that kernel's slot and resource budgets. Decompose only returns plans
   * that pass it, so every production entry and every diagnostic that reports a draw as routable
   * share this one rule. Multi-pass plans are never produced: their RGBA8 intermediates would
   * quantize (a discontinuous operator such as AlphaThreshold can flip outright) and could not
   * inherit the paint alpha or the draw transform.
   */
  static bool CanExecute(const AOTEffectGraph& graph, const AOTEffectPlan& plan);

  /**
   * Builds an atomic render task that replays originalDraw with the plan's rebuilt color processors,
   * without enqueueing it. The plan must pass CanExecute. Stencil-and-cover draws manage their own
   * pipelines and cannot use this task. On failure, originalDraw remains unchanged. On success,
   * ownership of originalDraw moves into the task. clearDestination is true only for a new
   * offscreen fill: its entire backing (including padding past the logical bounds) must start
   * transparent. Draws compositing onto an existing destination leave it false.
   */
  static PlacementPtr<RenderTask> Make(Context* context, const AOTEffectGraph& graph,
                                       const AOTEffectPlan& plan,
                                       std::shared_ptr<RenderTargetProxy> destination,
                                       PlacementPtr<DrawOp>* originalDraw,
                                       bool clearDestination = false);
};
}  // namespace tgfx
