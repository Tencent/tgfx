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

class AOTPlanExecutor {
 public:
  /**
   * Whether the executor can run this plan with the precompiled kernels: either one pass of a
   * kernel the executor runs on a draw (PointwiseChain/Graph, PerlinNoiseFill, YUVTextureFill)
   * within that kernel's slot and resource budgets, or a linear chain split into several chain
   * passes, each fitting one chain pass. Decompose only returns plans that pass it,
   * so every production entry and every diagnostic that reports a draw as routable share this rule.
   * A multi-pass plan stores each intermediate result in an RGBA8 texture, so a pass boundary
   * quantizes to the 1/255 grid (an AlphaThreshold right after a boundary can flip at the grid
   * gap).
   */
  static bool CanExecute(const AOTEffectGraph& graph, const AOTEffectPlan& plan);

  /**
   * Whether a multi-pass plan's fill passes carry local-coordinate leaves (plainly-sampled
   * textures, gradients, noise sources): those evaluate at top-left device positions and need the
   * draw's affine device-to-local mapping (see Make). A plan whose fills only sample device-space
   * content needs no mapping.
   */
  static bool NeedsLocalRemap(const AOTEffectGraph& graph, const AOTEffectPlan& plan);

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
   * For a multi-pass draw, originalDraw must report one uniform geometry color: its alpha is
   * applied in the source pass, while later segments read the intermediate without applying it
   * again. Geometry coverage and blending still execute only in the terminal pass.
   */
  static PlacementPtr<RenderTask> Make(Context* context, uint32_t renderFlags,
                                       const AOTEffectGraph& graph, const AOTEffectPlan& plan,
                                       const Rect& deviceBounds,
                                       std::shared_ptr<RenderTargetProxy> destination,
                                       PlacementPtr<DrawOp>* originalDraw,
                                       const Point& fpCoordOffset = Point::Zero(),
                                       bool clearDestination = false,
                                       const std::optional<Matrix>& deviceToLocal = std::nullopt);

  /**
   * Builds the passes of a plan that passed CanExecute without validating or enqueueing anything:
   * appends the split-off passes of a multi-pass plan to renderPlan->passes — each carrying its
   * cut segment as a pass IR — and the terminal pass's form to renderPlan->terminalIR (a chain
   * plan) or renderPlan->terminalColors (a linear-kernel plan). The parameters mean what they
   * mean for Make; destination is only read for its origin. Returns false if a processor, target,
   * or draw cannot be created.
   */
  static bool BuildPasses(Context* context, uint32_t renderFlags, const AOTEffectGraph& graph,
                          const AOTEffectPlan& plan, const Rect& deviceBounds,
                          const RenderTargetProxy* destination, const Point& fpCoordOffset,
                          const std::optional<Matrix>& deviceToLocal, AOTRenderPlan* renderPlan,
                          float sourceAlpha = 1.0f);
};
}  // namespace tgfx
