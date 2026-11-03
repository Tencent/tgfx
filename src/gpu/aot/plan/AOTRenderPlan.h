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
#include <memory>
#include <vector>
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

/**
 * Why an edge exists, which decides how the draw stats count it: the multi-pass linear planner's
 * device-space splits (planMaterializedEdges) versus a materialized blend operand
 * (fpFlattenEdges).
 */
enum class AOTEdgeReason {
  /// A pass the multi-pass linear planner split off.
  Split,
  /// A blend operand subtree the planner materialized (budget-driven).
  OperandMaterialize,
};

/**
 * The pixel geometry of one materialization edge: the area to capture and the coordinate mapping
 * that keeps the materialized subtree sampling the space it saw inline. Derived purely from the
 * draw bounds and the apron, with no GPU resource attached, so a caller that issues several passes
 * over the same area computes it once and every pass agrees on it. isEmpty() marks a degenerate
 * area that cannot back a texture.
 */
struct MaterializedGeometry {
  /// The draw bounds after the apron outset and integer snapping. Its top-left drives both the
  /// coordOffset below and the translation a consumer needs in its sampling matrix.
  Rect bounds = {};
  /// The translation handed to DrawingManager::makeFillDrawOp so the subtree keeps seeing its
  /// original coordinate space while drawing into an origin-based offscreen target.
  Point coordOffset = {};
  int width = 0;
  int height = 0;

  bool isEmpty() const {
    return width <= 0 || height <= 0;
  }

  /// The byte footprint an RGBA8 target of this size occupies, for whichever draw metric the
  /// caller maintains.
  uint64_t byteSize() const {
    return static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4;
  }
};

/**
 * One producer pass of an AOTRenderPlan: a fill draw that renders a materialized edge into its own
 * offscreen target. The target is cleared to transparent before the draw. A pass may sample the
 * targets of any earlier pass in the same plan, never a later one.
 *
 * The edge contract is inline: every edge is an RGBA8, top-left-origin, exact-fit target
 * allocated by the planner's materialization step, so crossing it rounds the producer's output to
 * the 1/255 grid. `geometry` is the captured area and the coordinate offset the producer renders
 * with.
 */
struct AOTRenderPass {
  std::shared_ptr<RenderTargetProxy> target = nullptr;
  /// The fill draw (DrawingManager::makeFillDrawOp) that renders the edge into target.
  PlacementPtr<DrawOp> drawOp = nullptr;
  AOTEdgeReason reason = AOTEdgeReason::Split;
  MaterializedGeometry geometry = {};
};

/**
 * The executable form of an AOT route for one draw: the producer passes, ordered so that every pass
 * comes after the passes whose targets it samples, then the terminal pass, which replays the
 * original draw onto its destination with the cut segment's terminalIR in place of its own
 * expression. The terminal pass may sample any producer. AOTPlanRenderTask runs
 * the whole plan as one task, so a producer and the passes that read it can never be split across
 * tasks.
 */
struct AOTRenderPlan {
  std::vector<AOTRenderPass> passes = {};
  /// The terminal pass of a chain plan: the cut segment's IR, rebuilt at prepare time.
  PlacementPtr<DrawIR> terminalIR = nullptr;

  /**
   * Whether every pass has a target and a draw. The pass order is the builder's responsibility
   * and is not checked here.
   */
  bool isWellFormed() const {
    for (const auto& pass : passes) {
      if (pass.target == nullptr || pass.drawOp == nullptr) {
        return false;
      }
    }
    return true;
  }
};
}  // namespace tgfx
