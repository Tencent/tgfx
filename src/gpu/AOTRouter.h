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
};

/**
 * The routing steps shared by the two AOT entries: OpsCompositor::addDrawOp (on-screen draws) and
 * DrawingManager::fillRTWithFP (offscreen fills). Each entry collects the processors it wants
 * planned and supplies a SubmitPlan callback that turns a plan into a render task for its own
 * destination. The router owns planning and the materialization retry, so both entries apply the
 * same rules:
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
   * untouched, when no task can be built for it.
   */
  using SubmitPlan = std::function<bool(const AOTEffectGraph&, const AOTEffectPlan&)>;

  /**
   * Lowers and decomposes the processors and hands the plan to submit.
   */
  static AOTRouteResult Route(const std::vector<const FragmentProcessor*>& processors,
                              const SubmitPlan& submit);

  /**
   * Whether the materialization retry applies to a chain led by this processor: it is a two-child
   * blend and the materialization policy would rewrite at least one child under renderFlags (a
   * child that is required for correctness always qualifies; one that only improves matchability
   * qualifies outside NestedRasterization).
   */
  static bool RetryWarranted(const FragmentProcessor* leading, uint32_t renderFlags);

  /**
   * The materialization retry: rebuilds the leading processor, then routes it followed by the
   * trailing processors. The rebuilt processor is released whether or not the route is taken, so a
   * refused retry never puts materialized operands on the entry's original draw. A failed rebuild
   * reports NoPlan.
   */
  static AOTRouteResult RouteWithMaterializedLeading(
      const ColorChainRebuild& rebuildLeading,
      const std::vector<const FragmentProcessor*>& trailing, const SubmitPlan& submit);
};
}  // namespace tgfx
