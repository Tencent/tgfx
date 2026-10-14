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
#include <optional>
#include <vector>
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/AOTRenderPlan.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/tasks/RenderTask.h"

namespace tgfx {
class Context;
class RenderTargetProxy;
class AOTMaterializationRecorder;

class AOTPlanExecutor {
 public:
  /**
   * Whether the executor can run this plan with the precompiled kernels: either one pass of a
   * kernel the executor runs on a draw (PointwiseChain/Graph, PointwiseTail, PerlinNoiseFill,
   * YUVTextureFill) within that kernel's slot and resource budgets, or a linear PointwiseTail chain
   * split into several passes at the tail slot budget. Decompose only returns plans that pass it,
   * so every production entry and every diagnostic that reports a draw as routable share this rule.
   * A multi-pass plan stores each intermediate result in an RGBA8 texture, so a pass boundary
   * quantizes to the 1/255 grid (an AlphaThreshold right after a boundary can flip at the grid
   * gap).
   */
  static bool CanExecute(const AOTEffectGraph& graph, const AOTEffectPlan& plan);

  /**
   * Whether the plan's passes evaluate in device space only, so its intermediate fills reproduce
   * what the original draw computes at every pixel whatever its geometry: true for single-pass
   * plans, and for multi-pass plans whose source samples device space. A multi-pass plan over a
   * local-coordinate source additionally needs the draw's device-to-local mapping (see Make).
   */
  static bool SamplesDeviceSpace(const AOTEffectGraph& graph, const AOTEffectPlan& plan);

  /**
   * Builds an atomic render task for the plan without enqueueing it: every materializing pass
   * renders into an intermediate target, then originalDraw replays with the terminal pass's color
   * processors. The plan must pass CanExecute. Stencil-and-cover draws manage their own pipelines
   * and cannot use this task. On failure, originalDraw remains unchanged. On success, ownership of
   * originalDraw moves into the task.
   *
   * deviceBounds is the area the original draw covers on the destination, in top-left device
   * coordinates; the intermediates cover exactly that area (multi-pass plans only). fpCoordOffset
   * is the translation between destination device coordinates and the coordinate space the
   * fragment processors evaluate in: zero for an on-screen draw, the caller's coordOffset for an
   * offscreen fill (DrawingManager::fillRTWithFP). clearDestination is true only for a new
   * offscreen fill: its entire backing (including padding past the logical bounds) must start
   * transparent. Draws compositing onto an existing destination leave it false. deviceToLocal is
   * the affine map from the draw's top-left device positions to the local coordinates its
   * processors see (StandardDrawOp::deviceToLocalMatrix); a multi-pass plan over a local-coordinate
   * source applies it to the source pass, and nullopt means local == device + fpCoordOffset.
   *
   * producers, when not null, holds the blend-child materializations recorded while the plan's
   * processors were built (the materialization retry). They are moved into the task ahead of the
   * plan's own passes, so they run in the same task as the draw that samples them. They are
   * consumed even when Make fails; the caller then refuses the retry, so nothing reads them.
   */
  static PlacementPtr<RenderTask> Make(Context* context, uint32_t renderFlags,
                                       const AOTEffectGraph& graph, const AOTEffectPlan& plan,
                                       const Rect& deviceBounds,
                                       std::shared_ptr<RenderTargetProxy> destination,
                                       PlacementPtr<DrawOp>* originalDraw,
                                       const Point& fpCoordOffset = Point::Zero(),
                                       bool clearDestination = false,
                                       const std::optional<Matrix>& deviceToLocal = std::nullopt,
                                       AOTMaterializationRecorder* producers = nullptr);

  /**
   * Builds the passes of a plan that passed CanExecute without validating or enqueueing anything:
   * appends the split-off passes of a multi-pass plan to passes and the terminal color processors
   * to terminalColors. The parameters mean what they mean for Make; destination is only read for
   * its origin. Returns false if a processor, target, or draw cannot be created.
   */
  static bool BuildPasses(Context* context, uint32_t renderFlags, const AOTEffectGraph& graph,
                          const AOTEffectPlan& plan, const Rect& deviceBounds,
                          const RenderTargetProxy* destination, const Point& fpCoordOffset,
                          const std::optional<Matrix>& deviceToLocal,
                          std::vector<AOTRenderPass>* passes,
                          DrawOp::ColorProcessorList* terminalColors);
};
}  // namespace tgfx
