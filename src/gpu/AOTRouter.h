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

#include <cstdint>
#include <functional>
#include <vector>
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
class AOTEffectGraph;
struct AOTEffectPlan;
class AOTMaterializationRecorder;
class Context;

/**
 * Rebuilds a draw's leading color processor (its materialization-prone source) with the
 * MaterializeBlendChildren flag set, so every EnsureSimpleBlendChild rewrite happens now. The
 * closure captures the exact FPArgs (including NestedRasterization) the original processor was
 * built with, so callers have no flags to thread through. Returns nullptr when the rebuild fails.
 */
using ColorChainRebuild = std::function<PlacementPtr<FragmentProcessor>()>;

/**
 * How far one routing attempt got.
 */
enum class AOTRouteResult {
  /// The processors did not lower, or no planner produced an executable plan for them.
  NoPlan,
  /// A plan exists, but the entry could not build a render task for it (SubmitPlan returned false).
  TaskRefused,
  /// The entry built and enqueued the plan task; it now owns the draw.
  Submitted,
  /// The plan is the prepare-time fused serving's own shape — a single chain-capable pass with no
  /// recorded producers — so no task runs for it: the entry keeps its draw on the plain route with
  /// the deferral flag, and the plain lookup's fused walk serves it at prepare time.
  Deferred,
};

/**
 * The AOT routing rules, in one place. A draw reaches the precompiled kernels in one of two ways:
 *
 *   - As a plan task (AOTPlanRenderTask), built at record time by the two entries,
 *     OpsCompositor::addDrawOp (on-screen draws) and DrawingManager::fillRTWithFP (offscreen
 *     fills), through Route / RouteWithMaterializedLeading below. A plan task can run several
 *     passes: materialized blend operands and the split-off passes of a long linear chain.
 *   - Through the plain program lookup at prepare time, for a draw that is still on the plain
 *     route: the two re-expressing kernels (the chain interpreter and the perlin kernel) rebuild
 *     the draw's own expression per draw (PlanInlineRewrite decides the shape). Single pass only;
 *     the chain interpreter also folds coverage processors.
 *
 * Each plan-task entry collects the processors it wants planned and supplies a SubmitPlan callback
 * that turns a plan into a render task for its own destination. The router owns planning and the
 * materialization retry, so both entries apply the same rules:
 *
 *   1. Route the original processors.
 *   2. If that route was not taken (no plan, or the entry could not build a task for it) and
 *      RetryWarranted holds for the leading processor, rebuild the leading processor with
 *      materialized blend children and route again (RouteWithMaterializedLeading).
 *   3. Otherwise the entry keeps its original draw for the plain route.
 */
class AOTRouter {
 public:
  /**
   * Builds and enqueues the render task for a plan. Returns false, leaving the entry's draw
   * untouched, when no task can be built for it. producers is the retry's recorder (nullptr for an
   * original route); the entry hands it to AOTPlanExecutor::Make so the recorded producers run in
   * the same task as the draw.
   */
  using SubmitPlan = std::function<bool(const AOTEffectGraph&, const AOTEffectPlan&,
                                        AOTMaterializationRecorder* producers)>;

  /**
   * Lowers and decomposes the processors and hands the plan to submit. An original route
   * (producers == nullptr) whose plan needs no task never calls submit and returns Deferred
   * instead: the plain lookup's fused walk owns that draw at prepare time. The materialization
   * retry passes a recorder, so its plans always run as tasks.
   */
  static AOTRouteResult Route(const std::vector<const FragmentProcessor*>& processors,
                              const SubmitPlan& submit,
                              AOTMaterializationRecorder* producers = nullptr);

  /**
   * Whether a plan can only run as a plan task: more than one pass, or a single pass whose kernel
   * the prepare-time fused serving cannot express (the fused kernels take a draw's whole color
   * expression as one chain or perlin processor; every other single-pass kernel still needs the
   * task). Route itself applies this gate to original routes and returns Deferred for the ones it
   * turns away; the retry's plans always run as tasks regardless.
   */
  static bool PlanRequiresTask(const AOTEffectPlan& plan);

  /**
   * Plans processors for the fused serving: true when they lower and decompose into a single pass
   * of a kernel whose processor can stand in for a draw's whole color expression
   * (PointwiseChain, PointwiseGraph, or PerlinNoiseFill). A multi-pass plan never qualifies: its
   * intermediate passes need a plan task.
   */
  static bool PlanInlineRewrite(const std::vector<const FragmentProcessor*>& processors,
                                AOTEffectGraph* graph, AOTEffectPlan* plan);

  /**
   * Whether the materialization retry applies to a chain led by this processor: it is a two-child
   * blend and the materialization policy would rewrite at least one child under renderFlags (a
   * child that is required for correctness always qualifies; one that only improves matchability
   * qualifies outside NestedRasterization).
   */
  static bool RetryWarranted(const FragmentProcessor* leading, uint32_t renderFlags);

  /**
   * The materialization retry: rebuilds the leading processor, then routes it followed by the
   * trailing processors. The rebuild runs inside an AOTMaterializationRecorder, so its blend-child
   * materializations are recorded instead of rendered: a taken route runs them in the draw's own
   * plan task, a refused one drops them unrendered. The rebuilt processor is released either way,
   * so a refused retry never puts materialized operands on the entry's original draw. A failed
   * rebuild reports NoPlan.
   */
  static AOTRouteResult RouteWithMaterializedLeading(
      Context* context, const ColorChainRebuild& rebuildLeading,
      const std::vector<const FragmentProcessor*>& trailing, const SubmitPlan& submit);
};
}  // namespace tgfx
