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
#include <optional>
#include <vector>
#include "gpu/aot/AOTEffectDecomposer.h"
#include "gpu/aot/DrawIR.h"
#include "gpu/processors/FragmentProcessor.h"
#include "tgfx/core/BlendMode.h"

namespace tgfx {

/**
 * One record-time routing outcome: the lowered effect graph and the plan the decomposer produced
 * for it.
 */
struct AOTRoutePlan {
  AOTEffectGraph graph = {};
  AOTEffectPlan plan = {};
};

/**
 * The AOT routing rules, in one place. A draw reaches the precompiled kernels in one of two ways:
 *
 *   - As a plan task (AOTPlanRenderTask), built at record time by the two entries,
 *     OpsCompositor::addDrawOp (on-screen draws) and DrawingManager::fillRTWithFP (offscreen
 *     fills), from the plan Route below returns. A plan task can run several passes:
 *     materialized blend operands and the split-off passes of a long linear chain.
 *   - Through the plain program lookup at prepare time, for a draw that is still on the plain
 *     route: the re-expressing kernels (the chain interpreter and the perlin kernel) rebuild the
 *     draw's own expression per draw (PlanSinglePass decides the shape). Single pass only; the
 *     chain interpreter also folds coverage processors.
 *
 * An entry whose route produced no plan, or whose task build was refused, keeps its original draw
 * for the plain route.
 */
class AOTRouter {
 public:
  /**
   * Lowers and decomposes the processors and returns the graph and plan for them, or nullopt when
   * the processors do not lower or no planner produces an executable plan. What to do with a plan
   * is the caller's: PlanRequiresTask decides between building a render task now and leaving the
   * draw on the plain route for the prepare-time fused walk. failureOutcome, when not null,
   * receives why an attempt failed (Trivial for an empty chain, BlockedByLowering when a processor
   * lacks a lowering, UnsupportedShape when no planner covers the lowered shape, FusablePointwise
   * on success) — the same facts the attempt itself computed, with no second analysis pass.
   */
  static std::optional<AOTRoutePlan> Route(const std::vector<const FragmentProcessor*>& processors,
                                           AOTDecomposeOutcome* failureOutcome = nullptr);

  /**
   * Plans a draw whose record-time IR already carries the lowered color graph: the decomposition
   * runs on ir.color with no second lowering, and the coverage terms stay on the draw — a
   * multi-pass plan merges them into its terminal pass (see TaskCarriesCoverage). Returns
   * nullopt when no planner covers the color graph; failureOutcome then receives
   * AOTDecomposeOutcome::UnsupportedShape (the graph already lowered, so no lowering can block).
   */
  static std::optional<AOTEffectPlan> Plan(const DrawIR& ir,
                                           AOTDecomposeOutcome* failureOutcome = nullptr);

  /**
   * Whether a plan that needs a task may carry the draw's coverage terms on its terminal pass:
   * the terminal must be a chain pass (a linear-kernel terminal — YUV or perlin — serves through
   * a rebuilt processor and cannot merge terms), and the draw's blend must never need a dst
   * texture with coverage — the exact set the record-time fold admitted, because a Subtree term
   * (mask-filter semantics) modulates the source, which the coverage composite reproduces only
   * for those modes. A draw the gate refuses keeps the plain route, coverage attached.
   */
  static bool TaskCarriesCoverage(const AOTEffectPlan& plan,
                                  const std::vector<AOTCoverageTerm>& coverageTerms,
                                  BlendMode blendMode);

  /**
   * Whether a plan can only run as a plan task: more than one pass, or a single pass whose kernel
   * the prepare-time fused serving cannot express (the fused kernels take a draw's whole color
   * expression as one chain or perlin processor; every other single-pass kernel still needs the
   * task). Route itself applies this gate to original routes and returns Deferred for the ones it
   * turns away.
   */
  static bool PlanRequiresTask(const AOTEffectPlan& plan);

  /**
   * Plans processors for the fused serving: true when they lower and decompose into a single pass
   * of a kernel whose processor can stand in for a draw's whole color expression
   * (PointwiseChain, PointwiseGraph, or PerlinNoiseFill). A multi-pass plan never qualifies: its
   * intermediate passes need a plan task — so only the single-pass planners run, skipping the cut
   * and linear-split planners whose results this entry would always discard.
   */
  static bool PlanSinglePass(const std::vector<const FragmentProcessor*>& processors,
                             AOTEffectGraph* graph, AOTEffectPlan* plan);
};
}  // namespace tgfx
