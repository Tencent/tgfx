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

#include <memory>
#include <vector>
#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/aot/DrawIR.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/RenderTargetProxy.h"

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
 * The contract of one materialized edge: how much area it captures. Every edge is an RGBA8,
 * top-left-origin, exact-fit target allocated by AOTMaterializationPolicy, so crossing an edge
 * rounds the producer's output to the 1/255 grid.
 */
struct AOTMaterializedEdge {
  AOTEdgeReason reason = AOTEdgeReason::Split;
  /// The captured area and the coordinate offset the producer renders with.
  MaterializedGeometry geometry = {};
};

/**
 * One producer pass of an AOTRenderPlan: a fill draw that renders a materialized edge into its own
 * offscreen target. The target is cleared to transparent before the draw. A pass may sample the
 * targets of any earlier pass in the same plan, never a later one.
 */
struct AOTRenderPass {
  std::shared_ptr<RenderTargetProxy> target = nullptr;
  /// The fill draw (DrawingManager::makeFillDrawOp) that renders the edge into target.
  PlacementPtr<DrawOp> drawOp = nullptr;
  AOTMaterializedEdge edge = {};
};

/**
 * The executable form of an AOT route for one draw: the producer passes, ordered so that every pass
 * comes after the passes whose targets it samples, then the terminal pass, which replays the original draw onto its destination with terminalColors in
 * place of its color processors. The terminal pass may sample any producer. AOTPlanRenderTask runs
 * the whole plan as one task, so a producer and the passes that read it can never be split across
 * tasks.
 */
struct AOTRenderPlan {
  std::vector<AOTRenderPass> passes = {};
  /// The terminal pass of a non-chain plan (YUV or perlin): its color processors.
  DrawOp::ColorProcessorList terminalColors = {};
  /// The terminal pass of a chain plan: the cut segment's IR, rebuilt at prepare time.
  PlacementPtr<DrawIR> terminalIR = nullptr;

  /**
   * Whether every pass has a target and a draw, and the terminal pass carries exactly one of its
   * two forms. The pass order is the builder's responsibility and is not checked here.
   */
  bool isWellFormed() const {
    for (const auto& pass : passes) {
      if (pass.target == nullptr || pass.drawOp == nullptr) {
        return false;
      }
    }
    return terminalColors.empty() != (terminalIR == nullptr);
  }
};
}  // namespace tgfx
