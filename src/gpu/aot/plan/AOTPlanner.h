/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <optional>
#include <vector>
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/plan/AOTEffectDecomposer.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/tasks/RenderTask.h"
#include "tgfx/core/BlendMode.h"

namespace tgfx {

class Context;
class RenderTargetProxy;
struct AOTChainCompile;

/// The record-time route one plan takes: served in place at prepare (its descriptor rides the
/// draw's IR), run as a plan task, or left to the plain lookup at prepare.
enum class AOTRecordRoute : uint8_t { Plain, InPlace, Task };

/**
 * The record-time route's one planner: the decomposition, the routing predicates, the pass
 * building and the task assembly, in one place. A draw reaches the precompiled kernels in one of
 * two ways:
 *
 *   - As a plan task (AOTPlanRenderTask), built at record time by the two entries,
 *     OpsCompositor::addDrawOp (on-screen draws) and DrawingManager::fillRTWithFP (offscreen
 *     fills), from the plan Plan below returns over the draw's record-time IR. A plan task can
 *     run several passes: materialized blend operands and the split-off passes of a long linear
 *     chain.
 *   - Through the plain program lookup at prepare time, for a draw that is still on the plain
 *     route: the re-expressing kernel (the chain interpreter) rebuilds the draw's own expression
 *     per draw. Single pass only; the chain interpreter also merges the classified coverage
 *     terms.
 *
 * An entry whose route produced no plan, or whose Build was refused, keeps its original draw for
 * the plain route.
 */
class AOTPlanner {
 public:
  /**
   * Plans a draw whose record-time IR already carries the lowered color graph: the decomposition
   * runs on ir.color with no second lowering, and the coverage terms stay on the draw — a
   * multi-pass plan merges them into its terminal pass (see TaskCarriesCoverage). Returns
   * nullopt when no planner covers the color graph; failureOutcome then receives
   * AOTDecomposeOutcome::UnsupportedShape (the graph already lowered, so no lowering can block).
   */
  static std::optional<Plan> MakePlan(const DrawIR& ir,
                                      AOTDecomposeOutcome* failureOutcome = nullptr);

  /**
   * Whether the plan's single pass is served in place by the prepare-time fused serving: the draw
   * keeps the plain route and its IR carries the pass descriptor, so the matcher compiles from the
   * record-time plan instead of decomposing the same graph again at prepare. The conditions mirror
   * Validate's single-pass branch exactly: a plan whose one pass materializes, carries
   * dependencies, or does not output the plan's root needs a task even at one pass, and a non-chain
   * kernel (perlin, YUV) has no fused serving — PlanRequiresTask keeps those on the task route.
   */
  static bool ServesInPlacePass(const Plan& plan);

  /**
   * Whether a plan that needs a task may carry the draw's coverage terms on its terminal pass:
   * the terminal must be a chain pass (a linear-kernel terminal — YUV or perlin — serves through a
   * rebuilt processor and cannot merge terms), and the draw's blend must never need a dst
   * texture with coverage — the exact set the record-time fold admitted, because a Subtree term
   * (mask-filter semantics) modulates the source, which the coverage composite reproduces only
   * for those modes. A draw the gate refuses keeps the plain route, coverage attached.
   */
  static bool TaskCarriesCoverage(const Plan& plan,
                                  const std::vector<AOTCoverageTerm>& coverageTerms,
                                  BlendMode blendMode);

  /**
   * Whether a plan can only run as a plan task: more than one pass, or a single pass whose kernel
   * the prepare-time fused serving cannot express (the fused kernels take a draw's whole color
   * expression as one chain or perlin processor; every other single-pass kernel still needs the
   * task).
   */
  static bool PlanRequiresTask(const Plan& plan);

  /**
   * The record-time route decision, in one place: the two record entries (OpsCompositor's draw
   * recording and DrawingManager::fillRTWithFP) ask this instead of re-deriving the predicate
   * chain, so a rule change cannot land on one entry only. A fill passes its empty coverage list
   * and Src blend; the coverage gate then admits every task-requiring plan, as the fill route
   * always did.
   */
  static AOTRecordRoute RouteRecordPlan(const Plan& plan,
                                        const std::vector<AOTCoverageTerm>& coverageTerms,
                                        BlendMode blendMode);

  /**
   * The plan type's own well-formedness predicate — the same check Build runs first. Either one
   * pass of a kernel the fused serving runs on a draw (PointwiseChain/Graph, YUVTextureFill)
   * within its slot and resource budgets, or a linear chain split into several chain passes, each
   * fitting one chain pass. Decompose only returns plans that pass it, so every production entry
   * and every diagnostic that reports a draw as routable share this rule. A multi-pass plan stores
   * each intermediate result in an RGBA8 texture, so a pass boundary quantizes to the 1/255 grid
   * (an AlphaThreshold right after a boundary can flip at the grid gap).
   *
   * A single chain pass is admitted by the builder's one compile entry — the same call the build
   * makes, terminal coverage included — so a validated single pass is by construction buildable,
   * and its compiled program is returned through `compiled` for the build to carry on the pass IR.
   * A multi-pass plan is structurally validated here; its segments compile one per pass when
   * Build builds them.
   *
   * Build is the serving authority; this predicate exists so the plan's validity has one public
   * definition, which the tests assert on and nothing else needs to re-derive.
   */
  static bool Validate(const AOTEffectGraph& graph, const Plan& plan,
                       const std::vector<AOTCoverageTerm>* terminalCoverage = nullptr,
                       AOTChainCompile* compiled = nullptr);

  /**
   * Whether a multi-pass plan's fill passes carry local-coordinate leaves (plainly-sampled
   * textures, gradients, noise sources): those evaluate at top-left device positions and need the
   * draw's affine device-to-local mapping (see Build). A plan whose fills only sample device-space
   * content needs no mapping.
   */
  static bool NeedsLocalRemap(const AOTEffectGraph& graph, const Plan& plan);

  /**
   * Builds an atomic render task for the plan without enqueueing it: every materializing pass
   * renders into an intermediate target, then originalDraw replays with the terminal pass's color
   * processors. The plan must pass Validate (Build runs it first). Stencil-and-cover draws manage
   * their own pipelines and cannot use this task. On failure, originalDraw remains unchanged. On
   * success, ownership of originalDraw moves into the task.
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
  static PlacementPtr<RenderTask> Build(
      Context* context, uint32_t renderFlags, const AOTEffectGraph& graph, const Plan& plan,
      const Rect& deviceBounds, std::shared_ptr<RenderTargetProxy> destination,
      PlacementPtr<DrawOp>* originalDraw, const Point& fpCoordOffset = Point::Zero(),
      bool clearDestination = false, const std::optional<Matrix>& deviceToLocal = std::nullopt,
      const std::vector<AOTCoverageTerm>* terminalCoverage = nullptr);
};
}  // namespace tgfx
